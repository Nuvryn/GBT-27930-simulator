/** @file test_sim_log.c @brief Slow-output isolation, full-ring rejection and restart drain. */
#define _POSIX_C_SOURCE 200809L
#include "logging/sim_log.h"
#include "support/log_sink.h"
#include "elog.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

extern void elog_async_output(uint8_t level, const char *log, size_t size);
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
    if (sim_log_open(false, NULL) != 0)
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
    if (test_log_slow_output() != 0 || sim_log_open(true, "async_log_test.log") != 0)
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
    if (sim_log_open(true, "async_log_test.log") != 0)
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
    if (saved_handler == SIG_ERR || sim_log_open(true, "async_log_failure.log") != 0 || setrlimit(RLIMIT_FSIZE, &limited) != 0)
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
