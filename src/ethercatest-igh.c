/* Port of ethercatest-soem to IgH ethercat
 *
 * ethercatest-itg: test EtherCAT connection by monitoring I/O
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

#include <ecrt.h>
#include <glib.h>

#define info g_print


typedef struct TraverserData_ TraverserData;
typedef gboolean (*TraverserCallback)(TraverserData *);

typedef struct {
    ec_master_t *           master;
    ec_master_info_t        master_info;
    ec_domain_t *           domain1;
    uint8_t *               map;
    ec_domain_state_t       domain1_state;
    gint64                  scan_span;
    guint64                 iteration;
} Fieldbus;

typedef struct {
    ec_direction_t  dir;
    unsigned        n_di;
    unsigned        n_ai;
    unsigned        n_do;
    unsigned        n_ao;
    unsigned        n_dio;
    unsigned        n_aio;
} TraverseConfiguration;

struct TraverserData_ {
    Fieldbus *          fieldbus;
    TraverserCallback   callback;
    gpointer            context;

    int                 nslave;
    int                 nsync;
    int                 npdo;
    int                 nentry;

    ec_slave_info_t     slave;
    ec_sync_info_t      sync;
    ec_pdo_info_t       pdo;
    ec_pdo_entry_info_t entry;
};

typedef gboolean (*FieldbusCallback)(Fieldbus *);


static void
fieldbus_initialize(Fieldbus *fieldbus)
{
    fieldbus->master = NULL;
    fieldbus->domain1 = NULL;
    fieldbus->map = NULL;
    fieldbus->scan_span = 0;
    fieldbus->iteration = 0;
}

static void
fieldbus_finalize(Fieldbus *fieldbus)
{
    /* Nothing to do */
}

static gboolean
fieldbus_roundtrip(Fieldbus *fieldbus)
{
    gint64 start = g_get_monotonic_time();

    /* Send process data */
    ecrt_domain_queue(fieldbus->domain1);
    ecrt_master_send(fieldbus->master);

    /* Receive process data */
    do {
        ecrt_master_receive(fieldbus->master);
        ecrt_domain_process(fieldbus->domain1);
        ecrt_domain_state(fieldbus->domain1, &fieldbus->domain1_state);
        fieldbus->scan_span = g_get_monotonic_time() - start;
    } while (fieldbus->scan_span < 1000000 && fieldbus->domain1_state.wc_state != EC_WC_COMPLETE);

    return fieldbus->domain1_state.wc_state == EC_WC_COMPLETE;
}

static gboolean
fieldbus_scan(Fieldbus *fieldbus, FieldbusCallback callback)
{
    gint64 start = g_get_monotonic_time();

    /* Skip the receiving on the first iteration (nothing to receive) */
    if (fieldbus->iteration > 1) {
        /* Receive process data */
        ecrt_master_receive(fieldbus->master);
        ecrt_domain_process(fieldbus->domain1);
        ecrt_domain_state(fieldbus->domain1, &fieldbus->domain1_state);
    }

    if (callback != NULL && ! callback(fieldbus)) {
        return FALSE;
    }

    /* Send process data */
    ecrt_domain_queue(fieldbus->domain1);
    ecrt_master_send(fieldbus->master);

    fieldbus->scan_span = g_get_monotonic_time() - start;
    return fieldbus->domain1_state.wc_state != EC_WC_INCOMPLETE;
}

