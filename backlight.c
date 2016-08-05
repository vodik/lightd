/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) Simon Gomizelj, 2012
 */

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

inline double clamp(double v, double low, double high)
{
    return (v > high) ? high : (v < low) ? low : v;
}

static int get(filepath_t path, long *value)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        warn("failed to open to read to backlight %s", path);
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
        warn("failed to open to write to backlight %s", path);
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
    value = clamp(value, 0.0, 100.0) / 100.0 * (double)b->max;
    return set(b->dev, (int)(value + 0.5));
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
