#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>
#include <dirent.h>

#include "backlight.h"

static int get(filepath_t path, long *value)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        warn("failed to open %s", path);
        return -1;
    }

    char buf[1024], *end = NULL;
    if (read(fd, buf, 1024) < 0)
        err(EXIT_FAILURE, "failed to read %s", path);

    errno = 0;
    *value = strtol(buf, &end, 10);
    if (errno || buf == end) {
        warn("not a number: %s", buf);
        return -1;
    }

    close(fd);
    return 0;
}

static int set(filepath_t path, long value)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        warn("failed to open %s", path);
        return -1;
    }

    char buf[1024];
    int len = snprintf(buf, 1024, "%ld", value);
    if (write(fd, buf, len) < 0)
        err(EXIT_FAILURE, "failed to set backlight");

    close(fd);
    return 0;
}

int backlight_init(struct backlight_t *b, const char *device)
{
    filepath_t path;

    snprintf(path, PATH_MAX, BACKLIGHT_ROOT "/%s/max_brightness", device);
    if (get(path, &b->max) < 0)
        return -1;

    snprintf(b->dev, PATH_MAX, BACKLIGHT_ROOT "/%s/brightness", device);
    return 0;
}

int backlight_set(struct backlight_t *b, double value)
{
    value = value / 100.0 * (double)b->max;
    return set(b->dev, (int)value);
}

double backlight_get(struct backlight_t *b)
{
    long value = 0;
    int rc = get(b->dev, &value);
    return rc ? rc : (double)value / (double)b->max * 100.0;
}

int backlight_find_best(struct backlight_t *b)
{
    long biggest = 0;
    struct dirent *dp;
    struct backlight_t node;
    DIR *dir = opendir(BACKLIGHT_ROOT);

    if (dir == NULL)
        err(EXIT_FAILURE, "failed to open directory");

    while ((dp = readdir(dir))) {
        /* HACK: intel_backlight broken for me, skip it */
        if (strcmp(dp->d_name, "intel_backlight") == 0)
            continue;

        if (dp->d_type & DT_LNK) {
            backlight_init(&node, dp->d_name);
            if (node.max > biggest) {
                biggest = node.max;
                *b = node;
            }
        }
    }

    closedir(dir);
    return 0;
}

// vim: et:sts=4:sw=4:cino=(0
