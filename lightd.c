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
#include <errno.h>
#include <err.h>

#include <libudev.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <linux/input.h>

#include "backlight.h"

enum power_state {
    AC_START = -1,
    AC_ON,
    AC_OFF,
    AC_BOTH
};

struct power_state_t {
    int epoll_fd;
    int timer_fd;
    struct timespec timeout;
    double brightness;
    double dim;
};

struct fd_data_t {
    int fd;
    char *devnode;
    struct fd_data_t *next;
    struct fd_data_t *prev;
};

static enum power_state power_mode = AC_START;
static struct power_state_t States[] = {
    [AC_ON] = {
        .brightness = 100
    },
    [AC_OFF] = {
        .timeout.tv_sec = 10,
        .brightness = 35,
        .dim = 10
    }
}, *state = NULL;

static bool dimmer = false;
static struct fd_data_t *head = NULL;
static struct backlight_t b;

static struct udev *udev;
static struct udev_monitor *power_mon, *input_mon;
static int power_mon_fd, input_mon_fd;

static void backlight_dim(struct backlight_t *b, double dim)
{
    state->brightness = backlight_get(b);
    backlight_set(b, clamp(state->brightness - dim, 1.5, 100));
}

static void register_device(const char *devnode, int fd)
{
    struct fd_data_t *node = malloc(sizeof(struct fd_data_t));
    node->fd = fd;
    node->devnode = strdup(devnode);
    node->next = head;
    node->prev = NULL;

    if (head)
        head->prev = node;
    head = node;
}

static void unregister_device(const char *devnode)
{
    struct fd_data_t *node;

    for (node = head; node; node = node->next) {
        if (strcmp(node->devnode, devnode) == 0) {
            free(node->devnode);
            close(node->fd);

            if (node == head) {
                head = node->next;
                head->prev = NULL;
            } else {
                node->prev->next = node->next;
                if (node->next)
                    node->next->prev = node->prev;
            }
            free(node);
        }
    }
}

// {{{1 EPOLL
static void epoll_init(void)
{
    size_t i, len = sizeof(States) / sizeof(States[0]);

    for (i = 0; i < len; ++i) {
        States[i].epoll_fd = epoll_create1(0);
        if (States[i].epoll_fd < 0)
            err(EXIT_FAILURE, "failed to start epoll");
    }
}

