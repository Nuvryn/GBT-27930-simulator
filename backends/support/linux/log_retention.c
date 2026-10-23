/** @file log_retention.c @brief Bounded startup retention using session ownership records and flock leases. */
#define _POSIX_C_SOURCE 200809L
#include "support/log_retention.h"
#include "log_retention_internal.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG_RETENTION_KEEP_COUNT 3u
#define LOG_RETENTION_NAME_CAPACITY 96u
#define LOG_RETENTION_KEY_CAPACITY 32u
#define LOG_RETENTION_APP 1u
#define LOG_RETENTION_LINKED_CAPTURE 2u
#define LOG_RETENTION_CAPTURE 4u
static const char s_log_retention_header[] = "GBT27930_LOG_SESSION_V1\n";
static int s_log_retention_directory = -1;
static int s_log_retention_lease = -1;
static unsigned s_log_retention_mode;
static char s_log_retention_key[LOG_RETENTION_KEY_CAPACITY];

static bool has_path(const char *path)
{
    return path != NULL && path[0] != '\0';
}
static bool key_valid(const char *key, const char *role)
{
    if (strlen(key) != 16u + strlen(role) || key[8] != '_' || key[15] != '_' || strcmp(key + 16, role) != 0)
    {
        return false;
    }
    for (size_t idx = 0u; idx < 15u; idx++)
    {
        if (idx != 8u && (key[idx] < '0' || key[idx] > '9'))
        {
            return false;
        }
    }
    return true;
}
static bool extract_key(const char *name, const char *prefix, const char *suffix, const char *role, char *out_key)
{
    size_t prefix_len = strlen(prefix), suffix_len = strlen(suffix), len = strlen(name);
    if (len <= prefix_len + suffix_len || strncmp(name, prefix, prefix_len) != 0 || strcmp(name + len - suffix_len, suffix) != 0)
    {
        return false;
    }
    size_t key_len = len - prefix_len - suffix_len;
    if (key_len >= LOG_RETENTION_KEY_CAPACITY)
    {
        return false;
    }
    memcpy(out_key, name + prefix_len, key_len);
    out_key[key_len] = '\0';
    return key_valid(out_key, role);
}
static int read_mode(int fd, unsigned *out_mode)
{
    struct stat info;
    char record[64];
    if (fstat(fd, &info) != 0)
    {
        return -errno;
    }
    if (!S_ISREG(info.st_mode) || info.st_nlink != 1)
    {
        return -EINVAL;
    }
    ssize_t len = pread(fd, record, sizeof(record), 0);
    size_t header_len = sizeof(s_log_retention_header) - 1u;
    if (len != (ssize_t) (header_len + 2u) || memcmp(record, s_log_retention_header, header_len) != 0 || record[header_len + 1u] != '\n')
    {
        return -EINVAL;
    }
    unsigned mode = (unsigned) (record[header_len] - '0');
    if (mode != LOG_RETENTION_APP && mode != (LOG_RETENTION_APP | LOG_RETENTION_LINKED_CAPTURE) && mode != LOG_RETENTION_CAPTURE)
    {
        return -EINVAL;
    }
    *out_mode = mode;
    return 0;
}
static int write_mode(int fd, unsigned mode)
{
    char record[64];
    int len = snprintf(record, sizeof(record), "%s%u\n", s_log_retention_header, mode);
    ssize_t written = pwrite(fd, record, (size_t) len, 0);
    if (written != len)
    {
        return written < 0 ? -errno : -EIO;
    }
    return ftruncate(fd, (off_t) len) == 0 ? 0 : -errno;
}
static void close_files(int *fds, size_t count)
{
    for (size_t idx = 0u; idx < count; idx++)
    {
        if (fds[idx] >= 0)
        {
            (void) close(fds[idx]);
        }
    }
}
/** A marker proves ownership; leases protect active automatic and explicit writers. */
static int remove_session(const char *marker, const char *key)
{
    int marker_fd = openat(s_log_retention_directory, marker, O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (marker_fd < 0)
    {
        return errno == ENOENT || errno == ELOOP ? 0 : -errno;
    }
    if (flock(marker_fd, LOCK_EX | LOCK_NB) != 0)
    {
        int rc = errno == EWOULDBLOCK ? 0 : -errno;
        (void) close(marker_fd);
        return rc;
    }
    unsigned mode;
    if (read_mode(marker_fd, &mode) != 0)
    {
        (void) close(marker_fd);
        return 0;
    }
    char paths[3][LOG_RETENTION_NAME_CAPACITY];
    int fds[3] = {-1, -1, -1};
    size_t count = 0u;
    if ((mode & LOG_RETENTION_APP) != 0u)
    {
        (void) snprintf(paths[count++], sizeof(paths[0]), "%s.log", key);
    }
    if ((mode & (LOG_RETENTION_LINKED_CAPTURE | LOG_RETENTION_CAPTURE)) != 0u)
    {
        const char *suffix = (mode & LOG_RETENTION_LINKED_CAPTURE) != 0u ? ".log.candump.log" : ".candump.log";
        (void) snprintf(paths[count++], sizeof(paths[0]), "%s%s", key, suffix);
        (void) snprintf(paths[count++], sizeof(paths[0]), "%s%s.stderr.log", key, suffix);
    }
    int rc = 0;
    bool removable = true;
    for (size_t idx = 0u; idx < count; idx++)
    {
        fds[idx] = openat(s_log_retention_directory, paths[idx], O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (fds[idx] < 0)
        {
            if (errno == ENOENT)
            {
                continue;
            }
            removable = false;
            rc = errno == ELOOP ? 0 : -errno;
            break;
        }
        struct stat info;
        if (fstat(fds[idx], &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1)
        {
            removable = false;
            break;
        }
        if (flock(fds[idx], LOCK_EX | LOCK_NB) != 0)
        {
            removable = false;
            rc = errno == EWOULDBLOCK ? 0 : -errno;
            break;
        }
    }
    if (removable)
    {
        for (size_t idx = 0u; idx < count && rc == 0; idx++)
        {
            if (unlinkat(s_log_retention_directory, paths[idx], 0) != 0 && errno != ENOENT)
            {
                rc = -errno;
            }
        }
        if (rc == 0 && unlinkat(s_log_retention_directory, marker, 0) != 0 && errno != ENOENT)
        {
            rc = -errno;
        }
    }
    close_files(fds, count);
    (void) close(marker_fd);
    return rc;
}
static int prune(const char *role)
{
    int fd = openat(s_log_retention_directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
    {
        return -errno;
    }
    DIR *directory = fdopendir(fd);
    if (directory == NULL)
    {
        int rc = -errno;
        (void) close(fd);
        return rc;
    }
    char newest[LOG_RETENTION_KEEP_COUNT][LOG_RETENTION_KEY_CAPACITY] = {{0}};
    int rc = 0;
    for (unsigned pass = 0u; pass < 2u && rc == 0; pass++)
    {
        rewinddir(directory);
        for (;;)
        {
            errno = 0;
            struct dirent *entry = readdir(directory);
            if (entry == NULL)
            {
                rc = errno == 0 ? 0 : -errno;
                break;
            }
            char key[LOG_RETENTION_KEY_CAPACITY];
            if (!extract_key(entry->d_name, ".", ".session", role, key))
            {
                continue;
            }
            if (pass == 0u)
            {
                int marker_fd = openat(s_log_retention_directory, entry->d_name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
                unsigned mode;
                bool valid = marker_fd >= 0 && read_mode(marker_fd, &mode) == 0;
                if (marker_fd >= 0)
                {
                    (void) close(marker_fd);
                }
                if (!valid)
                {
                    continue;
                }
                for (size_t idx = 0u; idx < LOG_RETENTION_KEEP_COUNT; idx++)
                {
                    if (strcmp(key, newest[idx]) > 0)
                    {
                        for (size_t tail = LOG_RETENTION_KEEP_COUNT - 1u; tail > idx; tail--)
                        {
                            memcpy(newest[tail], newest[tail - 1u], sizeof(newest[0]));
                        }
                        (void) snprintf(newest[idx], sizeof(newest[0]), "%s", key);
                        break;
                    }
                }
            }
            else if (newest[LOG_RETENTION_KEEP_COUNT - 1u][0] != '\0' && strcmp(key, newest[LOG_RETENTION_KEEP_COUNT - 1u]) < 0)
            {
                rc = remove_session(entry->d_name, key);
                if (rc != 0)
                {
                    break;
                }
            }
        }
    }
    (void) closedir(directory);
    return rc;
}
int log_retention_track(const char *role, const char *application_path, const char *capture_path)
{
    if (!has_path(application_path) && !has_path(capture_path))
    {
        return 0;
    }
    if (role == NULL || (strcmp(role, "bms") != 0 && strcmp(role, "charger") != 0))
    {
        return -EINVAL;
    }
    char key[LOG_RETENTION_KEY_CAPACITY] = {0}, capture_key[LOG_RETENTION_KEY_CAPACITY];
    unsigned mode = 0u;
    if (has_path(application_path))
    {
        if (!extract_key(application_path, "logs/", ".log", role, key))
        {
            return -EINVAL;
        }
        mode = LOG_RETENTION_APP;
    }
    if (has_path(capture_path))
    {
        bool linked = mode == LOG_RETENTION_APP;
        if (!extract_key(capture_path, "logs/", linked ? ".log.candump.log" : ".candump.log", role, capture_key) || (linked && strcmp(key, capture_key) != 0))
        {
            return -EINVAL;
        }
        (void) snprintf(key, sizeof(key), "%s", capture_key);
        mode |= linked ? LOG_RETENTION_LINKED_CAPTURE : LOG_RETENTION_CAPTURE;
    }
    bool new_session = s_log_retention_lease < 0;
    if (!new_session && strcmp(key, s_log_retention_key) != 0)
    {
        return -EBUSY;
    }
    if (new_session)
    {
        s_log_retention_directory = open("logs", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (s_log_retention_directory < 0)
        {
            return -errno;
        }
    }
    int lock_fd = openat(s_log_retention_directory, ".retention.lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    int rc = lock_fd < 0 ? -errno : 0;
    struct stat lock_info;
    if (rc == 0 && (fstat(lock_fd, &lock_info) != 0 || !S_ISREG(lock_info.st_mode) || lock_info.st_nlink != 1))
    {
        rc = -EINVAL;
    }
    if (rc == 0 && flock(lock_fd, LOCK_EX) != 0)
    {
        rc = -errno;
    }
    char marker[LOG_RETENTION_NAME_CAPACITY];
    (void) snprintf(marker, sizeof(marker), ".%s.session", key);
    if (rc == 0 && new_session)
    {
        s_log_retention_lease = openat(s_log_retention_directory, marker, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (s_log_retention_lease < 0 || flock(s_log_retention_lease, LOCK_EX | LOCK_NB) != 0)
        {
            rc = -errno;
        }
        else
        {
            (void) snprintf(s_log_retention_key, sizeof(s_log_retention_key), "%s", key);
        }
    }
    if (rc == 0)
    {
        mode |= s_log_retention_mode;
        if (mode != LOG_RETENTION_APP && mode != (LOG_RETENTION_APP | LOG_RETENTION_LINKED_CAPTURE) && mode != LOG_RETENTION_CAPTURE)
        {
            rc = -EINVAL;
        }
        else
        {
            rc = write_mode(s_log_retention_lease, mode);
        }
        if (rc == 0)
        {
            s_log_retention_mode = mode;
            rc = prune(role);
        }
    }
    if (lock_fd >= 0)
    {
        (void) close(lock_fd);
    }
    if (rc != 0 && new_session)
    {
        log_retention_close();
    }
    return rc;
}
void log_retention_close(void)
{
    if (s_log_retention_lease >= 0)
    {
        (void) close(s_log_retention_lease);
    }
    if (s_log_retention_directory >= 0)
    {
        (void) close(s_log_retention_directory);
    }
    s_log_retention_lease = -1;
    s_log_retention_directory = -1;
    s_log_retention_mode = 0u;
    s_log_retention_key[0] = '\0';
}
int log_retention_protect_file(int fd)
{
    struct stat target;
    if (fstat(fd, &target) != 0)
    {
        return -errno;
    }
    int root = open("logs", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root < 0)
    {
        return errno == ENOENT ? 0 : -errno;
    }
    /* Do not create a lock/registry just because an explicit file was opened. */
    int lock_fd = openat(root, ".retention.lock", O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (lock_fd < 0)
    {
        int rc = errno == ENOENT ? 0 : -errno;
        (void) close(root);
        return rc;
    }
    struct stat lock_info;
    int rc = fstat(lock_fd, &lock_info) == 0 && S_ISREG(lock_info.st_mode) && lock_info.st_nlink == 1 ? 0 : -EINVAL;
    if (rc == 0 && flock(lock_fd, LOCK_EX) != 0)
    {
        rc = -errno;
    }
    DIR *directory = rc == 0 ? fdopendir(root) : NULL;
    if (directory == NULL)
    {
        if (rc == 0)
        {
            rc = -errno;
        }
        (void) close(root);
    }
    else
    {
        for (;;)
        {
            errno = 0;
            struct dirent *entry = readdir(directory);
            if (entry == NULL)
            {
                rc = errno == 0 ? 0 : -errno;
                break;
            }
            char key[LOG_RETENTION_KEY_CAPACITY], path[LOG_RETENTION_NAME_CAPACITY];
            if (!extract_key(entry->d_name, ".", ".session", "bms", key) && !extract_key(entry->d_name, ".", ".session", "charger", key))
            {
                continue;
            }
            (void) snprintf(path, sizeof(path), "%s.log", key);
            struct stat candidate;
            if (fstatat(root, path, &candidate, AT_SYMLINK_NOFOLLOW) != 0 || candidate.st_dev != target.st_dev || candidate.st_ino != target.st_ino)
            {
                continue;
            }
            int marker = openat(root, entry->d_name, O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
            unsigned mode;
            if (marker >= 0)
            {
                if (read_mode(marker, &mode) == 0 && (mode & LOG_RETENTION_APP) != 0u)
                {
                    /* Mode zero withdraws ownership and is deliberately ignored by prune(). */
                    rc = flock(marker, LOCK_EX | LOCK_NB) == 0 ? write_mode(marker, 0u) : -errno;
                }
                (void) close(marker);
            }
            else
            {
                rc = -errno;
            }
            break;
        }
        (void) closedir(directory);
    }
    (void) close(lock_fd);
    return rc;
}
