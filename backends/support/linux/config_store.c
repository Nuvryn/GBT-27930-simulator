/** @file config_store.c @brief Same-directory temporary file and atomic rename. */
#define _POSIX_C_SOURCE 200809L
#include "support/config_store.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int config_store_write(const char *path, const char *text, size_t length)
{
    if (path == NULL || path[0] == '\0' || text == NULL)
    {
        return -EINVAL;
    }
    char temporary[512], directory[512];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path) >= (int) sizeof(temporary))
    {
        return -ENAMETOOLONG;
    }
    struct stat original;
    if (stat(path, &original) != 0)
    {
        return -errno;
    }
    if (!S_ISREG(original.st_mode))
    {
        return -EINVAL;
    }
    int fd = mkstemp(temporary);
    if (fd < 0)
    {
        return -errno;
    }
    int rc = fchmod(fd, original.st_mode & 0777u) == 0 ? 0 : -errno;
    size_t offset = 0u;
    while (rc == 0 && offset < length)
    {
        ssize_t written = write(fd, text + offset, length - offset);
        if (written < 0)
        {
            if (errno != EINTR)
            {
                rc = -errno;
            }
        }
        else if (written == 0)
        {
            rc = -EIO;
        }
        else
        {
            offset += (size_t) written;
        }
    }
    if (rc == 0 && fsync(fd) != 0)
    {
        rc = -errno;
    }
    if (close(fd) != 0 && rc == 0)
    {
        rc = -errno;
    }
    if (rc == 0 && rename(temporary, path) != 0)
    {
        rc = -errno;
    }
    if (rc != 0)
    {
        (void) unlink(temporary);
        return rc;
    }
    (void) snprintf(directory, sizeof(directory), "%s", path);
    char *slash = strrchr(directory, '/');
    if (slash != NULL)
    {
        if (slash == directory)
        {
            slash[1] = '\0';
        }
        else
        {
            *slash = '\0';
        }
    }
    else
    {
        (void) snprintf(directory, sizeof(directory), ".");
    }
    int parent = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent < 0)
    {
        return -errno;
    }
    rc = fsync(parent) == 0 ? 0 : -errno;
    (void) close(parent);
    return rc;
}
