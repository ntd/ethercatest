/* Adapted from SOEM simple_test.c
 *
 * ethercatest: test EtherCAT connection by monitoring I/O.
 * Copyright (C) 2021  Fontana Nicola <ntd at entidi.it>
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

#include <soem/ethercat.h>
#include <glib.h>
#include <soem/ethercattype.h>
#include <stdlib.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <net/if.h>

#define MUST_BE_ON  IFF_UP
#define MUST_BE_OFF (IFF_LOOPBACK | IFF_POINTOPOINT)
#define info g_print


typedef struct {
    ecx_contextt    context;
    gchar *         iface;
    uint8           group;
    gint64          roundtrip_time;

    /* Used by the context */
    uint8           map[4096];
    ecx_portt       port;
    ec_slavet       slavelist[EC_MAXSLAVE];
    int             slavecount;
    ec_groupt       grouplist[EC_MAXGROUP];
    uint8           esibuf[EC_MAXEEPBUF];
    uint32          esimap[EC_MAXEEPBITMAP];
    /* I would say this needs to be elist[EC_MAXELIST],
     * but the source code of SOEM does not agree with me */
    ec_eringt       elist;
    ec_idxstackT    idxstack;
    boolean         ecaterror;
    int64           DCtime;
    ec_SMcommtypet  SMcommtype[EC_MAX_MAPT];
    ec_PDOassignt   PDOassign[EC_MAX_MAPT];
    ec_PDOdesct     PDOdesc[EC_MAX_MAPT];
    ec_eepromSMt    eepSM;
    ec_eepromFMMUt  eepFMMU;
} Fieldbus;


static gchar *
get_valid_interface(void)
{
    gchar *iface;
    struct ifaddrs *addrs;

    iface = NULL;
    if (getifaddrs(&addrs) != 0) {
        info("getifaddrs() failed\n");
    } else if (addrs == NULL) {
        info("No interfaces found\n");
    } else {
        struct ifaddrs *addr;
        /* Find the first valid interface */
        for (addr = addrs; addr != NULL; addr = addr->ifa_next) {
            if ((addr->ifa_flags & MUST_BE_ON) == MUST_BE_ON &&
                (addr->ifa_flags & MUST_BE_OFF) == 0) {
                /* A valid interface has been found */
                break;
            }
        }
        if (addr == NULL) {
            info("No valid interfaces found\n");
        } else {
            iface = g_strdup(addr->ifa_name);
        }
        freeifaddrs(addrs);
    }

    return iface;
}

static void
fieldbus_initialize(Fieldbus *fieldbus)
{
    ecx_contextt *context;

    fieldbus->iface = NULL;
    fieldbus->group = 0;
    fieldbus->roundtrip_time = 0;
    fieldbus->ecaterror = FALSE;

    /* Initialize the ecx_contextt data structure */
    context = &fieldbus->context;

    /* Let's start by 0-filling it, just in case further fields
     * will be appended in the future */
    memset(context, 0, sizeof(*context));

    context->port = &fieldbus->port;
    context->slavelist = fieldbus->slavelist;
    context->slavecount = &fieldbus->slavecount;
    context->maxslave = EC_MAXSLAVE;
    context->grouplist = fieldbus->grouplist;
    context->maxgroup = EC_MAXGROUP;
    context->esibuf = fieldbus->esibuf;
    context->esimap = fieldbus->esimap;
    context->esislave = 0;
    context->elist = &fieldbus->elist;
    context->idxstack = &fieldbus->idxstack;
    context->ecaterror = &fieldbus->ecaterror;
    context->DCtime = &fieldbus->DCtime;
    context->SMcommtype = fieldbus->SMcommtype;
    context->PDOassign = fieldbus->PDOassign;
    context->PDOdesc = fieldbus->PDOdesc;
    context->eepSM = &fieldbus->eepSM;
    context->eepFMMU = &fieldbus->eepFMMU;
    context->FOEhook = NULL;
    context->EOEhook = NULL;
    context->manualstatechange = 0;
}