static void register_epoll(int fd, enum power_state power_mode)
{
    struct epoll_event event = {
        .data.fd = fd,
        .events  = EPOLLIN | EPOLLET
    };

    if ((power_mode == AC_OFF || power_mode == AC_BOTH) &&
        (epoll_ctl(States[AC_OFF].epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0))
        err(EXIT_FAILURE, "failed to add udev monitor to epoll");

    if ((power_mode == AC_ON || power_mode == AC_BOTH) &&
        (epoll_ctl(States[AC_ON].epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0))
        err(EXIT_FAILURE, "failed to add udev monitor to epoll");
}
// }}}

// {{{1 EVDEV
static inline uint8_t bit(int bit, const uint8_t array[static (EV_MAX + 7) / 8])
{
    return array[bit / 8] & (1 << (bit % 8));
}

static int ev_open(const filepath_t devnode, const char **n)
{
    int rc = 0;
    uint8_t evtype_bitmask[(EV_MAX + 7) / 8];
    static char name[256];

    int fd = open(devnode, O_RDONLY);
    if (fd < 0)
        err(EXIT_FAILURE, "failed to open evdev device %s", devnode);

    rc = ioctl(fd, EVIOCGBIT(0, EV_MAX), evtype_bitmask);
    if (rc < 0)
        goto cleanup;

    rc  = bit(EV_KEY, evtype_bitmask);
    rc |= bit(EV_REL, evtype_bitmask);
    rc |= bit(EV_ABS, evtype_bitmask);
    if (!rc)
        goto cleanup;

    if (n) {
        *n = name;
        rc = ioctl(fd, EVIOCGNAME(sizeof(name)), name);
    }

cleanup:
    if (rc <= 0) {
        close(fd);
        return -1;
    }
    return fd;
}
// }}}

// {{{1 UDEV
static bool update_power_state(struct udev_device *dev, bool save)
{
    enum power_state next = power_mode;

    const char *online = udev_device_get_property_value(dev, "POWER_SUPPLY_ONLINE");
    if (!online)
        return false;

    switch (online[0]) {
    case '1':
        printf("Using AC power profile...\n");
        next = AC_ON;
        break;
    case '0':
        printf("Using battery profile...\n");
        next = AC_OFF;
        break;
    default:
        return false;
    }
    fflush(stdout);

    if (next != power_mode) {
        if (save)
            state->brightness = backlight_get(&b);
        state = &States[next];
        backlight_set(&b, state->brightness);
    }

    power_mode = next;
    return true;
}

static void udev_init_power(void)
{
    struct udev_list_entry *devices, *dev_list_entry;
    struct udev_enumerate *enumerate = udev_enumerate_new(udev);

    udev_enumerate_add_match_subsystem(enumerate, "power_supply");
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);

    udev_list_entry_foreach(dev_list_entry, devices) {
        const char *path = udev_list_entry_get_name(dev_list_entry);
        struct udev_device *dev = udev_device_new_from_syspath(udev, path);

        if (update_power_state(dev, false)) {
            udev_device_unref(dev);
            state = &States[power_mode];
            break;
        }

        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);

    power_mon = udev_monitor_new_from_netlink(udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(power_mon, "power_supply", NULL);
    udev_monitor_enable_receiving(power_mon);

    power_mon_fd = udev_monitor_get_fd(power_mon);
    register_epoll(power_mon_fd, AC_BOTH);
}

static void udev_adddevice(struct udev_device *dev, bool enumerating)
{
    const char *devnode = udev_device_get_devnode(dev);
    const char *action  = udev_device_get_action(dev);
    const char *name = NULL;

    /* check there's an entry in /dev/... */
    if (!devnode)
        return;

    /* if we aren't enumerating, check there's an action */
    if (!enumerating && !action)
        return;

    /* check if device has ID_INPUT */
    if (udev_device_get_property_value(dev, "ID_INPUT") == NULL)
        return;

    if (enumerating || strcmp("add", action) == 0) {
        int fd = ev_open(devnode, &name);
        if (fd < 0)
            return;

        printf("Monitoring device %s: %s\n", name, devnode);
        fflush(stdout);

        register_device(devnode, fd);
        register_epoll(fd, power_mode);
    } else if (strcmp("remove", action) == 0) {
        unregister_device(devnode);
    }
}

static void udev_init_input(void)
{
    struct udev_list_entry *devices, *dev_list_entry;
    struct udev_enumerate *enumerate = udev_enumerate_new(udev);

    udev_enumerate_add_match_subsystem(enumerate, "input");
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);

    /* TODO: This is enumerating input/js*, input/mouse*..., lame */
    udev_list_entry_foreach(dev_list_entry, devices) {
        const char *path = udev_list_entry_get_name(dev_list_entry);
        struct udev_device *dev = udev_device_new_from_syspath(udev, path);

        udev_adddevice(dev, true);
        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);

    input_mon = udev_monitor_new_from_netlink(udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(input_mon, "input", NULL);
    udev_monitor_enable_receiving(input_mon);

    input_mon_fd = udev_monitor_get_fd(input_mon);
    register_epoll(input_mon_fd, AC_BOTH);
}

static bool udev_monitor_power(bool save)
{
    bool rc = false;

    while (true) {
        struct udev_device *dev = udev_monitor_receive_device(power_mon);
        if (!dev) {
            if (errno == EAGAIN)
                break;
            err(EXIT_FAILURE, "failed to recieve power device");
        }

        rc |= update_power_state(dev, save);
        udev_device_unref(dev);
    }

    return rc;
}

static void udev_monitor_input(void)
{
    while (true) {
        struct udev_device *dev = udev_monitor_receive_device(input_mon);
        if (!dev) {
            if (errno == EAGAIN)
                break;
            err(EXIT_FAILURE, "failed to recieve input device");
        }

        udev_adddevice(dev, false);
        udev_device_unref(dev);
    }
}

static void udev_init(void)
{
    udev = udev_new();
    if (!udev)
        err(EXIT_FAILURE, "can't create udev");

    udev_init_power();
    if (dimmer)
        udev_init_input();
}
// }}}

// {{{1 TIMER
static void timer_set(struct power_state_t *state)
{
    struct itimerspec spec = {
        .it_value = state->timeout,
    };

    if (timerfd_settime(state->timer_fd, 0, &spec, NULL) < 0)
        err(EXIT_FAILURE, "failed to set timer");
}

static void timer_state_init(struct power_state_t *state)
{
    state->timer_fd = timerfd_create(CLOCK_MONOTONIC,TFD_NONBLOCK);
    if (state->timer_fd < 0)
        err(EXIT_FAILURE, "failed to create timer");

    timer_set(state);

    struct epoll_event event = {
        .data.fd = state->timer_fd,
        .events  = EPOLLIN | EPOLLET
    };

    if (epoll_ctl(state->epoll_fd, EPOLL_CTL_ADD, state->timer_fd, &event) < 0)
        err(EXIT_FAILURE, "failed to add state's time to epoll");
}

static void timer_init(void)
{
    if (!dimmer)
        return;

    if (States[AC_ON].dim)
        timer_state_init(&States[AC_ON]);

    if (States[AC_OFF].dim)
        timer_state_init(&States[AC_OFF]);
}
// }}}

static int loop()
{
    bool dimmed = false;
    struct epoll_event events[64];

    while (true) {
        int i, n = epoll_wait(state->epoll_fd, events, 64, -1);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            err(EXIT_FAILURE, "epoll_wait failed");
        }

        for (i = 0; i < n; ++i) {
            struct epoll_event *evt = &events[i];

            if (evt->events & EPOLLERR || evt->events & EPOLLHUP) {
                close(evt->data.fd);
            } else if (evt->data.fd == input_mon_fd) {
                udev_monitor_input();
            } else if (evt->data.fd == power_mon_fd) {
                bool save = state->dim == 0;
                udev_monitor_power(save);
            } else if (evt->data.fd == state->timer_fd) {
                dimmed = true;
                backlight_dim(&b, state->dim);
            } else {
                /* We don't want to undim or reset the time each time we
                 * get activity. Creates a massive cpu load */
                if (dimmed) {
                    dimmed = false;
                    backlight_set(&b, state->brightness);
                }

                timer_set(state);
            }
        }
    }

    return 0;
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
    fprintf(out, "usage: %s [options]\n", program_invocation_short_name);
    fputs("Options:\n"
        " -h, --help             display this help and exit\n"
        " -v, --version          display version\n"
        " -D, --dimmer           dim the screen when inactivity detected\n"
        " -d, --dim=VALUE        the amount to dim the screen by\n"
        " -t, --timeout=VALUE    set the timeout till the screen is dimmed\n", out);

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    static const struct option opts[] = {
        { "help",    no_argument,       0, 'h' },
        { "version", no_argument,       0, 'v' },
        { "dimmer",  no_argument,       0, 'D' },
        { "dim",     required_argument, 0, 'd' },
        { "timeout", required_argument, 0, 't' },
        { 0, 0, 0, 0 }
    };

    while (true) {
        int opt = getopt_long(argc, argv, "hvDd:t:", opts, NULL);
        if (opt == -1)
            break;

        switch (opt) {
        case 'h':
            usage(stdout);
            break;
        case 'v':
            printf("%s %s\n", program_invocation_short_name, LIGHTD_VERSION);
            return 0;
        case 'D':
            dimmer = true;
            break;
        case 'd':
            States[AC_OFF].dim = atof(optarg);
            break;
        case 't':
            States[AC_OFF].timeout.tv_sec = atoi(optarg);
            break;
        default:
            usage(stderr);
        }
    }

    /* TODO: replace with udev code */
    if (backlight_find_best(&b) < 0)
        errx(EXIT_FAILURE, "failed to get backlight info");

    epoll_init();
    udev_init();
    timer_init();

    return loop();
}

// vim: et:sts=4:sw=4:cino=(0
