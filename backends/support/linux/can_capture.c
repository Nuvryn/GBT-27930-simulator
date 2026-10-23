/** @file can_capture.c @brief Spawn candump without a shell; supervise and persist its streams. */
#define _GNU_SOURCE
#include "support/can_capture.h"
#include "backends/support/linux/log_filename_time.h"
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
static pid_t s_can_capture_pid;
static pthread_t s_can_capture_thread;
static pthread_mutex_t s_can_capture_child_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool s_can_capture_started, s_can_capture_reaped;
static atomic_bool s_can_capture_stopping;
static atomic_int s_can_capture_error;
static char s_can_capture_path[512];
static FILE *s_can_capture_files[2];
static int s_can_capture_pipes[2] = {-1, -1};
static bool s_can_capture_line_start;

static void can_capture_fail(int error)
{
    int expected = 0;
    (void) atomic_compare_exchange_strong(&s_can_capture_error, &expected, error);
}
static uint64_t can_capture_now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        can_capture_fail(-EIO);
        return 0u;
    }
    return (uint64_t) now.tv_sec * 1000u + (uint64_t) now.tv_nsec / 1000000u;
}
static void can_capture_signal(int signal_number)
{
    (void) pthread_mutex_lock(&s_can_capture_child_mutex);
    if (!s_can_capture_reaped && kill(s_can_capture_pid, signal_number) != 0 && errno != ESRCH)
    {
        can_capture_fail(-errno);
    }
    (void) pthread_mutex_unlock(&s_can_capture_child_mutex);
}
static bool can_capture_reap(void)
{
    int status = 0;
    (void) pthread_mutex_lock(&s_can_capture_child_mutex);
    if (!s_can_capture_reaped)
    {
        pid_t result = waitpid(s_can_capture_pid, &status, WNOHANG);
        if (result == s_can_capture_pid)
        {
            s_can_capture_reaped = true;
            bool expected_stop = atomic_load(&s_can_capture_stopping) &&
                                 ((WIFEXITED(status) && (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 128 + SIGINT)) ||
                                     (WIFSIGNALED(status) && WTERMSIG(status) == SIGINT));
            if (!expected_stop)
            {
                can_capture_fail(-ECHILD);
                (void) fprintf(s_can_capture_files[1], "supervisor: unexpected child wait status=%d\n", status);
            }
        }
        else if (result < 0 && errno != EINTR)
        {
            can_capture_fail(-errno);
            s_can_capture_reaped = true;
        }
    }
    bool reaped = s_can_capture_reaped;
    (void) pthread_mutex_unlock(&s_can_capture_child_mutex);
    return reaped;
}
static void can_capture_read(unsigned stream)
{
    char buffer[4096];
    ssize_t len = read(s_can_capture_pipes[stream], buffer, sizeof(buffer));
    if (len > 0)
    {
        if (stream == 0u)
        {
            for (ssize_t idx = 0; idx < len; idx++)
            {
                if (s_can_capture_line_start && buffer[idx] != '(')
                {
                    can_capture_fail(-EOVERFLOW);
                }
                s_can_capture_line_start = buffer[idx] == '\n';
            }
        }
        if (stream == 1u)
        {
            /* candump -L has no expected stderr; drops/errors invalidate capture. */
            can_capture_fail(-EIO);
        }
        if (fwrite(buffer, 1u, (size_t) len, s_can_capture_files[stream]) != (size_t) len || fflush(s_can_capture_files[stream]) != 0)
        {
            can_capture_fail(-EIO);
        }
    }
    else if (len == 0 || (errno != EINTR && errno != EAGAIN))
    {
        if (len < 0)
        {
            can_capture_fail(-errno);
        }
        (void) close(s_can_capture_pipes[stream]);
        s_can_capture_pipes[stream] = -1;
    }
}
static void *can_capture_worker(void *user)
{
    (void) user;
    uint64_t stop_started = 0u;
    bool stop_timed = false, killed = false;
    for (;;)
    {
        struct pollfd events[2] = {{s_can_capture_pipes[0], POLLIN, 0}, {s_can_capture_pipes[1], POLLIN, 0}};
        int rc = poll(events, 2u, 20);
        if (rc < 0 && errno != EINTR)
        {
            can_capture_fail(-errno);
        }
        for (unsigned stream = 0u; stream < 2u; stream++)
        {
            if (events[stream].revents != 0)
            {
                can_capture_read(stream);
            }
        }
        bool reaped = can_capture_reap();
        if (reaped && s_can_capture_pipes[0] < 0 && s_can_capture_pipes[1] < 0)
        {
            break;
        }
        if (atomic_load(&s_can_capture_stopping))
        {
            uint64_t now = can_capture_now_ms();
            if (!stop_timed)
            {
                stop_started = now;
                stop_timed = true;
            }
            if (!killed && now - stop_started >= 500u)
            {
                can_capture_fail(-ETIMEDOUT);
                can_capture_signal(SIGKILL);
                killed = true;
            }
            if (reaped && now - stop_started >= 1000u)
            {
                /* Do not wait forever for an inherited pipe held by another process. */
                can_capture_fail(-ETIMEDOUT);
                break;
            }
        }
    }
    return NULL;
}
static void can_capture_close_files(void)
{
    for (unsigned stream = 0u; stream < 2u; stream++)
    {
        if (s_can_capture_pipes[stream] >= 0)
        {
            (void) close(s_can_capture_pipes[stream]);
            s_can_capture_pipes[stream] = -1;
        }
        if (s_can_capture_files[stream] != NULL)
        {
            if (fclose(s_can_capture_files[stream]) != 0)
            {
                can_capture_fail(-EIO);
            }
            s_can_capture_files[stream] = NULL;
        }
    }
}
static int can_capture_open_file(unsigned stream, const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0)
    {
        return -errno;
    }
    s_can_capture_files[stream] = fdopen(fd, "w");
    if (s_can_capture_files[stream] == NULL)
    {
        int error = -errno;
        (void) close(fd);
        return error;
    }
    return 0;
}
static int can_capture_choose_path(const char *path, const char *role)
{
    if (path != NULL && path[0] != '\0')
    {
        if (strlen(path) >= sizeof(s_can_capture_path))
        {
            return -ENAMETOOLONG;
        }
        (void) snprintf(s_can_capture_path, sizeof(s_can_capture_path), "%s", path);
        return 0;
    }
    if (role == NULL || (strcmp(role, "bms") != 0 && strcmp(role, "charger") != 0))
    {
        return -EINVAL;
    }
    if (mkdir("logs", 0755) != 0 && errno != EEXIST)
    {
        return -errno;
    }
    struct timespec now;
    char stamp[32];
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
    {
        return -EIO;
    }
    int rc = log_filename_time_format(now.tv_sec, stamp, sizeof(stamp));
    if (rc != 0)
    {
        return rc;
    }
    (void) snprintf(s_can_capture_path, sizeof(s_can_capture_path), "logs/%s_%s.candump.log", stamp, role);
    return 0;
}
int can_capture_start(const char *interface_name, const char *path, const char *role)
{
    if (s_can_capture_started)
    {
        return -EBUSY;
    }
    atomic_store(&s_can_capture_error, 0);
    atomic_store(&s_can_capture_stopping, false);
    s_can_capture_reaped = false;
    s_can_capture_line_start = true;
    /* No candump filters/options may be smuggled through the interface name. */
    if (interface_name == NULL || interface_name[0] == '\0' || strlen(interface_name) >= IFNAMSIZ ||
        strpbrk(interface_name, ",/ \t\r\n") != NULL || interface_name[0] == '-')
    {
        return -EINVAL;
    }
    int rc = can_capture_choose_path(path, role);
    if (rc != 0)
    {
        return rc;
    }
    rc = can_capture_open_file(0u, s_can_capture_path);
    char diagnostic_path[sizeof(s_can_capture_path) + 16u];
    (void) snprintf(diagnostic_path, sizeof(diagnostic_path), "%s.stderr.log", s_can_capture_path);
    if (rc == 0)
    {
        rc = can_capture_open_file(1u, diagnostic_path);
    }
    int pipes[2][2] = {{-1, -1}, {-1, -1}};
    for (unsigned stream = 0u; stream < 2u && rc == 0; stream++)
    {
        if (pipe2(pipes[stream], O_CLOEXEC) != 0)
        {
            rc = -errno;
        }
    }
    posix_spawn_file_actions_t actions;
    bool actions_initialized = false;
    if (rc == 0)
    {
        rc = -posix_spawn_file_actions_init(&actions);
        actions_initialized = rc == 0;
    }
    if (rc == 0)
    {
        rc = -posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    }
    for (unsigned stream = 0u; stream < 2u && rc == 0; stream++)
    {
        rc = -posix_spawn_file_actions_adddup2(&actions, pipes[stream][1], stream == 0u ? STDOUT_FILENO : STDERR_FILENO);
    }
    if (rc == 0)
    {
        char interface_filter[64];
        (void) snprintf(interface_filter, sizeof(interface_filter), "%s,0:0,#FFFFFFFF", interface_name);
        char *const args[] = {"candump", "-L", "-d", interface_filter, NULL};
        rc = -posix_spawnp(&s_can_capture_pid, "candump", &actions, NULL, args, environ);
    }
    if (actions_initialized)
    {
        (void) posix_spawn_file_actions_destroy(&actions);
    }
    for (unsigned stream = 0u; stream < 2u; stream++)
    {
        if (pipes[stream][1] >= 0)
        {
            (void) close(pipes[stream][1]);
        }
        s_can_capture_pipes[stream] = pipes[stream][0];
    }
    if (rc == 0)
    {
        rc = -pthread_create(&s_can_capture_thread, NULL, can_capture_worker, NULL);
        if (rc != 0)
        {
            can_capture_signal(SIGKILL);
            while (waitpid(s_can_capture_pid, NULL, 0) < 0 && errno == EINTR)
            {
            }
        }
    }
    if (rc != 0)
    {
        can_capture_close_files();
        return rc;
    }
    s_can_capture_started = true;
    /* candump has no ready handshake. Check early failures before sending protocol frames.
     * This startup grace is real time and does not claim atomic capture readiness.
     */
    for (unsigned wait = 0u; wait < 10u && can_capture_error() == 0; wait++)
    {
        struct timespec delay = {0, 10000000};
        (void) nanosleep(&delay, NULL);
    }
    rc = can_capture_error();
    if (rc != 0)
    {
        (void) can_capture_stop();
    }
    return rc;
}
int can_capture_error(void)
{
    return atomic_load(&s_can_capture_error);
}
const char *can_capture_path(void)
{
    return s_can_capture_path;
}
int can_capture_stop(void)
{
    if (s_can_capture_started)
    {
        /* Give candump time to receive the final SocketCAN loopback before signalling it.
         * Draining our stdout pipe alone cannot drain candump's kernel receive queue.
         */
        struct timespec settle = {0, 100000000};
        while (nanosleep(&settle, &settle) != 0 && errno == EINTR)
        {
        }
        atomic_store(&s_can_capture_stopping, true);
        can_capture_signal(SIGINT);
        int rc = pthread_join(s_can_capture_thread, NULL);
        if (rc != 0)
        {
            can_capture_fail(-rc);
            return -rc;
        }
        s_can_capture_started = false;
        can_capture_close_files();
    }
    return can_capture_error();
}
