/** @file test_can_capture.c @brief Isolated child fixtures verify lifecycle and failures. */
#define _POSIX_C_SOURCE 200809L
#include "support/can_capture.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define CAPTURE_CHECK(expr)                                         \
    do                                                              \
    {                                                               \
        if (!(expr))                                                \
        {                                                           \
            fprintf(stderr, "FAIL line=%d: %s\n", __LINE__, #expr); \
            (void) can_capture_stop();                              \
            return 1;                                               \
        }                                                           \
    } while (0)
static char s_capture_directory[] = "capture_test_XXXXXX";
static char s_capture_program[256];
static char s_capture_output[256];
static char s_capture_diagnostic[280];

static void test_capture_remove_output(void)
{
    (void) unlink(s_capture_output);
    (void) unlink(s_capture_diagnostic);
}
static int test_capture_program(const char *body)
{
    FILE *file = fopen(s_capture_program, "w");
    if (file == NULL)
    {
        return -1;
    }
    (void) fprintf(file, "#!/bin/sh\n%s\n", body);
    if (fclose(file) != 0 || chmod(s_capture_program, 0700) != 0)
    {
        return -1;
    }
    return 0;
}
int main(void)
{
    CAPTURE_CHECK(mkdtemp(s_capture_directory) != NULL);
    (void) snprintf(s_capture_program, sizeof(s_capture_program), "%s/candump", s_capture_directory);
    (void) snprintf(s_capture_output, sizeof(s_capture_output), "%s/raw.log", s_capture_directory);
    (void) snprintf(s_capture_diagnostic, sizeof(s_capture_diagnostic), "%s.stderr.log", s_capture_output);
    CAPTURE_CHECK(setenv("PATH", s_capture_directory, 1) == 0);
    CAPTURE_CHECK(can_capture_start("vcan0", s_capture_output, "bms") == -ENOENT);
    test_capture_remove_output();
    CAPTURE_CHECK(can_capture_start("vcan0,123:7FF", s_capture_output, "bms") == -EINVAL);
    CAPTURE_CHECK(test_capture_program("exit 17") == 0);
    CAPTURE_CHECK(can_capture_start("vcan0", s_capture_output, "bms") == -ECHILD);
    test_capture_remove_output();
    CAPTURE_CHECK(test_capture_program("printf 'capture-failure\\n' >&2; trap 'exit 0' INT; while :; do :; done") == 0);
    CAPTURE_CHECK(can_capture_start("vcan0", s_capture_output, "bms") == -EIO);
    FILE *file = fopen(s_capture_diagnostic, "r");
    char buffer[256] = {0};
    CAPTURE_CHECK(file != NULL && fread(buffer, 1u, sizeof(buffer) - 1u, file) != 0u);
    (void) fclose(file);
    CAPTURE_CHECK(strstr(buffer, "capture-failure") != NULL);
    test_capture_remove_output();
    CAPTURE_CHECK(test_capture_program("trap 'exit 0' INT; printf '(1.000001) vcan0 123#01\\n'; while :; do :; done") == 0);
    CAPTURE_CHECK(can_capture_start("vcan0", s_capture_output, "bms") == 0);
    CAPTURE_CHECK(can_capture_start("vcan0", s_capture_output, "bms") == -EBUSY);
    CAPTURE_CHECK(strcmp(can_capture_path(), s_capture_output) == 0 && can_capture_stop() == 0);
    file = fopen(s_capture_output, "r");
    memset(buffer, 0, sizeof(buffer));
    CAPTURE_CHECK(file != NULL && fread(buffer, 1u, sizeof(buffer) - 1u, file) != 0u);
    (void) fclose(file);
    CAPTURE_CHECK(strstr(buffer, "(1.000001) vcan0 123#01") != NULL);
    CAPTURE_CHECK(can_capture_start("vcan0", s_capture_output, "bms") == -EEXIST);
    test_capture_remove_output();
    CAPTURE_CHECK(test_capture_program("trap '' INT; while :; do :; done") == 0);
    CAPTURE_CHECK(can_capture_start("vcan0", s_capture_output, "bms") == 0);
    CAPTURE_CHECK(can_capture_stop() == -ETIMEDOUT);
    test_capture_remove_output();
    /* stdout diagnostics from -d must be treated as missing raw-frame evidence. */
    CAPTURE_CHECK(test_capture_program("trap 'exit 0' INT; printf 'DROPCOUNT: 1\\n'; while :; do :; done") == 0);
    CAPTURE_CHECK(can_capture_start("vcan0", s_capture_output, "bms") == -EOVERFLOW);
    test_capture_remove_output();
    CAPTURE_CHECK(test_capture_program("trap 'exit 0' INT; printf '(1.000001) vcan0 123#01\\n'; while :; do :; done") == 0);
    struct rlimit saved_limit;
    CAPTURE_CHECK(getrlimit(RLIMIT_FSIZE, &saved_limit) == 0);
    struct rlimit limited = saved_limit;
    limited.rlim_cur = 1u;
    void (*saved_handler)(int) = signal(SIGXFSZ, SIG_IGN);
    CAPTURE_CHECK(saved_handler != SIG_ERR && setrlimit(RLIMIT_FSIZE, &limited) == 0);
    int write_failure = can_capture_start("vcan0", s_capture_output, "bms");
    (void) setrlimit(RLIMIT_FSIZE, &saved_limit);
    (void) signal(SIGXFSZ, saved_handler);
    CAPTURE_CHECK(write_failure == -EIO);
    test_capture_remove_output();
    CAPTURE_CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
    (void) unlink(s_capture_program);
    CAPTURE_CHECK(rmdir(s_capture_directory) == 0);
    puts("PASS: missing tool, invalid interface, startup failure, stderr, exclusive file, graceful/forced stop, drops and no child leaks");
    return 0;
}
