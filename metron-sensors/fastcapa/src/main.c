/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <signal.h>

#include <rte_common.h>
#include <rte_malloc.h>

#include "args.h"
#include "kafka.h"
#include "nic.h"
#include "types.h"
#include "worker.h"

#define MBUF_CACHE_SIZE 512
#define PAUSE_MBUF_POOL_SIZE 8192

static volatile bool quit_signal = false;

/*
 * Handles interrupt signals.
 */
static void sig_handler(int sig_num)
{
    LOG_INFO("Exiting on signal '%d'\n", sig_num);
    LOG_INFO("Caught signal %s on core %u%s. Exiting...\n",
            strsignal(sig_num), rte_lcore_id(),
            rte_get_master_lcore()==rte_lcore_id()?" (MASTER CORE)":"");

    // set quit flag for rx thread to exit
    quit_signal = true;
}

static void wait_for_workers(void)
{
    unsigned lcore_id;

    /* wait for each worker to complete */
    RTE_LCORE_FOREACH_SLAVE(lcore_id) {
        if (rte_eal_wait_lcore(lcore_id) < 0) {
            LOG_WARN("Failed to wait for worker; lcore=%u \n", lcore_id);
        }
    }
}

int main(int argc, char* argv[])
{
    signal(SIGINT, sig_handler);
    app_params params = {0};
    parse_args(argc, argv, &params);

    /* Init config stats and buffer lists */
    params.rxw_params = calloc(params.nb_queues, sizeof(rx_worker_params));
    params.txw_params = calloc(params.nb_queues, sizeof(tx_worker_params));

    params.rxw_stats = calloc(params.nb_queues, sizeof(app_stats));
    params.txw_stats = calloc(params.nb_queues, sizeof(app_stats));

    params.rx_pools = calloc(params.nb_queues, sizeof(struct mempool *));
    params.tx_pools = calloc(params.nb_queues, sizeof(struct mempool *));

    params.pbuf_full_rings = calloc(params.nb_queues, sizeof(struct ring *));
    params.pbuf_free_rings = calloc(params.nb_queues, sizeof(struct ring *));

    params.buffers = calloc(params.nb_queues * params.nb_pbufs, sizeof(pcap_buffer *));

    kaf_init(&params);

    char name[32];
    uint16_t i, j, k, l, m, port;
    unsigned int lcore_id = rte_get_next_lcore(-1, 1, 0);
    int result;

    /* For each port */
    for (i = 0; i < params.nb_ports; i++) {
        port = params.port_list[i];

        /* Allocate memory */
        for (j = 0; j < params.nb_queues_per_port; j++) {

            k = i * params.nb_queues_per_port + j;

            sprintf(name, "RX_POOL_%d_%d", i, j);
            params.rx_pools[k] = rte_pktmbuf_pool_create(name, params.nb_mbufs,
                            MBUF_CACHE_SIZE, 0, params.mbuf_len, rte_socket_id());

            if (params.rx_pools[k] == NULL)
                rte_exit(EXIT_FAILURE, "Cannot create mbuf pool: (%d) %s\n",
                            rte_errno, rte_strerror(rte_errno));

            sprintf(name, "TX_POOL_%d_%d", i, j);
            params.tx_pools[k] = rte_pktmbuf_pool_create(name, PAUSE_MBUF_POOL_SIZE,
                            MBUF_CACHE_SIZE, 0, params.mbuf_len, rte_socket_id());

            if (params.tx_pools[k] == NULL)
                rte_exit(EXIT_FAILURE, "Cannot create pause frame mbuf pool: (%d) %s\n",
                            rte_errno, rte_strerror(rte_errno));

            sprintf(name, "PCE_RING_%d_%d", i, j);
            params.pbuf_free_rings[k] = rte_ring_create(name, params.nb_pbufs * 2,
                            rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);

            if (params.pbuf_free_rings[k] == NULL)
                rte_exit(EXIT_FAILURE, "Cannot create pbuf free ring: (%d) %s\n",
                            rte_errno, rte_strerror(rte_errno));

            sprintf(name, "PCF_RING_%d_%d", i, j);
            params.pbuf_full_rings[k] = rte_ring_create(name, params.nb_pbufs * 2,
                            rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);

            if (params.pbuf_full_rings[k] == NULL)
                rte_exit(EXIT_FAILURE, "Cannot create pbuf full ring: (%d) %s\n",
                            rte_errno, rte_strerror(rte_errno));

            for (l = 0; l < params.nb_pbufs; l++) {

                m = i * params.nb_queues_per_port * params.nb_pbufs + j * params.nb_pbufs + l;

                params.buffers[m] = calloc(1, sizeof(pcap_buffer));
                params.buffers[m]->offset = 0;
                params.buffers[m]->packets = 0;
                params.buffers[m]->buffer = rte_malloc(NULL, params.pbuf_len, 0);

                if (params.buffers[m]->buffer == NULL)
                    rte_exit(EXIT_FAILURE, "Cannot create pbuf buffer: (%d) %s\n",
                            rte_errno, rte_strerror(rte_errno));
            }

            m = i * params.nb_queues_per_port * params.nb_pbufs + j * params.nb_pbufs;
            rte_ring_sp_enqueue_bulk(params.pbuf_free_rings[k], (void **)&params.buffers[m], params.nb_pbufs, NULL);
        }

        /* Initialise and start the port */
        result = port_init(
                port,
                &params.rx_pools[i * params.nb_queues_per_port],
                params.nb_queues_per_port,
                params.nb_rx_desc,
                params.flow_control);

        if (result) {
            rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu8 "\n", port);
        }

        /* Configure and start the capture workers */
        for (j = 0; j < params.nb_queues_per_port; j++) {

            k = i * params.nb_queues_per_port + j;

            rx_worker_params * config = &(params.rxw_params[k]);
            config->worker_id = k;
            config->quit_signal = &quit_signal;
            config->port = port;
            config->queue = j;
            config->rx_burst_size = params.rx_burst_size;
            config->pause_burst_size = params.pause_burst_size;
            config->flow_control = params.flow_control;
            config->mw_timestamp = params.mw_timestamp;
            config->pbuf_free_ring = params.pbuf_free_rings[k];
            config->pbuf_full_ring = params.pbuf_full_rings[k];
            config->watermark = params.watermark;
            config->pause_mbuf_pool = params.tx_pools[k];
            config->stats = &(params.rxw_stats[k]);

            LOG_INFO("Launching receive worker; worker=%u, port=%u, core=%u, queue=%u\n", k, port, lcore_id, j);
            result = rte_eal_remote_launch((lcore_function_t *) receive_worker, config, lcore_id);
            if (result)
                rte_exit(EXIT_FAILURE, "Error: Could not launch capture process on lcore %d: (%d) %s\n",
                            lcore_id, result, rte_strerror(-result));

            lcore_id = rte_get_next_lcore(lcore_id, 1, 0);
        }

        /* Configure and start the transmit workers */
        for (j = 0; j < params.nb_queues_per_port; j++) {

            k = i * params.nb_queues_per_port + j;

            tx_worker_params * config = &(params.txw_params[k]);
            config->worker_id = k;
            config->quit_signal = &quit_signal;
            config->port = port;
            config->queue = (2 * j) + 1;
            config->kafka_id = k;
            config->tx_burst_size = params.nb_pbufs;
            config->pause_burst_size = params.pause_burst_size;
            config->flow_control = params.flow_control;
            config->pbuf_free_ring = params.pbuf_free_rings[k];
            config->pbuf_full_ring = params.pbuf_full_rings[k];
            config->pause_mbuf_pool = params.tx_pools[k];
            config->stats = &(params.txw_stats[k]);

            LOG_INFO("Launching transmit worker; worker=%u, port=%u, core=%u, queue=%u\n", k, port, lcore_id, j);
            result = rte_eal_remote_launch((lcore_function_t *) transmit_worker, config, lcore_id);
            if (result)
                rte_exit(EXIT_FAILURE, "Error: Could not launch transmit process on lcore %d: (%d) %s\n",
                            lcore_id, result, rte_strerror(-result));

            lcore_id = rte_get_next_lcore(lcore_id, 1, 0);
        }

    }

    if (params.stats_period)
        monitor_workers(&params, &quit_signal);

    while(!quit_signal)
        sleep(5);

    wait_for_workers();

    /* clean up */
    kaf_close();
    free(params.port_list);
    free(params.rxw_params);
    free(params.txw_params);
    free(params.rxw_stats);
    free(params.txw_stats);
    free(params.rx_pools);
    free(params.tx_pools);
    free(params.pbuf_full_rings);
    free(params.pbuf_free_rings);
    free(params.buffers);

    return 0;
}
