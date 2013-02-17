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
 * Copyright (C) Simon Gomizelj, 2013
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <getopt.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <errno.h>
#include <err.h>

#include <libudev.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <linux/input.h>

#include "backlight.h"

enum power {AC_ON, AC_OFF};

typedef void (* handler_fn)(int fd);

struct power_state_t {
    int epoll_fd;
    int timeout;
    double brightness;
    /* handler_fn handle_udev; */
};

static struct power_state_t States[2] = {
    [AC_ON] = {
        .timeout = -1,
        .brightness = 100
    },
    [AC_OFF] = {
        .timeout = 5 * 1000,
        .brightness = 5
    }
}, *state = NULL;

#define AC_BOTH -1

static enum power mode = AC_ON;

static int udev_fd;

static struct udev *udev;
static struct udev_monitor *mon;

static struct backlight_t b;

static uint8_t bit(int bit, const uint8_t *array)
{
    return array[bit / 8] & (1 << (bit % 8));
}

/* static void backlight_dim(struct backlight_t *b, double dim) */
/* { */
/*     double v = backlight_get(b); */

/*     if (dim) */
/*         backlight_set(b, CLAMP(v - dim, 1.5, 100)); */
/*     else if (blight > v) */
/*         backlight_set(b, blight); */

/*     blight = v; */
/* } */

// EPOLL CRAP {{{1
static void init_epoll(void)
{
    int i;

    for (i = 0; i < 2; ++i) {
        States[i].epoll_fd = epoll_create1(0);

        if (States[i].epoll_fd < 0)
            err(EXIT_FAILURE, "failed to start epoll");
    }
}

static void register_epoll(int fd, enum power mode)
{
    struct epoll_event event = {
        .data.fd = fd,
        .events  = EPOLLIN | EPOLLET
    };

    if ((mode == AC_OFF || mode == AC_BOTH) &&
        (epoll_ctl(States[AC_OFF].epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0))
        err(EXIT_FAILURE, "failed to add udev monitor to epoll");

    if ((mode == AC_ON || mode == AC_BOTH) &&
        (epoll_ctl(States[AC_ON].epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0))
        err(EXIT_FAILURE, "failed to add udev monitor to epoll");
}
// }}}

static void power_status(const char *online)
{
    enum power next = mode;

    if (strcmp("1", online) == 0)
        next = AC_ON;
    else if (strcmp("0", online) == 0)
        next = AC_OFF;

    if (next != mode) {
        printf("STATE CHANGE HERE\n");
        state = &States[next];
    }

    mode = next;
}

// {{{1 UDEV MAGIC
static void udev_enumerate(void)
{
    struct udev_list_entry *devices, *dev_list_entry;
    struct udev_device *dev;

    struct udev_enumerate *enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "power_supply");
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);

    udev_list_entry_foreach(dev_list_entry, devices) {
        const char *path = udev_list_entry_get_name(dev_list_entry);
        dev = udev_device_new_from_syspath(udev, path);

        const char *online = udev_device_get_property_value(dev, "POWER_SUPPLY_ONLINE");
        if (online) {
            power_status(online);
            udev_device_unref(dev);
            break;
        }

        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
}

static void udev_update(void)
{
    struct udev_device *dev = udev_monitor_receive_device(mon);

    if (dev) {
        const char *online = udev_device_get_property_value(dev, "POWER_SUPPLY_ONLINE");
        if (online)
            power_status(online);

        udev_device_unref(dev);
    }
}
// }}}

// {{{1 EVDEV INPUT
static int ev_adddevice(filepath_t path)
{
    int rc = 0;
    char name[256];
    uint8_t evtype_bitmask[(EV_MAX + 7) / 8];

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        err(EXIT_FAILURE, "failed to open %s", path);

    rc = ioctl(fd, EVIOCGBIT(0, EV_MAX), evtype_bitmask);
    if (rc < 0)
        goto cleanup;

    rc  = bit(EV_KEY, evtype_bitmask);
    rc |= bit(EV_REL, evtype_bitmask);
    rc |= bit(EV_ABS, evtype_bitmask);
    if (!rc)
        goto cleanup;

    rc = ioctl(fd, EVIOCGNAME(sizeof(name)), name);
    if (rc < 0)
        goto cleanup;

    printf("Monitoring device %s: %s\n", name, path);
    register_epoll(fd, AC_OFF);

cleanup:
    if (rc <= 0)
        close(fd);

    return rc;
}

static void ev_findall(void)
{
    filepath_t path;
    struct dirent *dp;
    DIR *dir = opendir("/dev/input");

    if (dir == NULL)
        err(EXIT_FAILURE, "failed to open directory");

    while ((dp = readdir(dir))) {
        if (dp->d_type & DT_CHR && strcmp("event", dp->d_name)) {
            snprintf(path, PATH_MAX, "/dev/input/%s", dp->d_name);
            ev_adddevice(path);
        }
    }

    closedir(dir);
}
// }}}

static int run()
{
    struct epoll_event events[64];
    int dim_timeout = state->timeout;

    while (true) {
        printf("waiting with timeout: %d\n", dim_timeout);
        int i, n = epoll_wait(state->epoll_fd, events, 64, dim_timeout);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            err(EXIT_FAILURE, "epoll_wait failed");
        } else if (n == 0 && dim_timeout > 0) {
            dim_timeout = -1;
            printf("TIME TO DIM\n");
        }

        for (i = 0; i < n; ++i) {
            struct epoll_event *evt = &events[i];

            if (evt->events & EPOLLERR || evt->events & EPOLLHUP) {
                close(evt->data.fd);
            } else if (evt->data.fd == udev_fd) {
                udev_update();
                printf("POWER STATE CHANGE\n");

                switch (mode) {
                case AC_ON:
                    printf("on AC power: BLIGHT: %f\n", state->brightness);
                    break;
                case AC_OFF:
                    printf("on battery power: BLIGHT: %f\n", state->brightness);
                    break;
                }
                backlight_set(&b, state->brightness);

                dim_timeout = state->timeout;
            } else if (dim_timeout == -1 && state->timeout != -1) {
                dim_timeout = state->timeout;
                printf("TIME TO UNDIM\n");
            }
        }
    }
}

int main(void)
{
    init_epoll();

    udev = udev_new();
    if (!udev)
        err(EXIT_FAILURE, "can't create udev");

    if (backlight_find_best(&b) < 0)
        errx(EXIT_FAILURE, "failed to get backlight info");

    udev_enumerate();
    state = &States[mode];

    switch (mode) {
    case AC_ON:
        printf("on AC power: BLIGHT: %f\n", state->brightness);
        break;
    case AC_OFF:
        printf("on battery power: BLIGHT: %f\n", state->brightness);
        break;
    }

    mon = udev_monitor_new_from_netlink(udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(mon, "power_supply", NULL);
    udev_monitor_enable_receiving(mon);

    udev_fd = udev_monitor_get_fd(mon);
    register_epoll(udev_fd, AC_BOTH);

    ev_findall();
    run();

    udev_unref(udev);
    return 0;
}

// vim: et:sts=4:sw=4:cino=(0