static void
fieldbus_finalize(Fieldbus *fieldbus)
{
    if (fieldbus->iface != NULL) {
        g_free(fieldbus->iface);
        fieldbus->iface = NULL;
    }
}

static int
fieldbus_roundtrip(Fieldbus *fieldbus)
{
    gint64 start;
    ecx_contextt *context;
    int wkc;

    context = &fieldbus->context;

    start = g_get_monotonic_time();
    ecx_send_processdata(context);
    wkc = ecx_receive_processdata(context, EC_TIMEOUTRET);
    fieldbus->roundtrip_time = g_get_monotonic_time() - start;

    return wkc;
}

static gboolean
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
    grp = fieldbus->grouplist + fieldbus->group;

    info("Initializing SOEM on '%s'... ", fieldbus->iface);
    if (! ecx_init(context, fieldbus->iface)) {
        info("no socket connection\n");
        return FALSE;
    }
    info("done\n");

    info("Finding autoconfig slaves... ");
    if (ecx_config_init(context, FALSE) <= 0) {
        info("no slaves found\n");
        return FALSE;
    }
    info("%d slaves found\n", fieldbus->slavecount);

    info("Sequential mapping of I/O... ");
    ecx_config_map_group(context, fieldbus->map, fieldbus->group);
    info("done\n");

    info("Configuring distributed clock... ");
    ecx_configdc(context);
    info("done\n");

    info("Waiting for all slaves in safe operational... ");
    ecx_statecheck(context, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);
    info("found %d segments", grp->nsegments);
    for (i = 0; i < grp->nsegments; ++i) {
        info("%s%d slaves", i == 0 ? " (" : ", ", grp->IOsegment[i]);
    }
    info(grp->nsegments > 0 ? ")\n" : "\n");

    /* Send one valid process data to make outputs in slaves happy */
    fieldbus_roundtrip(fieldbus);

    info("Setting operational state..");
    /* Act on slave 0 (a virtual slave used for broadcasting) */
    slave = fieldbus->slavelist;
    slave->state = EC_STATE_OPERATIONAL;
    ecx_writestate(context, 0);
    /* Poll the result ten times before giving up */
    for (i = 0; i < 10; ++i) {
        info(".");
        fieldbus_roundtrip(fieldbus);
        ecx_statecheck(context, 0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE / 10);
        if (slave->state == EC_STATE_OPERATIONAL) {
            info(" all slaves are now operational\n");
            return TRUE;
        }
    }

    info(" failed");
    ecx_readstate(context);
    for (i = 1; i <= fieldbus->slavecount; ++i) {
        slave = fieldbus->slavelist + i;
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
    slave = fieldbus->slavelist;

    info("Requesting init state on all slaves... ");
    slave->state = EC_STATE_INIT;
    ecx_writestate(context, 0);
    info("done\n");

    info("Close socket... ");
    ecx_close(context);
    info("done\n");
}

