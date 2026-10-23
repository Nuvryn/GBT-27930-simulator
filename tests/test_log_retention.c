/** @file test_log_retention.c @brief Filesystem ownership, grouping and multiprocess lease checks. */
#define _POSIX_C_SOURCE 200809L
#include "support/log_retention.h"
#include "support/log_sink.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(expr)                                                                           \
    do                                                                                        \
    {                                                                                         \
        if (!(expr))                                                                          \
        {                                                                                     \
            fprintf(stderr, "FAIL retention line %d: %s errno=%d\n", __LINE__, #expr, errno); \
            return 1;                                                                         \
        }                                                                                     \
    } while (0)

static int write_file(const char *path, const char *content)
{
    FILE *file = fopen(path, "w");
    if (file == NULL)
    {
        return -errno;
    }
    int rc = fputs(content, file) < 0 ? -EIO : 0;
    return fclose(file) == 0 ? rc : -EIO;
}
static void make_path(char *out_path, size_t capacity, unsigned sequence, const char *role, const char *suffix)
{
    (void) snprintf(out_path, capacity, "logs/20261023_1200%02u_%s%s", sequence, role, suffix);
}
static bool exists(unsigned sequence, const char *role, const char *suffix)
{
    char path[128];
    make_path(path, sizeof(path), sequence, role, suffix);
    return access(path, F_OK) == 0;
}
static int session(unsigned sequence, const char *role, bool application, bool capture)
{
    char app_path[128], capture_path[128], diagnostic_path[160];
    make_path(app_path, sizeof(app_path), sequence, role, ".log");
    make_path(capture_path, sizeof(capture_path), sequence, role, application ? ".log.candump.log" : ".candump.log");
    if (application)
    {
        CHECK(write_file(app_path, "application evidence\n") == 0);
        CHECK(log_retention_track(role, app_path, NULL) == 0);
    }
    if (capture)
    {
        CHECK(write_file(capture_path, "raw CAN evidence\n") == 0);
        (void) snprintf(diagnostic_path, sizeof(diagnostic_path), "%s.stderr.log", capture_path);
        CHECK(write_file(diagnostic_path, "capture diagnostics\n") == 0);
        CHECK(log_retention_track(role, application ? app_path : NULL, capture_path) == 0);
    }
    return 0;
}
static int test_grouping(void)
{
    CHECK(write_file("logs/20261023_120000_bms.log", "unmanaged automatic-looking file\n") == 0);
    CHECK(write_file("logs/manual.log", "user-selected file\n") == 0);
    CHECK(write_file("logs/.20261023_120000_bms.session", "not a valid session record\n") == 0);
    CHECK(log_retention_track("bms", "logs/invalid.log", NULL) == -EINVAL);
    CHECK(log_retention_track("../bms", "logs/20261023_120000_bms.log", NULL) == -EINVAL);
    CHECK(log_retention_track(NULL, NULL, NULL) == 0);
    for (unsigned idx = 1u; idx <= 5u; idx++)
    {
        CHECK(session(idx, "bms", true, true) == 0);
        log_retention_close();
    }
    for (unsigned idx = 1u; idx <= 5u; idx++)
    {
        bool retained = idx >= 3u;
        CHECK(exists(idx, "bms", ".log") == retained);
        CHECK(exists(idx, "bms", ".log.candump.log") == retained);
        CHECK(exists(idx, "bms", ".log.candump.log.stderr.log") == retained);
        char marker[128];
        (void) snprintf(marker, sizeof(marker), "logs/.20261023_1200%02u_bms.session", idx);
        CHECK((access(marker, F_OK) == 0) == retained);
    }
    for (unsigned idx = 1u; idx <= 4u; idx++)
    {
        CHECK(session(idx, "charger", false, true) == 0);
        log_retention_close();
    }
    CHECK(!exists(1u, "charger", ".candump.log") && !exists(1u, "charger", ".candump.log.stderr.log"));
    CHECK(exists(2u, "charger", ".candump.log") && exists(4u, "charger", ".candump.log"));
    CHECK(exists(3u, "bms", ".log") && exists(5u, "bms", ".log"));
    CHECK(exists(0u, "bms", ".log") && access("logs/manual.log", F_OK) == 0);
    return 0;
}
static int test_active_session(void)
{
    int ready[2], release[2];
    CHECK(pipe(ready) == 0 && pipe(release) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0)
    {
        (void) close(ready[0]);
        (void) close(release[1]);
        char message = session(1u, "bms", true, true) == 0 ? 'Y' : 'N';
        if (write(ready[1], &message, 1u) != 1 || read(release[0], &message, 1u) != 1)
        {
            _exit(1);
        }
        /* Simulate process death: the kernel must release leases without close(). */
        _exit(0);
    }
    (void) close(ready[1]);
    (void) close(release[0]);
    char message;
    CHECK(read(ready[0], &message, 1u) == 1 && message == 'Y');
    for (unsigned idx = 2u; idx <= 5u; idx++)
    {
        CHECK(session(idx, "bms", true, false) == 0);
        log_retention_close();
    }
    CHECK(exists(1u, "bms", ".log") && exists(1u, "bms", ".log.candump.log"));
    CHECK(!exists(2u, "bms", ".log"));
    CHECK(write(release[1], "X", 1u) == 1);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    (void) close(ready[0]);
    (void) close(release[1]);
    CHECK(session(6u, "bms", true, false) == 0);
    log_retention_close();
    CHECK(!exists(1u, "bms", ".log") && !exists(1u, "bms", ".log.candump.log") && !exists(3u, "bms", ".log"));
    CHECK(exists(4u, "bms", ".log") && exists(5u, "bms", ".log") && exists(6u, "bms", ".log"));
    return 0;
}
static int test_explicit_writer(void)
{
    CHECK(session(1u, "bms", true, false) == 0);
    log_retention_close();
    CHECK(log_sink_open(true, "logs/20261023_120001_bms.log", "bms") == 0);
    for (unsigned idx = 2u; idx <= 5u; idx++)
    {
        CHECK(session(idx, "bms", true, false) == 0);
        log_retention_close();
    }
    CHECK(exists(1u, "bms", ".log"));
    CHECK(log_sink_close() == 0);
    CHECK(session(6u, "bms", true, false) == 0);
    log_retention_close();
    CHECK(exists(1u, "bms", ".log"));
    return 0;
}
static int test_symlinks_and_failures(void)
{
    CHECK(write_file("outside.log", "must remain untouched\n") == 0);
    CHECK(session(1u, "bms", true, false) == 0);
    log_retention_close();
    CHECK(unlink("logs/20261023_120001_bms.log") == 0);
    CHECK(symlink("../outside.log", "logs/20261023_120001_bms.log") == 0);
    CHECK(symlink("../outside.log", "logs/.20261023_120000_bms.session") == 0);
    for (unsigned idx = 2u; idx <= 5u; idx++)
    {
        CHECK(session(idx, "bms", true, false) == 0);
        log_retention_close();
    }
    struct stat info;
    CHECK(lstat("logs/20261023_120001_bms.log", &info) == 0 && S_ISLNK(info.st_mode));
    CHECK(lstat("logs/.20261023_120000_bms.session", &info) == 0 && S_ISLNK(info.st_mode));
    CHECK(access("outside.log", F_OK) == 0);
    CHECK(unlink("logs/.retention.lock") == 0 && mkdir("logs/.retention.lock", 0700) == 0);
    CHECK(write_file("logs/20261023_120006_bms.log", "new file\n") == 0);
    CHECK(log_retention_track("bms", "logs/20261023_120006_bms.log", NULL) < 0);
    CHECK(exists(3u, "bms", ".log") && exists(4u, "bms", ".log") && exists(5u, "bms", ".log"));
    CHECK(rmdir("logs/.retention.lock") == 0);
    return 0;
}
static int run_isolated(int (*test_fn)(void))
{
    char directory[] = "retention_test_XXXXXX";
    int previous = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(previous >= 0 && mkdtemp(directory) != NULL && chdir(directory) == 0 && mkdir("logs", 0700) == 0);
    int result = test_fn();
    log_retention_close();
    DIR *logs = opendir("logs");
    CHECK(logs != NULL);
    struct dirent *entry;
    while ((entry = readdir(logs)) != NULL)
    {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
        {
            CHECK(unlinkat(dirfd(logs), entry->d_name, 0) == 0);
        }
    }
    CHECK(closedir(logs) == 0 && rmdir("logs") == 0);
    (void) unlink("outside.log");
    CHECK(fchdir(previous) == 0 && close(previous) == 0 && rmdir(directory) == 0);
    return result;
}
int main(void)
{
    if (run_isolated(test_grouping) != 0 || run_isolated(test_active_session) != 0 ||
        run_isolated(test_explicit_writer) != 0 || run_isolated(test_symlinks_and_failures) != 0)
    {
        return 1;
    }
    puts("PASS: three sessions per role, capture groups, active leases, crash release, explicit writers and ownership protection");
    return 0;
}
