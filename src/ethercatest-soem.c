/* Adapted from SOEM simple_test.c
 *
 * ethercatest-soem: test EtherCAT connection by monitoring I/O
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
#include <soem/soem.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>


typedef struct {
    ecx_contextt context;
    const char *iface;
    uint8 group;
    int wkc;
    uint64_t iteration;
    int64_t iteration_time;
    uint8 map[4096];
} Fieldbus;

typedef int (*FieldbusCallback)(Fieldbus *);


static void
fieldbus_initialize(Fieldbus *fieldbus)
{
    /* Let's start by 0-filling `fieldbus` to avoid surprises */
    memset(fieldbus, 0, sizeof(*fieldbus));

    fieldbus->iface = NULL;
    fieldbus->group = 0;
    fieldbus->wkc = 0;
    fieldbus->iteration = 0;
    fieldbus->iteration_time = 0;
}

static int
fieldbus_iteration(Fieldbus *fieldbus, FieldbusCallback callback)
{
    int64_t start, stop;
    ecx_contextt *context;

    context = &fieldbus->context;

    start = get_monotonic_time();

    fieldbus->wkc = ecx_receive_processdata(context, EC_TIMEOUTRET);
    if (callback != NULL && ! callback(fieldbus)) {
        return FALSE;
    }
    if (ecx_send_processdata(context) <= 0) {
        return FALSE;
    }

    stop = get_monotonic_time();

    ++fieldbus->iteration;
    fieldbus->iteration_time = stop - start;
    return TRUE;
}

static int
fieldbus_start(Fieldbus *fieldbus)
{
    ecx_contextt *context;
    ec_groupt *grp;
    ec_slavet *slave;
    int i;

    if (fieldbus->iface == NULL) {
        /* Fieldbus not configured: just bail out */
        return FALSE;
    }

    context = &fieldbus->context;
    grp = context->grouplist + fieldbus->group;

    info("Initializing SOEM on '%s'... ", fieldbus->iface);
    if (! ecx_init(context, fieldbus->iface)) {
        info("no socket connection\n");
        return FALSE;
    }
    info("done\n");

    info("Finding autoconfig slaves... ");
    if (ecx_config_init(context) <= 0) {
        info("no slaves found\n");
        return FALSE;
    }
    info("%d slaves found\n", context->slavecount);

    info("Sequential mapping of I/O... ");
    ecx_config_map_group(context, fieldbus->map, fieldbus->group);
    info("mapped %dO+%dI bytes from %d segments",
         grp->Obytes, grp->Ibytes, grp->nsegments);
    if (grp->nsegments > 1) {
        /* Show how slaves are distributed */
        for (i = 0; i < grp->nsegments; ++i) {
            info("%s%d", i == 0 ? " (" : "+", grp->IOsegment[i]);
        }
        info(" slaves)");
    }
    info("\n");

    info("Configuring distributed clock... ");
    ecx_configdc(context);
    info("done\n");

    info("Waiting for all slaves in safe operational... ");
    ecx_statecheck(context, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);
    info("done\n");

    info("Initial process data transmission... ");
    ecx_send_processdata(context);
    info("done\n");

    info("Setting operational state..");
    /* Act on slave 0 (a virtual slave used for broadcasting) */
    slave = context->slavelist;
    slave->state = EC_STATE_OPERATIONAL;
    ecx_writestate(context, 0);
    /* Poll the result ten times before giving up */
    for (i = 0; i < 10; ++i) {
        info(".");
        fieldbus_iteration(fieldbus, NULL);
        ecx_statecheck(context, 0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE / 10);
        if (slave->state == EC_STATE_OPERATIONAL) {
            info(" all slaves are now operational\n");
            return TRUE;
        }
    }

    info(" failed,");
    ecx_readstate(context);
    for (i = 1; i <= context->slavecount; ++i) {
        slave = context->slavelist + i;
        if (slave->state != EC_STATE_OPERATIONAL) {
            info(" slave %d is 0x%04X (AL-status=0x%04X %s)",
                 i, slave->state, slave->ALstatuscode,
                 ec_ALstatuscode2string(slave->ALstatuscode));
        }
    }
    info("\n");

    return FALSE;
}

static void
fieldbus_stop(Fieldbus *fieldbus)
{
    ecx_contextt *context;
    ec_slavet *slave;

    context = &fieldbus->context;
    /* Act on slave 0 (a virtual slave used for broadcasting) */
    slave = context->slavelist;

    info("Requesting init state on all slaves... ");
    slave->state = EC_STATE_INIT;
    ecx_writestate(context, 0);
    info("done\n");

    info("Close socket... ");
    ecx_close(context);
    info("done\n");
}

static int
fieldbus_dump(Fieldbus *fieldbus)
{
    ecx_contextt *context;
    ec_groupt *grp;
    uint32 n;
    int expected_wkc;

    context = &fieldbus->context;
    grp = context->grouplist + fieldbus->group;

    expected_wkc = grp->outputsWKC * 2 + grp->inputsWKC;
    info("Iteration %" PRIu64 ":  %" PRId64 " usec  WKC %d",
         fieldbus->iteration, fieldbus->iteration_time, fieldbus->wkc);
    if (fieldbus->wkc != expected_wkc) {
        info(" wrong (expected %d)\n", expected_wkc);
        return FALSE;
    }

    info("  O:");
    for (n = 0; n < grp->Obytes; ++n) {
        info(" %02X", grp->outputs[n]);
    }
    info("  I:");
    for (n = 0; n < grp->Ibytes; ++n) {
        info(" %02X", grp->inputs[n]);
    }
    info("  T: %lld\r", (long long) context->DCtime);
    return TRUE;
}

