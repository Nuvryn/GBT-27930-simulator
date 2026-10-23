/** @file log_sink.c @brief POSIX worker, Shanghai-time file names and persistent I/O failures. */
#define _POSIX_C_SOURCE 200809L
#include "support/log_sink.h"
#include "backends/support/linux/log_filename_time.h"
#include "log_retention_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static FILE *s_log_sink_file;
static char s_log_sink_path[512];
static atomic_int s_log_sink_error;
static pthread_mutex_t s_log_sink_output_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_log_sink_notice_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_log_sink_notice = PTHREAD_COND_INITIALIZER;
static pthread_t s_log_sink_thread;
static bool s_log_sink_started, s_log_sink_pending, s_log_sink_stopping;
static log_sink_drain_cb_t s_log_sink_drain_cb;
static void *s_log_sink_cb_user;
static char s_log_sink_prefix[7];
static size_t s_log_sink_prefix_len;
static bool s_log_sink_line_known, s_log_sink_file_only;

void log_sink_fail(int error)
{
    int expected = 0;
    if (error < 0)
    {
        (void) atomic_compare_exchange_strong(&s_log_sink_error, &expected, error);
    }
}
void log_sink_lock(void)
{
    (void) pthread_mutex_lock(&s_log_sink_output_mutex);
}
void log_sink_unlock(void)
{
    (void) pthread_mutex_unlock(&s_log_sink_output_mutex);
}
static void *log_sink_worker(void *user)
{
    (void) user;
    bool error_reported = false;
    for (;;)
    {
        (void) pthread_mutex_lock(&s_log_sink_notice_mutex);
        while (!s_log_sink_pending && !s_log_sink_stopping)
        {
            (void) pthread_cond_wait(&s_log_sink_notice, &s_log_sink_notice_mutex);
        }
        bool stopping = s_log_sink_stopping;
        s_log_sink_pending = false;
        (void) pthread_mutex_unlock(&s_log_sink_notice_mutex);
        s_log_sink_drain_cb(s_log_sink_cb_user);
        int error = log_sink_error();
        if (error != 0 && !error_reported)
        {
            char diagnostic[128];
            int len = snprintf(diagnostic, sizeof(diagnostic), "ERROR async_logger status=FAIL error=%d%s\n", error,
                error == -ENOSPC ? " buffer_full records_rejected" : " output_failed");
            if (len > 0 && (size_t) len < sizeof(diagnostic))
            {
                log_sink_write(diagnostic, (size_t) len);
            }
            error_reported = true;
        }
        if (stopping)
        {
            break;
        }
    }
    return NULL;
}
int log_sink_start(log_sink_drain_cb_t drain_cb, void *cb_user)
{
    if (drain_cb == NULL || s_log_sink_started)
    {
        return -EINVAL;
    }
    s_log_sink_drain_cb = drain_cb;
    s_log_sink_cb_user = cb_user;
    s_log_sink_stopping = false;
    s_log_sink_pending = false;
    int rc = pthread_create(&s_log_sink_thread, NULL, log_sink_worker, NULL);
    if (rc != 0)
    {
        return -rc;
    }
    s_log_sink_started = true;
    return 0;
}
void log_sink_notify(void)
{
    (void) pthread_mutex_lock(&s_log_sink_notice_mutex);
    s_log_sink_pending = true;
    (void) pthread_cond_signal(&s_log_sink_notice);
    (void) pthread_mutex_unlock(&s_log_sink_notice_mutex);
}
int log_sink_stop(void)
{
    if (s_log_sink_started)
    {
        (void) pthread_mutex_lock(&s_log_sink_notice_mutex);
        s_log_sink_stopping = true;
        (void) pthread_cond_signal(&s_log_sink_notice);
        (void) pthread_mutex_unlock(&s_log_sink_notice_mutex);
        int rc = pthread_join(s_log_sink_thread, NULL);
        if (rc != 0)
        {
            log_sink_fail(-rc);
            return -rc;
        }
        s_log_sink_started = false;
    }
    return log_sink_error();
}
static int log_sink_open_generated(const char *role)
{
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
    (void) snprintf(s_log_sink_path, sizeof(s_log_sink_path), "logs/%s_%s.log", stamp, role);
    int fd = open(s_log_sink_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0)
    {
        return -errno;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        int error = -errno;
        (void) close(fd);
        return error;
    }
    s_log_sink_file = fdopen(fd, "w");
    if (s_log_sink_file == NULL)
    {
        int error = -errno;
        (void) close(fd);
        return error;
    }
    return 0;
}
int log_sink_open(bool file_enabled, const char *path, const char *role)
{
    if (s_log_sink_file != NULL || s_log_sink_started)
    {
        return -EBUSY;
    }
    atomic_store(&s_log_sink_error, 0);
    s_log_sink_path[0] = '\0';
    s_log_sink_prefix_len = 0u;
    s_log_sink_line_known = false;
    s_log_sink_file_only = false;
    if (!file_enabled)
    {
        return 0;
    }
    if (path == NULL || path[0] == '\0')
    {
        return log_sink_open_generated(role);
    }
    if (strlen(path) >= sizeof(s_log_sink_path))
    {
        return -ENAMETOOLONG;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0)
    {
        return -errno;
    }
    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode))
    {
        (void) close(fd);
        return -EINVAL;
    }
    /* Acquire the writer lease before truncation, including explicitly named files. */
    if (flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        int error = -errno;
        (void) close(fd);
        return error;
    }
    int rc = log_retention_protect_file(fd);
    if (rc != 0 || ftruncate(fd, 0) != 0)
    {
        int error = rc != 0 ? rc : -errno;
        (void) close(fd);
        return error;
    }
    s_log_sink_file = fdopen(fd, "w");
    if (s_log_sink_file == NULL)
    {
        int error = -errno;
        (void) close(fd);
        return error;
    }
    (void) snprintf(s_log_sink_path, sizeof(s_log_sink_path), "%s", path);
    return 0;
}
const char *log_sink_path(void)
{
    return s_log_sink_path;
}
static void log_sink_console_write(const char *data, size_t len)
{
    if (len != 0u && fwrite(data, 1u, len, stdout) != len)
    {
        log_sink_fail(-EIO);
    }
}
/** The async ring may split records at any byte, including inside the tag. */
static void log_sink_console_filter(const char *data, size_t len)
{
    for (size_t pos = 0u; pos < len;)
    {
        if (!s_log_sink_line_known)
        {
            char byte = data[pos++];
            s_log_sink_prefix[s_log_sink_prefix_len++] = byte;
            if (s_log_sink_prefix_len == sizeof(s_log_sink_prefix) || byte == '\n')
            {
                s_log_sink_file_only = s_log_sink_prefix_len == sizeof(s_log_sink_prefix) &&
                                       memcmp(s_log_sink_prefix, "DETAIL ", sizeof(s_log_sink_prefix)) == 0;
                if (!s_log_sink_file_only)
                {
                    log_sink_console_write(s_log_sink_prefix, s_log_sink_prefix_len);
                }
                s_log_sink_line_known = byte != '\n';
                s_log_sink_prefix_len = 0u;
            }
        }
        else
        {
            const char *newline = memchr(data + pos, '\n', len - pos);
            size_t count = newline != NULL ? (size_t) (newline - (data + pos)) + 1u : len - pos;
            if (!s_log_sink_file_only)
            {
                log_sink_console_write(data + pos, count);
            }
            pos += count;
            if (newline != NULL)
            {
                s_log_sink_line_known = false;
                s_log_sink_file_only = false;
            }
        }
    }
}
void log_sink_write(const char *data, size_t len)
{
    if (data == NULL && len != 0u)
    {
        log_sink_fail(-EINVAL);
        return;
    }
    if (s_log_sink_file != NULL && (fwrite(data, 1u, len, s_log_sink_file) != len || fflush(s_log_sink_file) != 0))
    {
        log_sink_fail(-EIO);
    }
    log_sink_console_filter(data, len);
    if (fflush(stdout) != 0)
    {
        log_sink_fail(-EIO);
    }
}
int log_sink_error(void)
{
    return atomic_load(&s_log_sink_error);
}
int log_sink_close(void)
{
    (void) log_sink_stop();
    if (s_log_sink_prefix_len != 0u)
    {
        log_sink_console_write(s_log_sink_prefix, s_log_sink_prefix_len);
        s_log_sink_prefix_len = 0u;
        if (fflush(stdout) != 0)
        {
            log_sink_fail(-EIO);
        }
    }
    if (s_log_sink_file != NULL)
    {
        if (fclose(s_log_sink_file) != 0)
        {
            log_sink_fail(-EIO);
        }
        s_log_sink_file = NULL;
    }
    return log_sink_error();
}
