/* Port of ethercatest-soem to IgH ethercat
 *
 * ethercatest-igh: test EtherCAT connection by monitoring I/O
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
#include "ethercatest.h"


typedef struct {
    ec_master_t *           master;
    ec_master_info_t        master_info;
    ec_domain_t *           domain1;
    uint8_t *               map;
    ec_domain_state_t       domain1_state;
    int64_t                 scan_span;
    uint64_t                iteration;
} Fieldbus;

typedef struct TraverserData_ TraverserData;
typedef int (*TraverserCallback)(TraverserData *);

struct TraverserData_ {
    Fieldbus *          fieldbus;
    TraverserCallback   callback;
    void *              context;

    int                 nslave;
    int                 nsync;
    int                 npdo;
    int                 nentry;

    ec_slave_info_t     slave;
    ec_sync_info_t      sync;
    ec_pdo_info_t       pdo;
    ec_pdo_entry_info_t entry;
};

typedef struct {
    int nslave;
    int nsync;
    int npdo;
    int nentry;
} TraverseMapping;

typedef struct {
    ec_direction_t  dir;
    const char *    prefix;
    unsigned        channels;
    int             is_digital;
} TraverseConfiguration;

typedef int (*FieldbusCallback)(Fieldbus *);


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

static int
fieldbus_roundtrip(Fieldbus *fieldbus)
{
    int64_t start = get_monotonic_time();

    /* Send process data */
    ecrt_domain_queue(fieldbus->domain1);
    ecrt_master_send(fieldbus->master);

    /* Receive process data */
    do {
        usleep(5);
        ecrt_master_receive(fieldbus->master);
        ecrt_domain_process(fieldbus->domain1);
        ecrt_domain_state(fieldbus->domain1, &fieldbus->domain1_state);
        fieldbus->scan_span = get_monotonic_time() - start;
    } while (fieldbus->scan_span < 1000000 && fieldbus->domain1_state.wc_state != EC_WC_COMPLETE);

    return fieldbus->domain1_state.wc_state == EC_WC_COMPLETE;
}

static int
fieldbus_scan(Fieldbus *fieldbus, FieldbusCallback callback)
{
    int64_t start = get_monotonic_time();

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

    fieldbus->scan_span = get_monotonic_time() - start;
    return fieldbus->domain1_state.wc_state != EC_WC_INCOMPLETE;
}

