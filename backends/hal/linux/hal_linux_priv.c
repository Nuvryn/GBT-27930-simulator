/*
 * hal_linux_priv.c
 *
 * Private helper implementation shared by the Linux HAL backend.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include "hal_linux_priv.h"

hal_linux_dev_t *hal_linux_dev_alloc(void)
{
	hal_linux_dev_t *d = malloc(sizeof(*d));

	if (d == NULL)
		return NULL;

	memset(d, 0, sizeof(*d));
	if (pthread_mutex_init(&d->lock, NULL) != 0) {
		free(d);
		return NULL;
	}
	d->fd = -1;
	return d;
}

void hal_linux_dev_free(hal_linux_dev_t *d)
{
	if (d == NULL)
		return;
	if (d->fd >= 0)
		(void)close(d->fd);
	(void)pthread_mutex_destroy(&d->lock);
	free(d);
}

void hal_linux_dev_lock(hal_linux_dev_t *d)
{
	(void)pthread_mutex_lock(&d->lock);
}

void hal_linux_dev_unlock(hal_linux_dev_t *d)
{
	(void)pthread_mutex_unlock(&d->lock);
}

int hal_linux_parse_trailing_index(const char *name, int *index)
{
	const char *p;
	long v;
	char *end;

	if (name == NULL || index == NULL)
		return -EINVAL;

	p = name;
	while (*p != '\0')
		p++;
	/* Walk back over the trailing decimal digits. */
	while (p > name && p[-1] >= '0' && p[-1] <= '9')
		p--;
	if (*p == '\0')
		return -EINVAL; /* no trailing digits */

	v = strtol(p, &end, 10);
	if (end == p || *end != '\0' || v < 0 || v > 0x7fffffffL)
		return -EINVAL;

	*index = (int)v;
	return 0;
}

int hal_linux_write_text_file(const char *path, const char *text)
{
	FILE *f;
	size_t n, w;

	if (path == NULL || text == NULL)
		return -EINVAL;

	f = fopen(path, "w");
	if (f == NULL)
		return -errno;

	n = strlen(text);
	while (n > 0) {
		w = fwrite(text, 1, n, f);
		if (w == 0) {
			int e = errno;
			(void)fclose(f);
			return -e;
		}
		n -= w;
		text += w;
	}

	if (fclose(f) != 0)
		return -errno;
	return 0;
}

int hal_linux_read_text_file(const char *path, char *buf, size_t buflen)
{
	FILE *f;
	size_t n;

	if (path == NULL || buf == NULL || buflen == 0)
		return -EINVAL;

	f = fopen(path, "r");
	if (f == NULL)
		return -errno;

	n = fread(buf, 1, buflen - 1, f);
	(void)fclose(f);
	buf[n] = '\0';
	return 0;
}

int hal_linux_write_int_file(const char *path, long value)
{
	char text[32];
	int n;

	n = snprintf(text, sizeof(text), "%ld", value);
	if (n < 0 || n >= (int)sizeof(text))
		return -EINVAL;
	return hal_linux_write_text_file(path, text);
}

int hal_linux_read_int_file(const char *path, long *out)
{
	char text[24];
	char *end;
	long v;

	if (out == NULL)
		return -EINVAL;
	if (hal_linux_read_text_file(path, text, sizeof(text)) != 0)
		return -EINVAL;

	v = strtol(text, &end, 10);
	/* A trailing newline is normal for sysfs attribute files. */
	if (end == text || (*end != '\0' && *end != '\n' && *end != '\r'))
		return -EIO;

	*out = v;
	return 0;
}

int hal_linux_dir_exists(const char *path)
{
	struct stat st;

	if (path == NULL)
		return -EINVAL;
	if (stat(path, &st) != 0)
		return -errno;
	if (!S_ISDIR(st.st_mode))
		return -ENOTDIR;
	return 0;
}