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
