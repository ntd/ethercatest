/* Code shared by both ethercatest programs.
 *
 * Copyright (C) 2021, 2025  Fontana Nicola <ntd at entidi.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "ethercatest.h"
#include <ifaddrs.h>
#include <inttypes.h>
#include <net/if.h>
#include <alloca.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>


int64_t
get_monotonic_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (((int64_t) ts.tv_sec) * 1000000) + (ts.tv_nsec / 1000);
}

void
wait_next_iteration(int64_t iteration_time, int64_t period)
{
    if (period == 0) {
    } else if (iteration_time > period) {
        info("\n Iteration time overflow (%" PRId64 " usec)\n", iteration_time);
    } else {
        usleep(period - iteration_time);
    }
}

static int
is_wireless(const char *iface)
{
    const char *prefix = "/sys/class/net/";
    const char *suffix = "/wireless";
    size_t prefix_len = strlen(prefix);
    size_t iface_len = strlen(iface);
    size_t suffix_len = strlen(suffix);

    char *path = alloca(prefix_len + iface_len + suffix_len + 1);
    strcpy(path, prefix);
    strcpy(path + prefix_len, iface);
    strcpy(path + prefix_len + iface_len, "/wireless");
    return access(path, F_OK) == 0;
}

const char *
get_default_interface(void)
{
    /* A bit dirty to use a static pointer to the heap...
     * but these are test programs: no need to get picky */
    static char *iface = NULL;
    struct ifaddrs *list, *item;
    int family, s;

    /* Just to avoid leaks in subsequent calls */
    if (iface != NULL) {
        free(iface);
    }

    if (getifaddrs(&list) < 0) {
        return NULL;
    }

    for (item = list; item != NULL; item = item->ifa_next) {
        /* A valid interface must satisfy all following requirements */
        if (item->ifa_addr != NULL &&
            item->ifa_addr->sa_family == AF_PACKET &&
            (item->ifa_flags & IFF_LOOPBACK) == 0 &&
            (item->ifa_flags & IFF_UP) > 0 &&
            ! is_wireless(item->ifa_name)
        ) {
            iface = strdup(item->ifa_name);
            break;
        }
    }

    freeifaddrs(list);
    return iface;
}
