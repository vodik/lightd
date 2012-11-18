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
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <err.h>

#include "backlight.h"

enum action {
    ACTION_SET,
    ACTION_INC,
    ACTION_DEC,
    ACTION_INVALID
};

static void __attribute__((__noreturn__)) usage(FILE *out)
{
    fprintf(out, "usage: %s [options] [value]\n", program_invocation_short_name);
    fputs("Options:\n"
        " -h, --help             display this help and exit\n"
        " -v, --version          display version\n"
        " -i, --inc              increment the backlight\n"
        " -d, --dec              decrement the backlight\n", out);

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    enum action action = ACTION_SET;
    struct backlight_t b;
    char *arg;
    long value = 0;
    double current;

    static const struct option opts[] = {
        { "help",    no_argument, 0, 'h' },
        { "version", no_argument, 0, 'v' },
        { "inc",     no_argument, 0, 'i' },
        { "dec",     no_argument, 0, 'd' },
        { 0, 0, 0, 0 }
    };

    while (true) {
        int opt = getopt_long(argc, argv, "hvid", opts, NULL);
        if (opt == -1)
            break;

        switch (opt) {
        case 'h':
            usage(stdout);
            break;
        case 'v':
            printf("%s %s\n", program_invocation_short_name, DIMMER_VERSION);
            return 0;
        case 'i':
            action = ACTION_INC;
            break;
        case 'd':
            action = ACTION_DEC;
            break;
        default:
            usage(stderr);
        }
    }

    if (backlight_find_best(&b) < 0)
        errx(EXIT_FAILURE, "couldn't get backlight information");
    current = backlight_get(&b);

    if (optind == argc) {
        printf("%.1f%%\n", backlight_get(&b));
        return 0;
    }

    arg = argv[optind++];
    if (action == ACTION_SET && strcmp("max", arg) == 0)
        value = 100;
    else {
        char *end = NULL;
        value = strtol(arg, &end, 10);
        if (errno || arg == end)
            errx(EXIT_FAILURE, "invalid setting: %s", arg);
    }

    switch (action) {
    case ACTION_SET:
        return backlight_set(&b, value);
    case ACTION_INC:
        return backlight_set(&b, current + value);
    case ACTION_DEC:
        return backlight_set(&b, current - value);
    case ACTION_INVALID:
        break;
    }

    return -1;
}

// vim: et:sts=4:sw=4:cino=(0