static void
fieldbus_recover(Fieldbus *fieldbus)
{
    ecx_contextt *context;
    ec_groupt *grp;
    ec_slavet *slave;
    int i;

    context = &fieldbus->context;
    grp = context->grouplist + fieldbus->group;
    grp->docheckstate = FALSE;
    ecx_readstate(context);
    for (i = 1; i <= context->slavecount; ++i) {
        slave = context->slavelist + i;
        if (slave->group != fieldbus->group) {
            /* This slave is part of another group: do nothing */
        } else if (slave->state != EC_STATE_OPERATIONAL) {
            grp->docheckstate = TRUE;
            if (slave->state == EC_STATE_SAFE_OP + EC_STATE_ERROR) {
                info("* Slave %d is in SAFE_OP+ERROR, attempting ACK\n", i);
                slave->state = EC_STATE_SAFE_OP + EC_STATE_ACK;
                ecx_writestate(context, i);
            } else if (slave->state == EC_STATE_SAFE_OP) {
                info("* Slave %d is in SAFE_OP, change to OPERATIONAL\n", i);
                slave->state = EC_STATE_OPERATIONAL;
                ecx_writestate(context, i);
            } else if (slave->state > EC_STATE_NONE) {
                if (ecx_reconfig_slave(context, i, EC_TIMEOUTRET)) {
                    slave->islost = FALSE;
                    info("* Slave %d reconfigured\n", i);
                }
            } else if (! slave->islost) {
                ecx_statecheck(context, i, EC_STATE_OPERATIONAL, EC_TIMEOUTRET);
                if (slave->state == EC_STATE_NONE) {
                    slave->islost = TRUE;
                    info("* Slave %d lost\n", i);
                }
            }
        } else if (slave->islost) {
            if (slave->state != EC_STATE_NONE) {
                slave->islost = FALSE;
                info("* Slave %d found\n", i);
            } else if (ecx_recover_slave(context, i, EC_TIMEOUTRET)) {
                slave->islost = FALSE;
                info("* Slave %d recovered\n", i);
            }
        }
    }

    if (! grp->docheckstate) {
        info("All slaves resumed OPERATIONAL\n");
    }
}

static int
all_digits(const char *string)
{
    const char *ch;
    for (ch = string; *ch != '\0'; ++ch) {
        if (! isdigit(*ch)) {
            return FALSE;
        }
    }
    /* An empty string is not considered a valid number */
    return ch != string;
}

static int
digital_counter(Fieldbus *fieldbus)
{
    /* Show a digital counter that updates every 20 iterations
     * in the first 8 digital outputs */
    fieldbus->map[0] = fieldbus->iteration / 20;
    return TRUE;
}

static void
usage(void)
{
    info("Usage: ethercatest-soem [INTERFACE] [PERIOD]\n"
         "  [INTERFACE] Ethernet device to use (e.g. 'eth0')\n"
         "  [PERIOD]    Scantime in us (0 for roundtrip performances)\n");
}

int
main(int argc, char *argv[])
{
    Fieldbus fieldbus;
    unsigned long period;

    setbuf(stdout, NULL);

    fieldbus_initialize(&fieldbus);

    /* Parse arguments */
    period = 5000;
    if (argc == 1) {
        fieldbus.iface = get_default_interface();
    } else if (argc == 3) {
        fieldbus.iface = argv[1];
        period = atoi(argv[2]);
    } else if (argc == 2) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            usage();
        } else if (all_digits(argv[1])) {
            /* There is one number argument only */
            fieldbus.iface = get_default_interface();
            period = atoi(argv[1]);
        } else {
            /* There is one string argument only */
            fieldbus.iface = argv[1];
        }
    } else {
        info("Invalid arguments.\n");
        usage();
    }

    /* On invalid arguments, `fieldbus.iface` will be empty
     * so `fieldbus_start` will fail */
    if (fieldbus_start(&fieldbus)) {
        int64_t min_time = 0;
        int64_t max_time = 0;
        uint64_t iterations = 100000 / (period / 100 + 1);
        FieldbusCallback cycle = period > 0 ? digital_counter : NULL;
        while (++fieldbus.iteration < iterations) {
            if (! fieldbus_iteration(&fieldbus, cycle) ||
                ! fieldbus_dump(&fieldbus)) {
                fieldbus_recover(&fieldbus);
            } else if (max_time == 0) {
                min_time = max_time = fieldbus.iteration_time;
            } else if (fieldbus.iteration_time < min_time) {
                min_time = fieldbus.iteration_time;
            } else if (fieldbus.iteration_time > max_time) {
                max_time = fieldbus.iteration_time;
            }
            wait_next_iteration(fieldbus.iteration_time, period);
        }
        info("\nIteration time (usec): min %" PRId64 "  max %" PRId64 "\n",
             min_time, max_time);
        fieldbus_stop(&fieldbus);
    }

    return 0;
}
