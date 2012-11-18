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

#ifndef BACKLIGHT_H
#define BACKLIGHT_H

#include <limits.h>

#define BACKLIGHT_ROOT "/sys/class/backlight"

#define CLAMP(x, low, high) \
    __extension__ ({ \
        typeof(x) _x = (x); \
        typeof(low) _low = (low); \
        typeof(high) _high = (high); \
        ((_x > _high) ? _high : ((_x < _low) ? _low : _x)); \
    })

typedef char filepath_t[PATH_MAX];

struct backlight_t {
    long max;
    filepath_t dev;
};

int backlight_init(struct backlight_t *b, const char *device);
int backlight_set(struct backlight_t *b, double value);
double backlight_get(struct backlight_t *b);
int backlight_find_best(struct backlight_t *light);

#endif
