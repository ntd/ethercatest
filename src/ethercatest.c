/* Adapted from SOEM simple_test.c */

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
    gchar *         interface;
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
} IO;


static gchar *
get_valid_interface(void)
{
    gchar *interface;
    struct ifaddrs *ifaces;

    interface = NULL;
    if (getifaddrs(&ifaces) != 0) {
        info("getifaddrs() failed\n");
    } else if (ifaces == NULL) {
        info("No interfaces found\n");
    } else {
        struct ifaddrs *iface;
        /* Find the first valid interface */
        for (iface = ifaces; iface != NULL; iface = iface->ifa_next) {
            if ((iface->ifa_flags & MUST_BE_ON) == MUST_BE_ON &&
                (iface->ifa_flags & MUST_BE_OFF) == 0) {
                /* A valid interface has been found */
                break;
            }
        }
        if (iface == NULL) {
            info("No valid interfaces found\n");
        } else {
            interface = g_strdup(iface->ifa_name);
        }
        freeifaddrs(ifaces);
    }

    return interface;
}

static void
io_initialize(IO *io)
{
    ecx_contextt *context;

    io->interface = NULL;
    io->group = 0;
    io->roundtrip_time = 0;
    io->ecaterror = FALSE;

    /* Initialize the ecx_contextt data structure */
    context = &io->context;

    /* Let's start by 0-filling it, just in case further fields
     * will be appended in the future */
    memset(context, 0, sizeof(*context));

    context->port = &io->port;
    context->slavelist = io->slavelist;
    context->slavecount = &io->slavecount;
    context->maxslave = EC_MAXSLAVE;
    context->grouplist = io->grouplist;
    context->maxgroup = EC_MAXGROUP;
    context->esibuf = io->esibuf;
    context->esimap = io->esimap;
    context->esislave = 0;
    context->elist = &io->elist;
    context->idxstack = &io->idxstack;
    context->ecaterror = &io->ecaterror;
    context->DCtime = &io->DCtime;
    context->SMcommtype = io->SMcommtype;
    context->PDOassign = io->PDOassign;
    context->PDOdesc = io->PDOdesc;
    context->eepSM = &io->eepSM;
    context->eepFMMU = &io->eepFMMU;
    context->FOEhook = NULL;
    context->EOEhook = NULL;
    context->manualstatechange = 0;
}

static void
io_finalize(IO *io)
{
    if (io->interface != NULL) {
        g_free(io->interface);
        io->interface = NULL;
    }
}

static int
io_roundtrip(IO *io)
{
    gint64 start;
    ecx_contextt *context;
    int wkc;

    context = &io->context;

    start = g_get_monotonic_time();
    ecx_send_processdata(context);
    wkc = ecx_receive_processdata(context, EC_TIMEOUTRET);
    io->roundtrip_time = g_get_monotonic_time() - start;

    return wkc;
}

static gboolean
io_start(IO *io)
{
    ecx_contextt *context;
    ec_groupt *grp;
    ec_slavet *slave;
    int i;

    if (io->interface == NULL) {
        /* IO not configured: just bail out */
        return FALSE;
    }

    context = &io->context;
    grp = io->grouplist + io->group;

    info("Initializing SOEM on '%s'... ", io->interface);
    if (! ecx_init(context, io->interface)) {
        info("no socket connection\n");
        return FALSE;
    }
    info("done\n");

    info("Finding autoconfig slaves... ");
    if (ecx_config_init(context, FALSE) <= 0) {
        info("no slaves found\n");
        return FALSE;
    }
    info("%d slaves found\n", io->slavecount);

    info("Sequential mapping of I/O... ");
    ecx_config_map_group(context, io->map, io->group);
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
    io_roundtrip(io);

    info("Setting operational state..");
    /* Act on slave 0 (a virtual slave used for broadcasting) */
    slave = io->slavelist;
    slave->state = EC_STATE_OPERATIONAL;
    ecx_writestate(context, 0);
    /* Poll the result ten times before giving up */
    for (i = 0; i < 10; ++i) {
        info(".");
        io_roundtrip(io);
        ecx_statecheck(context, 0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE / 10);
        if (slave->state == EC_STATE_OPERATIONAL) {
            info(" all slaves are now operational\n");
            return TRUE;
        }
    }

    info(" failed");
    ecx_readstate(context);
    for (i = 1; i <= io->slavecount; ++i) {
        slave = io->slavelist + i;
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
io_stop(IO *io)
{
    ecx_contextt *context;
    ec_slavet *slave;

    context = &io->context;
    /* Act on slave 0 (a virtual slave used for broadcasting) */
    slave = io->slavelist;

    info("Requesting init state on all slaves... ");
    slave->state = EC_STATE_INIT;
    ecx_writestate(context, 0);
    info("done\n");

    info("Close socket... ");
    ecx_close(context);
    info("done\n");
}

static gboolean
io_dump(IO *io)
{
    ec_groupt *grp;
    int n, wkc, expected_wkc;

    grp = io->grouplist + io->group;

    wkc = io_roundtrip(io);
    expected_wkc = grp->outputsWKC * 2 + grp->inputsWKC;
    info("%" G_GINT64_FORMAT " usec  WKC %d", io->roundtrip_time, wkc);
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
    info("  T: %" G_GINT64_FORMAT "\r", io->DCtime);
    return TRUE;
}

static void
io_check_state(IO *io)
{
    ecx_contextt *context;
    ec_groupt *grp;
    ec_slavet *slave;
    int i;

    context = &io->context;
    grp = context->grouplist + io->group;
    grp->docheckstate = FALSE;
    ecx_readstate(context);
    for (i = 1; i <= io->slavecount; ++i) {
        slave = context->slavelist + i;
        if (slave->group != io->group) {
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
parse_args(IO *io, int argc, char *argv[])
{
    if (argc == 1) {
        io->interface = get_valid_interface();
    } else if (argc == 3) {
        io->interface = g_strdup(argv[1]);
        io->group = (uint8) atoi(argv[2]);
    } else if (argc == 2) {
        if (g_strcmp0(argv[1], "-h") == 0 || g_strcmp0(argv[1], "--help") == 0) {
            usage();
        } else if (all_digits(argv[1])) {
            /* There is one number argument only */
            io->interface = get_valid_interface();
            io->group = (uint8) atoi(argv[1]);
        } else {
            /* There is one string argument only */
            io->interface = g_strdup(argv[1]);
        }
    } else {
        info("Invalid arguments.\n");
        usage();
    }
}

int
main(int argc, char *argv[])
{
    IO io;

    io_initialize(&io);
    parse_args(&io, argc, argv);

    if (io_start(&io)) {
        gint64 min, max;
        int i;
        min = 0;
        max = 0;
        for (i = 0; i < 10000; ++i) {
            /* Write some outputs, just for fun */
            io.map[0] = i / 20;
            info("Iteration %d:  ", i);
            if (! io_dump(&io)) {
                io_check_state(&io);
            } else if (i == 0) {
                min = max = io.roundtrip_time;
            } else if (io.roundtrip_time < min) {
                min = io.roundtrip_time;
            } else if (io.roundtrip_time > max) {
                max = io.roundtrip_time;
            }
            g_usleep(5000);
        }
        info("\nRoundtrip time (usec): min %" G_GINT64_FORMAT
             "  max %" G_GINT64_FORMAT "\n", min, max);
        io_stop(&io);
    }

    io_finalize(&io);
    return 0;
}
