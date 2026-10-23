/** @file libvncserver_port.c @brief Linux memory display with isolated VNC I/O and bounded pointer input. */
#define _GNU_SOURCE
#include "libvncserver_port.h"
#include "lvgl/lvgl.h"
#include <rfb/rfb.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#define UI_WEB_INPUT_CAPACITY 32u

typedef struct
{
    const char *path;
    const unsigned char *data;
    size_t size;
} ui_web_asset_t;
#include "web_assets.inc"
/* Only the upstream HTTP translation unit maps fopen to this resource reader. */
FILE *ui_web_asset_open(const char *path, const char *mode)
{
    const char prefix[] = "/__gbt_web__";
    if (strcmp(mode, "r") != 0 || strncmp(path, prefix, sizeof(prefix) - 1u) != 0)
    {
        errno = ENOENT;
        return NULL;
    }
    for (size_t idx = 0u; idx < sizeof(ui_web_assets) / sizeof(ui_web_assets[0]); idx++)
    {
        if (strcmp(path + sizeof(prefix) - 1u, ui_web_assets[idx].path) == 0)
        {
            return fmemopen((void *) ui_web_assets[idx].data, ui_web_assets[idx].size, "r");
        }
    }
    errno = ENOENT;
    return NULL;
}
typedef struct
{
    int32_t x, y;
    bool pressed;
} ui_web_pointer_t;
typedef struct
{
    rfbScreenInfoPtr server;
    rfbClientPtr client;
    pthread_t thread;
    pthread_mutex_t mutex;
    atomic_bool stop;
    atomic_int error;
    bool started, dirty, release_pending;
    unsigned width, height, interval_ms;
    uint32_t accepted_ms;
    unsigned x1, y1, x2, y2;
    uint32_t *pixels, *wire;
    void *draw;
    ui_web_pointer_t inputs[UI_WEB_INPUT_CAPACITY], pointer;
    unsigned head, count;
} ui_web_state_t;
static ui_web_state_t *s_ui_web;
static uint32_t ui_web_now(void)
{
    struct timespec stamp;
    (void) clock_gettime(CLOCK_MONOTONIC, &stamp);
    return (uint32_t) ((uint64_t) stamp.tv_sec * 1000u + (uint64_t) stamp.tv_nsec / 1000000u);
}
static void ui_web_disconnected(rfbClientPtr client)
{
    ui_web_state_t *state = client->screen->screenData;
    if (state->client != client)
    {
        return;
    }
    state->client = NULL;
    (void) pthread_mutex_lock(&state->mutex);
    state->head = state->count = 0u;
    state->release_pending = true;
    (void) pthread_mutex_unlock(&state->mutex);
}
static enum rfbNewClientAction ui_web_connected(rfbClientPtr client)
{
    ui_web_state_t *state = client->screen->screenData;
    if (state->client != NULL)
    {
        if (state->client->sock != RFB_INVALID_SOCKET)
        {
            return RFB_CLIENT_REFUSE;
        }
        /* Closed clients can await upstream cleanup after a new accept. */
        ui_web_disconnected(state->client);
    }
    state->client = client;
    state->accepted_ms = ui_web_now();
    client->clientGoneHook = ui_web_disconnected;
    return RFB_CLIENT_ACCEPT;
}
static void ui_web_pointer(int buttons, int x, int y, rfbClientPtr client)
{
    ui_web_state_t *state = client->screen->screenData;
    ui_web_pointer_t pointer = {.x = x < 0 ? 0 : (x >= (int) state->width ? (int32_t) state->width - 1 : x),
        .y = y < 0 ? 0 : (y >= (int) state->height ? (int32_t) state->height - 1 : y),
        .pressed = (buttons & 1) != 0};
    (void) pthread_mutex_lock(&state->mutex);
    if (state->count != 0u && state->inputs[(state->head + state->count - 1u) % UI_WEB_INPUT_CAPACITY].pressed == pointer.pressed)
    {
        state->inputs[(state->head + state->count - 1u) % UI_WEB_INPUT_CAPACITY] = pointer;
    }
    else if (state->count < UI_WEB_INPUT_CAPACITY)
    {
        state->inputs[(state->head + state->count) % UI_WEB_INPUT_CAPACITY] = pointer;
        state->count++;
    }
    else
    {
        state->head = state->count = 0u;
        state->release_pending = true;
        (void) pthread_mutex_unlock(&state->mutex);
        rfbCloseClient(client);
        return;
    }
    (void) pthread_mutex_unlock(&state->mutex);
}
static void ui_web_read(lv_indev_t *device, lv_indev_data_t *data)
{
    ui_web_state_t *state = lv_indev_get_user_data(device);
    (void) pthread_mutex_lock(&state->mutex);
    if (state->release_pending)
    {
        state->pointer.pressed = false;
        state->release_pending = false;
        /* Cancel the gesture, rather than turn a disconnect into a button click. */
        lv_indev_wait_release(device);
    }
    else if (state->count != 0u)
    {
        state->pointer = state->inputs[state->head];
        state->head = (state->head + 1u) % UI_WEB_INPUT_CAPACITY;
        state->count--;
    }
    data->point.x = state->pointer.x;
    data->point.y = state->pointer.y;
    data->state = state->pointer.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = state->count != 0u;
    (void) pthread_mutex_unlock(&state->mutex);
}
static void ui_web_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    ui_web_state_t *state = lv_display_get_user_data(display);
    if (area->x1 < 0 || area->y1 < 0 || area->x2 >= (int32_t) state->width || area->y2 >= (int32_t) state->height)
    {
        atomic_store(&state->error, -EOVERFLOW);
        lv_display_flush_ready(display);
        return;
    }
    unsigned x1 = (unsigned) area->x1, y1 = (unsigned) area->y1, x2 = (unsigned) area->x2, y2 = (unsigned) area->y2;
    size_t bytes = ((size_t) x2 - x1 + 1u) * sizeof(uint32_t);
    (void) pthread_mutex_lock(&state->mutex);
    for (unsigned y = y1; y <= y2; y++)
    {
        memcpy(state->pixels + (size_t) y * state->width + x1, pixels, bytes);
        pixels += bytes;
    }
    if (!state->dirty)
    {
        state->x1 = x1;
        state->y1 = y1;
        state->x2 = x2;
        state->y2 = y2;
    }
    else
    {
        if (x1 < state->x1)
        {
            state->x1 = x1;
        }
        if (y1 < state->y1)
        {
            state->y1 = y1;
        }
        if (x2 > state->x2)
        {
            state->x2 = x2;
        }
        if (y2 > state->y2)
        {
            state->y2 = y2;
        }
    }
    state->dirty = true;
    (void) pthread_mutex_unlock(&state->mutex);
    lv_display_flush_ready(display);
}
static void *ui_web_network(void *user)
{
    ui_web_state_t *state = user;
    uint32_t last = ui_web_now();
    while (!atomic_load(&state->stop))
    {
        uint32_t now = ui_web_now();
        if (now - last >= state->interval_ms)
        {
            bool dirty;
            unsigned x1, y1, x2, y2;
            (void) pthread_mutex_lock(&state->mutex);
            dirty = state->dirty;
            x1 = state->x1;
            y1 = state->y1;
            x2 = state->x2;
            y2 = state->y2;
            if (dirty)
            {
                for (unsigned y = y1; y <= y2; y++)
                {
                    size_t offset = (size_t) y * state->width + x1;
                    memcpy(state->wire + offset, state->pixels + offset, ((size_t) x2 - x1 + 1u) * sizeof(uint32_t));
                }
                state->dirty = false;
            }
            (void) pthread_mutex_unlock(&state->mutex);
            if (dirty)
            {
                rfbMarkRectAsModified(state->server, (int) x1, (int) y1, (int) x2 + 1, (int) y2 + 1);
            }
            last = now;
        }
        if (state->client != NULL && state->client->state != RFB_NORMAL && now - state->accepted_ms >= 5000u)
        {
            rfbCloseClient(state->client);
        }
        (void) rfbProcessEvents(state->server, 5000);
        if (!rfbIsActive(state->server))
        {
            atomic_store(&state->error, -ENETDOWN);
            break;
        }
    }
    rfbShutdownServer(state->server, TRUE);
    return NULL;
}
int ui_web_open(const ui_display_options_t *options, void **display)
{
    if (s_ui_web != NULL)
    {
        return -EBUSY;
    }
    if (options->width < 480u || options->width > 1920u || options->height < 272u || options->height > 1080u ||
        options->fps == 0u || options->fps > 30u || options->rotation != 0u || options->web_port == options->vnc_port ||
        options->web_port == 0u || options->web_port > 65535u || options->vnc_port == 0u || options->vnc_port > 65535u)
    {
        return -EINVAL;
    }
    struct in_addr address;
    if (options->listen == NULL || inet_pton(AF_INET, options->listen, &address) != 1)
    {
        return -EINVAL;
    }
    ui_web_state_t *state = calloc(1u, sizeof(*state));
    if (state == NULL)
    {
        return -ENOMEM;
    }
    int rc = pthread_mutex_init(&state->mutex, NULL);
    if (rc != 0)
    {
        free(state);
        return -rc;
    }
    atomic_init(&state->stop, false);
    atomic_init(&state->error, 0);
    s_ui_web = state;
    state->width = options->width;
    state->height = options->height;
    state->interval_ms = (1000u + options->fps - 1u) / options->fps;
    size_t size = (size_t) state->width * state->height * sizeof(uint32_t);
    size_t draw_size = (size_t) state->width * 64u * sizeof(uint32_t);
    state->pixels = calloc(1u, size);
    state->wire = calloc(1u, size);
    state->draw = malloc(draw_size);
    if (state->pixels == NULL || state->wire == NULL || state->draw == NULL)
    {
        return -ENOMEM;
    }
    state->server = rfbGetScreen(NULL, NULL, (int) state->width, (int) state->height, 8, 3, 4);
    if (state->server == NULL)
    {
        return -ENOMEM;
    }
    rfbScreenInfoPtr server = state->server;
    server->frameBuffer = (char *) state->wire;
    server->serverFormat.redShift = 16;
    server->serverFormat.greenShift = 8;
    server->serverFormat.blueShift = 0;
    server->screenData = state;
    server->desktopName = "GB/T 27930 Simulator";
    server->port = (int) options->vnc_port;
    server->httpPort = (int) options->web_port;
    server->listenInterface = address.s_addr;
    server->httpDir = "/__gbt_web__";
    server->httpEnableProxyConnect = FALSE;
    server->newClientHook = ui_web_connected;
    server->ptrAddEvent = ui_web_pointer;
    server->maxClientWait = 500;
    server->deferUpdateTime = 0;
    rfbMaxClientWait = 500;
    rfbInitServer(server);
    if (server->listenSock == RFB_INVALID_SOCKET || server->httpListenSock == RFB_INVALID_SOCKET)
    {
        return -EADDRINUSE;
    }
    lv_display_t *screen = lv_display_create((int32_t) state->width, (int32_t) state->height);
    if (screen == NULL)
    {
        return -ENOMEM;
    }
    lv_display_set_color_format(screen, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_user_data(screen, state);
    lv_display_set_flush_cb(screen, ui_web_flush);
    lv_display_set_buffers(screen, state->draw, NULL, (uint32_t) draw_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_indev_t *pointer = lv_indev_create();
    if (pointer == NULL)
    {
        return -ENOMEM;
    }
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer, screen);
    lv_indev_set_user_data(pointer, state);
    lv_indev_set_read_cb(pointer, ui_web_read);
    rc = pthread_create(&state->thread, NULL, ui_web_network, state);
    if (rc != 0)
    {
        return -rc;
    }
    state->started = true;
    *display = screen;
    return 0;
}
int ui_web_error(void)
{
    return s_ui_web == NULL ? -ENODEV : atomic_load(&s_ui_web->error);
}
void ui_web_close(void)
{
    ui_web_state_t *state = s_ui_web;
    if (state != NULL)
    {
        atomic_store(&state->stop, true);
        if (state->started)
        {
            (void) pthread_join(state->thread, NULL);
        }
        else if (state->server != NULL)
        {
            rfbShutdownServer(state->server, TRUE);
        }
        if (state->server != NULL)
        {
            rfbScreenCleanup(state->server);
        }
    }
    lv_deinit();
    if (state != NULL)
    {
        free(state->pixels);
        free(state->wire);
        free(state->draw);
        (void) pthread_mutex_destroy(&state->mutex);
        free(state);
        s_ui_web = NULL;
    }
}