static gboolean
fieldbus_dump(Fieldbus *fieldbus)
{
    ec_groupt *grp;
    int n, wkc, expected_wkc;

    grp = fieldbus->grouplist + fieldbus->group;

    wkc = fieldbus_roundtrip(fieldbus);
    expected_wkc = grp->outputsWKC * 2 + grp->inputsWKC;
    info("%" G_GINT64_FORMAT " usec  WKC %d", fieldbus->roundtrip_time, wkc);
    if (wkc < expected_wkc) {
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
    info("  T: %" G_GINT64_FORMAT "\r", fieldbus->DCtime);
    return TRUE;
}

static void
fieldbus_check_state(Fieldbus *fieldbus)
{
    ecx_contextt *context;
    ec_groupt *grp;
    ec_slavet *slave;
    int i;

    context = &fieldbus->context;
    grp = context->grouplist + fieldbus->group;
    grp->docheckstate = FALSE;
    ecx_readstate(context);
    for (i = 1; i <= fieldbus->slavecount; ++i) {
        slave = context->slavelist + i;
        if (slave->group != fieldbus->group) {
            /* This slave is part of another group: do nothing */
        } else if (slave->state != EC_STATE_OPERATIONAL) {
            grp->docheckstate = TRUE;
            if (slave->state == EC_STATE_SAFE_OP + EC_STATE_ERROR) {
                info("* Slave %d is in SAFE_OP+ERROR, attempting ACK\n", i);
                slave->state = EC_STATE_SAFE_OP + EC_STATE_ACK;
                ecx_writestate(context, i);
            } else if(slave->state == EC_STATE_SAFE_OP) {
                info("* Slave %d is in SAFE_OP, change to OPERATIONAL\n", i);
                slave->state = EC_STATE_OPERATIONAL;
                ecx_writestate(context, i);
            } else if(slave->state > EC_STATE_NONE) {
                if (ecx_reconfig_slave(context, i, EC_TIMEOUTRET)) {
                    slave->islost = FALSE;
                    info("* Slave %d reconfigured\n", i);
                }
            } else if(! slave->islost) {
                ecx_statecheck(context, i, EC_STATE_OPERATIONAL, EC_TIMEOUTRET);
                if (slave->state == EC_STATE_NONE) {
                    slave->islost = TRUE;
                    info("* Slave %d lost\n", i);
                }
            }
        } else if (slave->islost) {
            if(slave->state != EC_STATE_NONE) {
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

static gboolean
all_digits(const gchar *string)
{
    const gchar *ch;
    for (ch = string; *ch != '\0'; ++ch) {
        if (! g_ascii_isdigit(*ch)) {
            return FALSE;
        }
    }
    /* An empty string is not considered a valid number */
    return ch != string;
}

static void
usage(void)
{
    info("Usage: ethercatest [INTERFACE] [GROUP]\n");
}

static void
parse_args(Fieldbus *fieldbus, int argc, char *argv[])
{
    if (argc == 1) {
        fieldbus->iface = get_valid_interface();
    } else if (argc == 3) {
        fieldbus->iface = g_strdup(argv[1]);
        fieldbus->group = (uint8) atoi(argv[2]);
    } else if (argc == 2) {
        if (g_strcmp0(argv[1], "-h") == 0 || g_strcmp0(argv[1], "--help") == 0) {
            usage();
        } else if (all_digits(argv[1])) {
            /* There is one number argument only */
            fieldbus->iface = get_valid_interface();
            fieldbus->group = (uint8) atoi(argv[1]);
        } else {
            /* There is one string argument only */
            fieldbus->iface = g_strdup(argv[1]);
        }
    } else {
        info("Invalid arguments.\n");
        usage();
    }
}

int
main(int argc, char *argv[])
{
    Fieldbus fieldbus;

    fieldbus_initialize(&fieldbus);
    parse_args(&fieldbus, argc, argv);

    if (fieldbus_start(&fieldbus)) {
        gint64 min, max;
        int i;
        min = 0;
        max = 0;
        for (i = 0; i < 10000; ++i) {
            /* Write some outputs, just for fun */
            fieldbus.map[0] = i / 20;
            info("Iteration %d:  ", i);
            if (! fieldbus_dump(&fieldbus)) {
                fieldbus_check_state(&fieldbus);
            } else if (i == 0) {
                min = max = fieldbus.roundtrip_time;
            } else if (fieldbus.roundtrip_time < min) {
                min = fieldbus.roundtrip_time;
            } else if (fieldbus.roundtrip_time > max) {
                max = fieldbus.roundtrip_time;
            }
            g_usleep(5000);
        }
        info("\nRoundtrip time (usec): min %" G_GINT64_FORMAT
             "  max %" G_GINT64_FORMAT "\n", min, max);
        fieldbus_stop(&fieldbus);
    }

    fieldbus_finalize(&fieldbus);
    return 0;
}