static int
traverse_pdo(TraverserData *data)
{
    if (ecrt_master_get_pdo(data->fieldbus->master,
                            data->nslave, data->nsync,
                            data->npdo, &data->pdo) != 0) {
        info("failed to get PDO %d from sync manager %d from slave %d\n",
             data->npdo, data->nsync, data->nslave);
        return FALSE;
    }

    for (data->nentry = 0; data->nentry < data->pdo.n_entries; ++data->nentry) {
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

static int
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

static int
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

static int
fieldbus_traverse_pdo_entries(Fieldbus *fieldbus,
                              TraverserCallback callback, void *context)
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

static int
traverser_mapper(TraverserData *data)
{
    TraverseMapping *mapping = data->context;

    if (data->nslave != mapping->nslave) {
        /* Mapping new slave */
        mapping->nslave = data->nslave;
        mapping->nsync  = -1;
    }
    if (data->nsync != mapping->nsync) {
        /* Mapping new sync manager */
        mapping->nsync = data->nsync;
        mapping->npdo  = -1;
    }
    if (data->npdo != mapping->npdo) {
        /* Mapping new PDO */
        ec_slave_config_t *sc = traverser_get_slave_config(data);
        mapping->npdo = data->npdo;
        if (data->npdo == 0) {
            ecrt_slave_config_pdo_assign_clear(sc, data->nsync);
        }
        ecrt_slave_config_pdo_assign_add(sc, data->nsync, data->npdo);
        ecrt_slave_config_pdo_mapping_clear(sc, data->npdo);
        ecrt_slave_config_pdo_mapping_add(sc, data->npdo,
                                          data->entry.index,
                                          data->entry.subindex,
                                          data->entry.bit_length);
    }

    return TRUE;
}

static int
fieldbus_automapping(Fieldbus *fieldbus)
{
    TraverseMapping mapping = {
        .nslave = -1,
        .nsync  = -1,
        .npdo   = -1,
        .nentry = -1,
    };
    return fieldbus_traverse_pdo_entries(fieldbus, traverser_mapper, &mapping);
}

static void
dump_configuration(TraverseConfiguration *configuration)
{
    const char *dir;

    if (configuration->channels == 0) {
        /* No configuration to dump */
        return;
    }

    switch (configuration->dir) {
    case EC_DIR_INPUT:
        dir = "I";
        break;
    case EC_DIR_OUTPUT:
        dir = "O";
        break;
    default:
        dir = "X";
        break;
    }

    info("%s%u%c%s", configuration->prefix, configuration->channels,
         configuration->is_digital ? 'D' : 'A', dir);

    configuration->prefix = "+";
    configuration->channels = 0;
}

static int
traverser_configurer(TraverserData *data)
{
    TraverseConfiguration *configuration = data->context;
    ec_slave_config_t *sc;
    int bytepos;
    unsigned bitpos;
    int is_digital;

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

    /* Update configuration */
    is_digital = data->entry.bit_length <= 1;
    if (is_digital != configuration->is_digital) {
        /* Dump the previous configuration */
        dump_configuration(configuration);
    }
    configuration->is_digital = is_digital;
    ++configuration->channels;

    return TRUE;
}

static int
fieldbus_autoconfigure(Fieldbus *fieldbus)
{
    TraverseConfiguration configuration = {
        .prefix = "",
        .channels = 0,
    };

    /* Fill process data with outputs first and inputs last,
     * similarily to what done by SOEM legacy */
    configuration.dir = EC_DIR_OUTPUT;
    if (! fieldbus_traverse_pdo_entries(fieldbus, traverser_configurer, &configuration)) {
        return FALSE;
    }
    dump_configuration(&configuration);

    configuration.dir = EC_DIR_INPUT;
    if (! fieldbus_traverse_pdo_entries(fieldbus, traverser_configurer, &configuration)) {
        return FALSE;
    }
    dump_configuration(&configuration);

    return TRUE;
}

static int
fieldbus_start(Fieldbus *fieldbus)
{
    ec_master_state_t master;
    int n;

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

    /* This is not strictly needed (the slaves should have been
     * already mapped in this way) but... just in case */
    info("Automapping slaves... ");
    if (! fieldbus_automapping(fieldbus)) {
        return FALSE;
    }
    info("%d slaves mapped\n", fieldbus->master_info.slave_count);

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

    info("Waiting all slaves in OP state... ");
    for (n = 0; n < 5000; ++n) {
        ecrt_master_send(fieldbus->master);
        ecrt_master_receive(fieldbus->master);
        ecrt_master_state(fieldbus->master, &master);
        if (master.al_states == EC_AL_STATE_OP) {
            break;
        }
        usleep(100);
    }
    if (master.al_states != EC_AL_STATE_OP) {
        const char *prefix = "";
        ec_slave_info_t slave_info;
        for (n = 0; n < fieldbus->master_info.slave_count; ++n) {
            ecrt_master_get_slave(fieldbus->master, n, &slave_info);
            if (slave_info.al_state != EC_AL_STATE_OP) {
                info("%s%s still in %u", prefix, slave_info.name, slave_info.al_state);
                prefix = ", ";
            }
        }
        info("\n");
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

static int
fieldbus_dump(Fieldbus *fieldbus)
{
    int wkc = fieldbus->domain1_state.working_counter;
    int expected_wkc = fieldbus->master_info.slave_count;
    int i;

    info("Iteration %" PRIu64 ":  %" PRId64 " usec WKC %d",
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

static int
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

int
main(int argc, char *argv[])
{
    Fieldbus fieldbus;
    unsigned long period;

    fieldbus_initialize(&fieldbus);

    /* Parse arguments */
    if (argc == 1) {
        period = 5000;
    } else if (argc > 2) {
        info("Too many arguments.\n");
        usage();
        return 0;
    } else if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        usage();
        return 0;
    } else {
            period = atoi(argv[1]);
    }

    if (fieldbus_start(&fieldbus)) {
        int64_t min_span = 0;
        int64_t max_span = 0;
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
            info("\nRoundtrip time (usec): min %" PRId64 "  max %" PRId64 "\n",
                 min_span, max_span);
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
                    info("\nScan too low (%" PRId64 " usec)\n", fieldbus.scan_span);
                } else {
                    usleep(period - fieldbus.scan_span);
                }
            }
            info("\nTime span of scans (usec): min %" PRId64 "  max %" PRId64 "\n",
                 min_span, max_span);
        }
        fieldbus_stop(&fieldbus);
    }

    fieldbus_finalize(&fieldbus);
    return 0;
}
