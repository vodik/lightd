#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <errno.h>
#include <err.h>

#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <linux/input.h>

#define CLAMP(x, low, high) \
    __extension__ ({ \
        typeof(x) _x = (x); \
        typeof(low) _low = (low); \
        typeof(high) _high = (high); \
        ((_x > _high) ? _high : ((_x < _low) ? _low : _x)); \
    })

#define BIT(_bit, array) \
    __extension__ ({ \
        typeof(_bit) bit = (_bit); \
        (array)[bit / 8] & (1 << (bit % 8)); \
    })

typedef char filepath_t[PATH_MAX];

struct backlight_t {
    long max, cur;
    filepath_t dev;
};

static int epoll_fd;
static struct backlight_t b;

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

    *value = strtol(buf, &end, 10);
    if (errno || buf == end)
        errx(EXIT_FAILURE, "not a number: %s", buf);

    close(fd);
    return 0;
}

static int set(const char *path, long value)
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

static int get_backlight_info(struct backlight_t *b, int id)
{
    filepath_t path;

    snprintf(path, PATH_MAX, "/sys/class/backlight/acpi_video%d/max_brightness", id);
    if (get(path, &b->max) < 0)
        return -1;

    snprintf(b->dev, PATH_MAX, "/sys/class/backlight/acpi_video%d/brightness", id);
    if (get(b->dev, &b->cur) < 0)
        return -1;

    return 0;
}

static int ev_adddevice(filepath_t path)
{
    int rc = 0;
    char name[256] = "Unknown";
    uint8_t evtype_bitmask[(EV_MAX + 7) / 8];

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        err(EXIT_FAILURE, "failed to open %s", path);

    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0 ||
        ioctl(fd, EVIOCGBIT(0, EV_MAX), evtype_bitmask) < 0) {
        close(fd);
        return 1;
    }

    rc |= BIT(EV_KEY, evtype_bitmask);
    rc |= BIT(EV_REL, evtype_bitmask);
    rc |= BIT(EV_ABS, evtype_bitmask);

    if (rc) {
        struct epoll_event event = {
            .data.fd = fd,
            .events  = EPOLLIN | EPOLLET
        };

        printf("adding device %s\n", name);
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0)
            err(EXIT_FAILURE, "failed to add to epoll");
    } else
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

static void sighandler(int signum)
{
    switch (signum) {
    case SIGINT:
    case SIGTERM:
        set(b.dev, b.cur);
        exit(EXIT_SUCCESS);
    }
}

static int run(int timeout, int dim)
{
    struct epoll_event events[64];
    int dim_timeout = timeout;

    while (true) {
        int i, n = epoll_wait(epoll_fd, events, 64, dim_timeout);

        if (n == 0 && dim_timeout > 0) {
            dim_timeout = -1;
            printf("TIMEOUT: inactivity\n");
            set(b.dev, CLAMP(b.cur - dim, 0, b.max));
        } else if (dim_timeout == -1) {
            dim_timeout = timeout;
            printf("EVENT: activity\n");
            set(b.dev, CLAMP(b.cur, 0, b.max));
        }

        for (i = 0; i < n; ++i) {
            struct epoll_event *evt = &events[i];

            if (evt->events & EPOLLERR ||
                evt->events & EPOLLHUP ||
                !(evt->events & EPOLLIN)) {
                fprintf(stderr, "epoll error\n");
                close(evt->data.fd);
                continue;
            } else {
                lseek(evt->data.fd, 0, SEEK_END);
            }
        }
    }

    return 0;
}

int main(void)
{
    int rc = 0;

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
        err(EXIT_FAILURE, "failed to start epoll");

    if (get_backlight_info(&b, 0) < 0)
        errx(EXIT_FAILURE, "failed to get backlight info");

    signal(SIGTERM, sighandler);
    signal(SIGINT,  sighandler);

    ev_findall();
    rc = run(7000, 2);

    close(epoll_fd);
    return rc;
}

// vim: et:sts=4:sw=4:cino=(0
