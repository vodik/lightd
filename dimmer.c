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
#include <syslog.h>

#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <linux/input.h>

#include "backlight.h"

#define BIT(_bit, array) \
    __extension__ ({ \
        typeof(_bit) bit = (_bit); \
        (array)[bit / 8] & (1 << (bit % 8)); \
    })

static double blight;
static int epoll_fd, inotify_fd;
static struct backlight_t b;

static int xstrtol(const char *str, long *out)
{
    char *end = NULL;

    if (str == NULL || *str == '\0')
        return -1;
    errno = 0;

    *out = strtol(str, &end, 10);
    if (errno || str == end)
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

        syslog(LOG_INFO, "monitoring device %s: %s\n", name, path);
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

static void sighandler(int signum)
{
    switch (signum) {
    case SIGINT:
    case SIGTERM:
        backlight_set(&b, blight);
        exit(EXIT_SUCCESS);
    }
}

static void backlight_dim(struct backlight_t *b, double dim)
{
    double v = backlight_get(b);
    printf("read v at %f\n", v);

    if (dim) {
        printf("dimming to %f\n", v - dim);
        backlight_set(b, CLAMP(v - dim, 1.5, 100));
    } else if (blight > v) {
        printf("undimming to %f\n", blight);
        backlight_set(b, blight);
    }

    blight = v;
}

static int run(int timeout, double dim)
{
    struct epoll_event events[64];
    int dim_timeout = timeout;

    while (true) {
        int i, n = epoll_wait(epoll_fd, events, 64, dim_timeout);

        if (n == 0 && dim_timeout > 0) {
            dim_timeout = -1;
            backlight_dim(&b, dim);
        }

        for (i = 0; i < n; ++i) {
            struct epoll_event *evt = &events[i];

            if (evt->events & EPOLLERR || evt->events & EPOLLHUP) {
                close(evt->data.fd);
                continue;
            } else if (evt->data.fd == inotify_fd) {
                inotify_read();
            } else {
                if (dim_timeout == -1) {
                    dim_timeout = timeout;
                    backlight_dim(&b, 0);
                }

                lseek(evt->data.fd, 0, SEEK_END);
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
        " -t, --timeout=TIME     the timeout in seconds to dim after\n"
        " -d, --dim-by=LEVEL     how much to dim by\n", out);

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    long timeout = 10, dim = 2;
    int rc = 0;

    static const struct option opts[] = {
        { "help",    no_argument,       0, 'h' },
        { "version", no_argument,       0, 'v' },
        { "timeout", required_argument, 0, 't' },
        { "dim-by",  required_argument, 0, 'd' },
        { 0, 0, 0, 0 }
    };

    while (true) {
        int opt = getopt_long(argc, argv, "hvt:d:", opts, NULL);
        if (opt == -1)
            break;

        switch (opt) {
        case 'h':
            usage(stdout);
            break;
        case 'v':
            printf("%s %s\n", program_invocation_short_name, DIMMER_VERSION);
            return 0;
        case 't':
            if (xstrtol(optarg, &timeout) < 0)
                errx(EXIT_FAILURE, "invalid timeout: %s", optarg);
            break;
        case 'd':
            if (xstrtol(optarg, &dim) < 0)
                errx(EXIT_FAILURE, "invalid dim: %s", optarg);
            break;
        default:
            usage(stderr);
        }
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
        err(EXIT_FAILURE, "failed to start epoll");

    inotify_fd = inotify_init();
    if (inotify_fd < 0)
        err(EXIT_FAILURE, "failed to start inotify");

    /* if (backlight_find_best(&b) < 0) */
    if (backlight_init(&b, "acpi_video0") < 0)
        errx(EXIT_FAILURE, "failed to get backlight info");

    signal(SIGTERM, sighandler);
    signal(SIGINT,  sighandler);

    int wd = inotify_add_watch(inotify_fd, "/dev/input", IN_CREATE);
    if (wd < 0)
        err(EXIT_FAILURE, "failed to watch /dev/input");

    struct epoll_event event = {
        .data.fd = inotify_fd,
        .events  = EPOLLIN | EPOLLET
    };

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, inotify_fd, &event) < 0)
        err(EXIT_FAILURE, "failed to add inotify to epoll");

    ev_findall();
    rc = run(timeout * 1000, dim);

    close(epoll_fd);
    return rc;
}

// vim: et:sts=4:sw=4:cino=(0
