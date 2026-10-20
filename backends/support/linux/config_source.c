/**
 * @file config_source.c
 * Linux/Windows host backend - configuration-file source (config_source_load implementation).
 *
 * Contract (see support/config_source.h):
 *   config_source_load always returns a malloc copy; config_source_t.owns is always true,
 *   meaning the caller is responsible for free. Returning a static literal pointer is forbidden.
 *
 * src semantics:
 *   This backend is a file backend; src must be a config file path (e.g. "config/default.json");
 *   when src == NULL it returns -1 (a file backend cannot decide the built-in default).
 *
 * Implementation: fopen("rb") -> fseek/ftell to get the file length -> malloc(len+1) -> fread
 *       -> data[len]='\0' -> out->text = data, out->owns = true.
 * Any failure (file open failure / illegal length / short read) always returns -1, and the out
 * contents are undefined (per the header contract, the caller must not free on the failure path).
 */

#include "support/config_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int config_source_load(config_source_t *out, const char *src)
{
    if (!out || !src)
        return -1; /* the file backend requires a path */

    FILE *fp = fopen(src, "rb");
    if (!fp)
        return -1;

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return -1;
    }
    long len = ftell(fp);
    if (len <= 0 || len > 65536L)
    {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return -1;
    }

    char *data = (char *) malloc((size_t) len + 1);
    if (!data)
    {
        fclose(fp);
        return -1;
    }

    size_t nread = fread(data, 1, (size_t) len, fp);
    fclose(fp);
    if (nread < (size_t) len || memchr(data, '\0', nread) != NULL)
    {
        free(data);
        return -1;
    }
    data[nread] = '\0';

    out->text = data;
    out->owns = true;
    return 0;
}

void config_source_free(config_source_t *cfg)
{
    if (!cfg)
        return;

    if (cfg->owns)
        free((void *) cfg->text);

    cfg->text = NULL;
    cfg->owns = false;
}
