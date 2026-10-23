/** @file test_sim_log.c @brief Slow-output isolation, full-ring rejection and restart drain. */
#define _POSIX_C_SOURCE 200809L
#include "logging/sim_log.h"
#include "support/log_sink.h"
#include "backends/support/linux/log_filename_time.h"
#include "elog.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

extern void elog_async_output(uint8_t level, const char *log, size_t size);
static int test_log_shanghai_time(void)
{
    const char *original_zone = getenv("TZ");
    char *saved_zone = original_zone != NULL ? strdup(original_zone) : NULL;
    if (original_zone != NULL && saved_zone == NULL)
    {
        return 1;
    }
    const char *zones[] = {"UTC0", "CST-8", "EST5EDT,M3.2.0/2,M11.1.0/2", NULL};
    const time_t seconds[] = {0, 1704038399, 1704038400, 1709222399, 1709222400};
    const char *expected[] = {"19700101_080000", "20231231_235959", "20240101_000000", "20240229_235959", "20240301_000000"};
    int result = 0;
    for (size_t zone = 0u; zone < sizeof(zones) / sizeof(zones[0]); zone++)
    {
        if ((zones[zone] != NULL ? setenv("TZ", zones[zone], 1) : unsetenv("TZ")) != 0)
        {
            result = 1;
            break;
        }
        tzset();
        for (size_t idx = 0u; idx < sizeof(seconds) / sizeof(seconds[0]); idx++)
        {
            char stamp[32];
            if (log_filename_time_format(seconds[idx], stamp, sizeof(stamp)) != 0 || strcmp(stamp, expected[idx]) != 0)
            {
                result = 1;
            }
        }
    }
    char small[2];
    time_t maximum = (time_t) (sizeof(time_t) == sizeof(int32_t) ? INT32_MAX : INT64_MAX);
    if (log_filename_time_format(0, small, sizeof(small)) != -ENOSPC || small[0] != '\0' ||
        log_filename_time_format(0, NULL, 0u) != -EINVAL || log_filename_time_format(maximum, small, sizeof(small)) != -EOVERFLOW)
    {
        result = 1;
    }
    if ((saved_zone != NULL ? setenv("TZ", saved_zone, 1) : unsetenv("TZ")) != 0)
    {
        result = 1;
    }
    free(saved_zone);
    tzset();
    return result;
}
static void *test_log_reader(void *user)
{
    int fd = *(int *) user;
    char buffer[4096];
    while (read(fd, buffer, sizeof(buffer)) > 0)
    {
    }
    return NULL;
}
static int test_log_slow_output(void)
{
    int pipes[2];
    int saved = dup(STDOUT_FILENO);
    if (saved < 0 || pipe(pipes) != 0)
    {
        return 1;
    }
    int flags = fcntl(pipes[1], F_GETFL);
    if (flags < 0 || fcntl(pipes[1], F_SETFL, flags | O_NONBLOCK) < 0)
    {
        return 1;
    }
    char fill[4096] = {0};
    while (write(pipes[1], fill, sizeof(fill)) > 0)
    {
    }
    if (errno != EAGAIN || fcntl(pipes[1], F_SETFL, flags) < 0 || dup2(pipes[1], STDOUT_FILENO) < 0)
    {
        return 1;
    }
    (void) close(pipes[1]);
    if (sim_log_open(false, NULL, "bms") != 0)
    {
        return 1;
    }
    /* Pipe is full and has no reader yet: a synchronous producer would hang here. */
    for (unsigned idx = 0u; idx < 100u; idx++)
    {
        sim_log_event("TEST", "slow-output record=%u", idx);
    }
    pthread_t reader;
    if (pthread_create(&reader, NULL, test_log_reader, &pipes[0]) != 0)
    {
        return 1;
    }
    int result = sim_log_close();
    (void) dup2(saved, STDOUT_FILENO);
    (void) close(saved);
    (void) pthread_join(reader, NULL);
    (void) close(pipes[0]);
    return result == 0 ? 0 : 1;
}
int main(void)
{
    if (test_log_shanghai_time() != 0 || test_log_slow_output() != 0 || sim_log_open(true, "async_log_test.log", "bms") != 0)
    {
        return 1;
    }
    /* Holding the consumer's ring lock makes overflow deterministic. */
    char data[1024];
    memset(data, 'X', sizeof(data));
    log_sink_lock();
    for (unsigned idx = 0u; idx < 100u; idx++)
    {
        elog_async_output(ELOG_LVL_INFO, data, sizeof(data));
    }
    log_sink_unlock();
    if (log_sink_error() != -ENOSPC || sim_log_close() != -ENOSPC)
    {
        return 1;
    }
    if (sim_log_open(true, "async_log_test.log", "bms") != 0)
    {
        return 1;
    }
    sim_log_time(123u);
    sim_log_event("TEST", "final-drained-record");
    if (sim_log_close() != 0)
    {
        return 1;
    }
    FILE *file = fopen("async_log_test.log", "rb");
    char buffer[4096] = {0};
    if (file == NULL)
    {
        return 1;
    }
    size_t len = fread(buffer, 1u, sizeof(buffer) - 1u, file);
    (void) fclose(file);
    (void) unlink("async_log_test.log");
    if (len == 0u || strstr(buffer, "[123 ms] final-drained-record") == NULL || strstr(buffer, "XXX") != NULL)
    {
        return 1;
    }
    /* A regular file opens successfully, then fails on the worker's write. */
    struct rlimit saved_limit;
    if (getrlimit(RLIMIT_FSIZE, &saved_limit) != 0)
    {
        return 1;
    }
    struct rlimit limited = saved_limit;
    limited.rlim_cur = 1u;
    void (*saved_handler)(int) = signal(SIGXFSZ, SIG_IGN);
    if (saved_handler == SIG_ERR || sim_log_open(true, "async_log_failure.log", "bms") != 0 || setrlimit(RLIMIT_FSIZE, &limited) != 0)
    {
        return 1;
    }
    sim_log_event("TEST", "worker-write-must-fail");
    int close_result = sim_log_close();
    (void) setrlimit(RLIMIT_FSIZE, &saved_limit);
    (void) signal(SIGXFSZ, saved_handler);
    (void) unlink("async_log_failure.log");
    if (close_result != -EIO)
    {
        return 1;
    }
    puts("PASS: blocked I/O stays on worker, overflow visible, restart clean and final record drained");
    return 0;
}