static gboolean
traverse_pdo(TraverserData *data)
{
    if (ecrt_master_get_pdo(data->fieldbus->master,
                            data->nslave, data->nsync,
                            data->npdo, &data->pdo) != 0) {
        info("failed to get PDO %d from sync manager %d from slave %d\n",
             data->npdo, data->nsync, data->nslave);
        return FALSE;
    }

    for (data->nentry = 0;
         data->nentry < data->pdo.n_entries;
         ++data->nentry) {
        if (ecrt_master_get_pdo_entry(data->fieldbus->master, data->nslave,
                                      data->nsync, data->npdo,
                                      data->nentry, &data->entry) != 0) {
            info("failed to get entry %d of PDO %d from sync manager %d from slave %d\n",
                 data->nentry, data->npdo, data->nsync, data->nslave);
            return FALSE;
        }
        if (! data->callback(data)) {
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
traverse_sync(TraverserData *data)
{
    if (ecrt_master_get_sync_manager(data->fieldbus->master,
                                     data->nslave, data->nsync,
                                     &data->sync) != 0) {
        info("failed to get sync manager %d from slave %d\n",
             data->nsync, data->nslave);
        return FALSE;
    }

    for (data->npdo = 0; data->npdo < data->sync.n_pdos; ++data->npdo) {
        if (! traverse_pdo(data)) {
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
traverse_slave(TraverserData *data)
{
    if (ecrt_master_get_slave(data->fieldbus->master,
                              data->nslave, &data->slave) != 0) {
        info("failed to fetch information from slave %d\n", data->nslave);
        return FALSE;
    }

    for (data->nsync = 0; data->nsync < data->slave.sync_count; ++data->nsync) {
        if (! traverse_sync(data)) {
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
fieldbus_traverse_pdo_entries(Fieldbus *fieldbus,
                              TraverserCallback callback, gpointer context)
{
    TraverserData data;
    data.fieldbus = fieldbus;
    data.callback = callback;
    data.context  = context;
    for (data.nslave = 0; data.nslave < fieldbus->master_info.slave_count; ++data.nslave) {
        if (! traverse_slave(&data)) {
            return FALSE;
        }
    }

    return TRUE;
}

static ec_slave_config_t *
traverser_get_slave_config(TraverserData *data)
{
    ec_slave_config_t *sc;
    sc = ecrt_master_slave_config(data->fieldbus->master, 0, data->nslave,
                                  data->slave.vendor_id, data->slave.product_code);
    if (sc == NULL) {
        info("unable to configure slave %d\n", data->nslave);
    }
    return sc;
}

static void
dump_configuration(TraverseConfiguration *configuration)
{
    const gchar *prefix = "";

    if (configuration->n_di > 0) {
        info("%s%uDI", prefix, configuration->n_di);
        prefix = "+";
        configuration->n_di = 0;
    }
    if (configuration->n_do > 0) {
        info("%s%uDO", prefix, configuration->n_do);
        prefix = "+";
        configuration->n_do = 0;
    }
    if (configuration->n_dio > 0) {
        info("%s%uDIO", prefix, configuration->n_dio);
        prefix = "+";
        configuration->n_dio = 0;
    }
    if (configuration->n_ai > 0) {
        info("%s%uAI", prefix, configuration->n_ai);
        prefix = "+";
        configuration->n_ai = 0;
    }
    if (configuration->n_ao > 0) {
        info("%s%uAI", prefix, configuration->n_ao);
        prefix = "+";
        configuration->n_ao = 0;
    }
    if (configuration->n_aio > 0) {
        info("%s%uAIO", prefix, configuration->n_aio);
        prefix = "+";
        configuration->n_aio = 0;
    }
}

static gboolean
traverser_configurer(TraverserData *data)
{
    TraverseConfiguration *configuration = data->context;
    ec_slave_config_t *sc;
    int bytepos;
    unsigned bitpos;

    if (configuration->dir != data->sync.dir) {
        return TRUE;
    }

    sc = traverser_get_slave_config(data);
    bytepos = ecrt_slave_config_reg_pdo_entry(sc, data->entry.index, data->entry.subindex,
                                              data->fieldbus->domain1, &bitpos);
    if (bytepos < 0) {
        info("failed to register entry %d on PDO %d on sync manager %d on slave %d\n",
             data->nentry, data->npdo, data->nsync, data->nslave);
        return FALSE;
    }

    /* Update counters */
    switch (data->sync.dir) {

    case EC_DIR_OUTPUT:
        if (data->entry.bit_length <= 1) {
            ++configuration->n_do;
        } else {
            ++configuration->n_ao;
        }
        break;

    case EC_DIR_INPUT:
        if (data->entry.bit_length <= 1) {
            ++configuration->n_di;
        } else {
            ++configuration->n_ai;
        }
        break;

    case EC_DIR_BOTH:
        if (data->entry.bit_length <= 1) {
            ++configuration->n_dio;
        } else {
            ++configuration->n_aio;
        }
        break;

    default:
        break;
    }

    return TRUE;
}

static gboolean
fieldbus_autoconfigure(Fieldbus *fieldbus)
{
    TraverseConfiguration configuration = {
        .n_do = 0,
        .n_ao = 0,
        .n_di = 0,
        .n_ai = 0,
        .n_dio = 0,
        .n_aio = 0,
    };

    /* Fill process data with outputs, inputs and inputoutputs in
     * sequential order, similarily to what done by SOEM legacy */
    configuration.dir = EC_DIR_OUTPUT;
    if (! fieldbus_traverse_pdo_entries(fieldbus, traverser_configurer, &configuration)) {
        return FALSE;
    }

    configuration.dir = EC_DIR_INPUT;
    if (! fieldbus_traverse_pdo_entries(fieldbus, traverser_configurer, &configuration)) {
        return FALSE;
    }

    configuration.dir = EC_DIR_BOTH;
    if (! fieldbus_traverse_pdo_entries(fieldbus, traverser_configurer, &configuration)) {
        return FALSE;
    }

    dump_configuration(&configuration);
    return TRUE;
}

static gboolean
fieldbus_start(Fieldbus *fieldbus)
{
    struct sched_param param;
    ec_master_state_t master;

    if (fieldbus->master != NULL) {
        /* Fieldbus already configured: just bail out */
        return TRUE;
    }

    info("Allocating master resources... ");
    fieldbus->master = ecrt_request_master(0);
    if (fieldbus->master == NULL) {
        return FALSE;
    }
    if (ecrt_master(fieldbus->master, &fieldbus->master_info) != 0) {
        return FALSE;
    }
    info("done\n");

    info("Creating domain... ");
    fieldbus->domain1 = ecrt_master_create_domain(fieldbus->master);
    if (fieldbus->domain1 == NULL) {
        info("failed\n");
        return FALSE;
    }
    info("done\n");

    info("Autoconfiguring slaves... ");
    if (! fieldbus_autoconfigure(fieldbus)) {
        return FALSE;
    }
    info("\n");

    info("Activating configuration... ");
    if (ecrt_master_activate(fieldbus->master) != 0) {
        info("failed\n");
        return FALSE;
    }
    info("done\n");

    info("Get domain process data... ");
    fieldbus->map = ecrt_domain_data(fieldbus->domain1);
    if (fieldbus->map == NULL) {
        info("failed\n");
        return FALSE;
    }
    info("done\n");

    info("Setting priority... ");
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        info("failed\n");
        return FALSE;
    }
    info("set to %i\n", param.sched_priority);

    info("Waiting all slaves in PREOP+OP state... ");
    for (int n = 0; n < 5000; ++n) {
        ecrt_master_send(fieldbus->master);
        ecrt_master_receive(fieldbus->master);
        ecrt_master_state(fieldbus->master, &master);
        if (master.al_states == (EC_AL_STATE_PREOP | EC_AL_STATE_OP)) {
            break;
        }
        g_usleep(100);
    }
    if (master.al_states != (EC_AL_STATE_PREOP | EC_AL_STATE_OP)) {
        info("failed\n");
        return FALSE;
    }
    info("done\n");

    info("Do a roundtrip to fill the process data... ");
    if (! fieldbus_roundtrip(fieldbus)) {
        info("failed\n");
        return FALSE;
    }
    info("done\n");

    return TRUE;
}

static void
fieldbus_stop(Fieldbus *fieldbus)
{
    if (fieldbus->master != NULL) {
        ecrt_release_master(fieldbus->master);
        fieldbus->master = NULL;
    }
}

static gboolean
fieldbus_dump(Fieldbus *fieldbus)
{
    int wkc = fieldbus->domain1_state.working_counter;
    int expected_wkc = fieldbus->master_info.slave_count;
    int i;

    info("Iteration %" G_GUINT64_FORMAT ":  %" G_GINT64_FORMAT " usec WKC %d",
         fieldbus->iteration, fieldbus->scan_span, wkc);
    if (wkc != expected_wkc) {
        info(" wrong (expected %d)\n", expected_wkc);
        return FALSE;
    }

    for (i = 0; i < ecrt_domain_size(fieldbus->domain1); ++i) {
        info(" %02X", fieldbus->map[i]);
    }
    info("   \r");

    return TRUE;
}

static void
fieldbus_recover(Fieldbus *fieldbus)
{
    /* TODO */
}

static gboolean
cycle(Fieldbus *fieldbus)
{
    /* Show a digital counter that updates every 0.1 s (5000 us x 20)
     * in the first 8 digital outputs */
    fieldbus->map[0] = fieldbus->iteration / 20;
    return TRUE;
}

static void
usage(void)
{
    info("Usage: ethercatest-itg [PERIOD]\n"
         "  [PERIOD] Scantime in us (0 for roundtrip performances)\n");
}

int main(int argc, char **argv)
{
    Fieldbus fieldbus;
    gulong period;

    fieldbus_initialize(&fieldbus);

    /* Parse arguments */
    if (argc == 1) {
        period = 5000;
    } else if (argc > 2) {
        info("Too many arguments.\n");
        usage();
        return 0;
    } else if (g_strcmp0(argv[1], "-h") == 0 || g_strcmp0(argv[1], "--help") == 0) {
        usage();
        return 0;
    } else {
            period = atoi(argv[1]);
    }

    if (fieldbus_start(&fieldbus)) {
        gint64 min_span = 0;
        gint64 max_span = 0;
        if (period == 0) {
            while (++fieldbus.iteration < 50000) {
                if (! fieldbus_roundtrip(&fieldbus) ||
                    ! fieldbus_dump(&fieldbus)) {
                    fieldbus_recover(&fieldbus);
                } else if (max_span == 0) {
                    min_span = max_span = fieldbus.scan_span;
                } else if (fieldbus.scan_span < min_span) {
                    min_span = fieldbus.scan_span;
                } else if (fieldbus.scan_span > max_span) {
                    max_span = fieldbus.scan_span;
                }
            }
            info("\nRoundtrip time (usec): min %" G_GINT64_FORMAT
                 "  max %" G_GINT64_FORMAT "\n", min_span, max_span);
        } else {
            while (++fieldbus.iteration < 10000) {
                if (! fieldbus_scan(&fieldbus, cycle) ||
                    ! fieldbus_dump(&fieldbus)) {
                    fieldbus_recover(&fieldbus);
                } else if (max_span == 0) {
                    min_span = max_span = fieldbus.scan_span;
                } else if (fieldbus.scan_span < min_span) {
                    min_span = fieldbus.scan_span;
                } else if (fieldbus.scan_span > max_span) {
                    max_span = fieldbus.scan_span;
                }
                /* Wait for the next scan */
                if (fieldbus.scan_span > period) {
                    info("\nScan too low (%" G_GINT64_FORMAT " usec)\n",
                         fieldbus.scan_span);
                } else {
                    g_usleep(period - fieldbus.scan_span);
                }
            }
            info("\nTime span of scans (usec): min %" G_GINT64_FORMAT
                 "  max %" G_GINT64_FORMAT "\n", min_span, max_span);
        }
        fieldbus_stop(&fieldbus);
    }

    fieldbus_finalize(&fieldbus);
    return 0;
}
