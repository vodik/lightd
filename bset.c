#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <err.h>

#include "backlight.h"

static void __attribute__((__noreturn__)) usage(FILE *out)
{
    fprintf(out, "usage: %s [value]\n", program_invocation_short_name);

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    struct backlight_t b;
    char *arg = argv[1];
    long value = 0;

    if (argc != 2)
        usage(stderr);

    if (backlight_find_best(&b) < 0)
        errx(EXIT_FAILURE, "couldn't get backlight information");

    if (strcmp("max", arg) == 0)
        return backlight_set(&b, 100);

    char *end = NULL;
    value = strtol(arg, &end, 10);
    if (errno || arg == end)
        errx(EXIT_FAILURE, "invalid setting: %s", arg);
    return backlight_set(&b, value);
}

// vim: et:sts=4:sw=4:cino=(0
