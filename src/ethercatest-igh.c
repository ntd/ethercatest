/* Port of ethercatest-soem to IgH ethercat
 *
 * ethercatest-igh: test EtherCAT connection by monitoring I/O
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
#include <ecrt.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>


typedef struct {
    ec_master_t *master;
    ec_master_info_t master_info;
    ec_domain_t *domain;
    ec_domain_state_t domain_state;
    int64_t iteration_time;
    uint64_t iteration;
    uint8_t *map;
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
    ec_direction_t  dir;
    const char *    prefix;
    unsigned        channels;
    int             is_digital;
} TraverseConfiguration;

typedef void (*FieldbusCallback)(Fieldbus *);


static void
fieldbus_initialize(Fieldbus *fieldbus)
{
    /* Let's start by 0-filling `fieldbus` to avoid surprises */
    memset(fieldbus, 0, sizeof(*fieldbus));

    fieldbus->master = NULL;
    fieldbus->domain = NULL;
    fieldbus->map = NULL;
    fieldbus->iteration = 0;
    fieldbus->iteration_time = 0;
}

static int
fieldbus_send(Fieldbus *fieldbus)
{
    int status;

    status = ecrt_domain_queue(fieldbus->domain);
    if (status < 0) {
        return status;
    }

    return ecrt_master_send(fieldbus->master);
}

static int
fieldbus_receive(Fieldbus *fieldbus)
{
    int status;

    status = ecrt_master_receive(fieldbus->master);
    if (status < 0) {
        return status;
    }

    status = ecrt_domain_process(fieldbus->domain);
    if (status < 0) {
        return status;
    }

    return ecrt_domain_state(fieldbus->domain, &fieldbus->domain_state);
}

static int
fieldbus_iterate(Fieldbus *fieldbus, FieldbusCallback callback)
{
    int64_t start, stop;
    int status;

    start = get_monotonic_time();

    status = fieldbus_receive(fieldbus);
    if (status < 0) {
        return status;
    }

    if (callback != NULL) {
        callback(fieldbus);
    }

    status = fieldbus_send(fieldbus);
    if (status < 0) {
        return status;
    }

    stop = get_monotonic_time();

    ++fieldbus->iteration;
    fieldbus->iteration_time = stop - start;
    return 0;
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
                                              data->fieldbus->domain, &bitpos);
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
    ec_master_state_t state;
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
    fieldbus->domain = ecrt_master_create_domain(fieldbus->master);
    if (fieldbus->domain == NULL) {
        info("failed\n");
        return FALSE;
    }
    info("done\n");

    info("Autoconfiguring slaves... ");
    if (! fieldbus_autoconfigure(fieldbus)) {
        info("failed\n");
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
    fieldbus->map = ecrt_domain_data(fieldbus->domain);
    if (fieldbus->map == NULL) {
        info("failed\n");
        return FALSE;
    }
    info("done\n");

    info("Waiting all slaves in OP state... ");
    for (n = 0; n < 10000; ++n) {
        fieldbus_receive(fieldbus);
        fieldbus_send(fieldbus);
        usleep(500);
        ecrt_master_state(fieldbus->master, &state);
        if (state.al_states == EC_AL_STATE_OP) {
            break;
        }
    }
    if (state.al_states != EC_AL_STATE_OP) {
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

static void
fieldbus_dump(Fieldbus *fieldbus)
{
    int wkc = fieldbus->domain_state.working_counter;
    int i;

    info("Iteration %" PRIu64 ":  %" PRId64 " usec  WKC %d",
         fieldbus->iteration, fieldbus->iteration_time, wkc);

    for (i = 0; i < ecrt_domain_size(fieldbus->domain); ++i) {
        info(" %02X", fieldbus->map[i]);
    }
    info("   \r");
}

static void
digital_counter(Fieldbus *fieldbus)
{
    /* Show a digital counter that updates every 20 iterations
     * in the first 8 digital outputs */
    fieldbus->map[0] = fieldbus->iteration / 20;
}

static void
usage(void)
{
    info("Usage: ethercatest-igh [-q|--quiet] [PERIOD]\n"
         "  [PERIOD] Scantime in us (0 for roundtrip performances)\n");
}

int
main(int argc, char *argv[])
{
    Fieldbus fieldbus;
    const char *arg;
    unsigned long period;
    int n, silent;

    setbuf(stdout, NULL);

    fieldbus_initialize(&fieldbus);

    /* Parse arguments */
    period = 5000;
    silent = 0;

    for (n = 1; n < argc; ++n) {
        arg = argv[n];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage();
            return 0;
        } else if (strcmp(arg, "-q") == 0 || strcmp(arg, "--quiet") == 0) {
            silent = 1;
        } else {
            period = atoi(arg);
        }
    }

    if (! fieldbus_start(&fieldbus)) {
        return 2;
    }

    int64_t min_time = 0;
    int64_t max_time = 0;
    int64_t total_time = 0;
    int errors = 0;
    uint64_t iterations = 100000 / (period / 100 + 3);
    FieldbusCallback cycle = period > 0 ? digital_counter : NULL;

    int status;
    while (++fieldbus.iteration < iterations) {
        status = fieldbus_iterate(&fieldbus, cycle);
        if (status < 0) {
            ++errors;
            info("\nIteration error: status %d\n", status);
            continue;
        }
        if (! silent) {
            fieldbus_dump(&fieldbus);
        }
        if (max_time == 0) {
            min_time = max_time = fieldbus.iteration_time;
        } else if (fieldbus.iteration_time < min_time) {
            min_time = fieldbus.iteration_time;
        } else if (fieldbus.iteration_time > max_time) {
            max_time = fieldbus.iteration_time;
        }
        total_time += fieldbus.iteration_time;
        wait_next_iteration(fieldbus.iteration_time, period);
    }
    info("\nIteration time (usec): min %" PRId64 "  max %" PRId64 "  total %" PRId64 "  errors %d\n",
         min_time, max_time, total_time, errors);
    fieldbus_stop(&fieldbus);

    return 0;
}
