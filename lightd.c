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

struct power_state_t {
    int epoll_fd;
    int timeout;
    double brightness;
    double dim;
};

static struct power_state_t States[2] = {
    [AC_ON] = {
        .timeout = -1,
        .brightness = 100
    },
    [AC_OFF] = {
        .timeout = 2 * 1000,
        .brightness = 35,
        .dim = 20
    }
}, *state = NULL;

#define AC_BOTH -1

static enum power mode = AC_ON;

static int udev_fd, inotify_fd;

static struct udev *udev;
static struct udev_monitor *mon;

static struct backlight_t b;

static void backlight_dim(struct backlight_t *b, double dim)
{
    state->brightness = backlight_get(b);
    backlight_set(b, CLAMP(state->brightness - dim, 1.5, 100));
}

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

static bool power_status(struct udev_device *dev, bool save)
{
    enum power next = mode;

    const char *online = udev_device_get_property_value(dev, "POWER_SUPPLY_ONLINE");
    if (!online)
        return false;

    if (strcmp("1", online) == 0)
        next = AC_ON;
    else if (strcmp("0", online) == 0)
        next = AC_OFF;

    if (next != mode) {
        if (save)
            state->brightness = backlight_get(&b);
        state = &States[next];
        backlight_set(&b, state->brightness);
    }

    mode = next;
    return true;
}

// {{{1 UDEV MAGIC
static void udev_enumerate(void)
{
    struct udev_list_entry *devices, *dev_list_entry;
    struct udev_enumerate *enumerate = udev_enumerate_new(udev);

    udev_enumerate_add_match_subsystem(enumerate, "power_supply");
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);

    udev_list_entry_foreach(dev_list_entry, devices) {
        const char *path = udev_list_entry_get_name(dev_list_entry);
        struct udev_device *dev = udev_device_new_from_syspath(udev, path);

        if (power_status(dev, false)) {
            udev_device_unref(dev);
            break;
        }

        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
}

static bool udev_update(bool save)
{
    struct udev_device *dev = udev_monitor_receive_device(mon);
    bool rc = power_status(dev, save);

    udev_device_unref(dev);
    return rc;
}

static void ev_findall(void);

static void udev_init(void)
{
    udev = udev_new();
    if (!udev)
        err(EXIT_FAILURE, "can't create udev");

    /* TODO: replace with udev code */
    ev_findall();

    udev_enumerate();
    state = &States[mode];

    mon = udev_monitor_new_from_netlink(udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(mon, "power_supply", NULL);
    udev_monitor_enable_receiving(mon);

    udev_fd = udev_monitor_get_fd(mon);
    register_epoll(udev_fd, AC_BOTH);
}
// }}}

// {{{1 EVDEV INPUT
static uint8_t bit(int bit, const uint8_t array[static (EV_MAX + 7) / 8])
{
    return array[bit / 8] & (1 << (bit % 8));
}

static int ev_adddevice(filepath_t path)
{
    int rc = 0;
    char name[256];
    uint8_t evtype_bitmask[(EV_MAX + 7) / 8];

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        err(EXIT_FAILURE, "failed to open evdev device %s", path);

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
// }}}

// {{{1 TO STRIP OUT
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

// INOTIFY GARBAGE {{{2
static void notify_init(void)
{
   inotify_fd = inotify_init();
    if (inotify_fd < 0)
        err(EXIT_FAILURE, "failed to start inotify");

    int wd = inotify_add_watch(inotify_fd, "/dev/input", IN_CREATE);
    if (wd < 0)
        err(EXIT_FAILURE, "failed to watch /dev/input");

    register_epoll(inotify_fd, -1);
}

static void inotify_read(void)
{
    filepath_t path;
    uint8_t buf[1024 * (sizeof(struct inotify_event) + 16)];
    int nread, i = 0;

    while (true) {
        nread = read(inotify_fd, buf, sizeof(buf));
        if (nread < 0) {
            if (errno != EAGAIN) {
                err(EXIT_FAILURE, "failed to read inotify data");
                break;
            }
        } else
            break;
    }

    while (i < nread) {
        struct inotify_event *event = (struct inotify_event *)&buf[i];

        if (!event->len)
            continue;

        if (event->mask & IN_CREATE) {
            snprintf(path, PATH_MAX, "/dev/input/%s", event->name);
            ev_adddevice(path);
        }

        i += sizeof(struct inotify_event) + event->len;
    }
}
// }}} }}}

static int loop()
{
    struct epoll_event events[64];
    int dim_timeout = state->timeout;

    while (true) {
        int i, n = epoll_wait(state->epoll_fd, events, 64, dim_timeout);
        bool save = state->dim == 0 || (state->dim > 0 && dim_timeout != -1);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            err(EXIT_FAILURE, "epoll_wait failed");
        } else if (n == 0 && dim_timeout > 0) {
            dim_timeout = -1;
            backlight_dim(&b, state->dim);
        }

        for (i = 0; i < n; ++i) {
            struct epoll_event *evt = &events[i];

            if (evt->events & EPOLLERR || evt->events & EPOLLHUP) {
                close(evt->data.fd);
            } else if (evt->data.fd == inotify_fd) {
                inotify_read();
            } else if (evt->data.fd == udev_fd && udev_update(save)) {
                dim_timeout = state->timeout;
            } else if (dim_timeout == -1 && state->timeout != -1) {
                dim_timeout = state->timeout;
                backlight_set(&b, state->brightness);
            }
        }
    }
}

static void sighandler(int signum)
{
    switch (signum) {
    case SIGINT:
    case SIGTERM:
        backlight_set(&b, 100);
        exit(EXIT_SUCCESS);
    }
}

int main(void)
{
    mode = -1;

    /* TODO: replace with udev code */
    if (backlight_find_best(&b) < 0)
        errx(EXIT_FAILURE, "failed to get backlight info");

    init_epoll();
    udev_init();
    notify_init();

    signal(SIGTERM, sighandler);
    signal(SIGINT,  sighandler);

    loop();

    udev_unref(udev);
    return 0;
}

// vim: et:sts=4:sw=4:cino=(0
