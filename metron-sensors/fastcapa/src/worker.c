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

#include "worker.h"

struct ether_fc_frame {
  uint16_t opcode;
  uint16_t param;
} __rte_packed;

static inline void prepare_pause_frame(uint16_t port, struct rte_mbuf *mbuf) {
    struct ether_fc_frame *pause_frame;
    struct rte_ether_hdr *hdr;

    /* Prepare a PAUSE frame */
    hdr = rte_pktmbuf_mtod(mbuf, struct rte_ether_hdr *);
    pause_frame = (struct ether_fc_frame *) &hdr[1];
    rte_eth_macaddr_get(port, &hdr->s_addr);

    void *tmp = &hdr->d_addr.addr_bytes[0];
    *((uint64_t *)tmp) = 0x010000C28001ULL;

    hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_FLOW_CONTROL);
    pause_frame->opcode = rte_cpu_to_be_16(OPCODE_PAUSE);
    pause_frame->param  = rte_cpu_to_be_16(PAUSE_TIME);
    mbuf->pkt_len  = 60;
    mbuf->data_len = 60;
}

static inline void clone_pause_frames(const struct rte_mbuf *original,
            struct rte_mbuf **clones, int num, struct rte_mempool *pool) {
    if(unlikely(rte_pktmbuf_alloc_bulk(pool, clones, num)))
        rte_exit(EXIT_FAILURE, "Error: Could not allocate pause frame buffers " \
                                    "for cloning on Core %d\n", rte_lcore_id());
    for (int i=0; i<num; i++) {
        clones[i]->pkt_len  = 60;
        clones[i]->data_len  = 60;
        rte_mov64(rte_pktmbuf_mtod(clones[i], void*), rte_pktmbuf_mtod(original, void*));
    }
}

static inline uint16_t send_pause_frames(uint16_t port, uint16_t queue,
            const struct rte_mbuf *pause_frame, struct rte_mbuf **pause_mbufs,
            uint16_t num, struct rte_mempool *pool) {
    uint16_t nb_tx = rte_eth_tx_burst(port, queue, pause_mbufs, num);
    if(likely(nb_tx))
        clone_pause_frames(pause_frame, pause_mbufs, nb_tx, pool);
    return nb_tx;
}

/*
 * Prints packet processing metrics to stdout.
 */
static inline void print_stats(app_params* params)
{
    struct rte_eth_stats eth_stats;
    unsigned int i;
    uint64_t in, out, depth, drops, pause_frames;
    app_stats stats;

    // header
    printf("\n       ----------------------------------  packets  ---------------------------------\n");
    printf("\n      %15s %15s %15s %15s %15s \n", " ----- in -----", " --- queued ---", "----- out -----", "---- drops ----", "---- pause ----");

    // summarize stats from each port
    in = out = depth = drops = pause_frames = 0;
    for (i=0; i < rte_eth_dev_count_avail(); i++) {
        rte_eth_stats_get(i, &eth_stats);
        in += eth_stats.ipackets;
        drops += eth_stats.ierrors + eth_stats.imissed + eth_stats.rx_nombuf;
        out += eth_stats.opackets;
    }
    printf("[nic] %15" PRIu64 " %15s %15" PRIu64 " %15" PRIu64 " %15s\n", in, "-", out, drops, "-");

    // summarize receive; from network to receive queues
    in = out = depth = drops = pause_frames = 0;
    for (i=0; i < params->nb_queues; i++) {
        in += params->rxw_stats[i].in;
        out += params->rxw_stats[i].out;
        depth += params->rxw_stats[i].depth;
        pause_frames += params->rxw_stats[i].pause_frames;
    }
    printf("[rx]  %15" PRIu64 " %15" PRIu64 " %15" PRIu64 " %15s %15" PRIu64 "\n", in, depth, out, "-", pause_frames);

    // summarize transmit; from receive queues to transmit rings
    in = out = depth = drops = pause_frames = 0;
    for (i=0; i < params->nb_queues; i++) {
        in += params->txw_stats[i].in;
        out += params->txw_stats[i].out;
        depth += params->txw_stats[i].depth;
        pause_frames += params->txw_stats[i].pause_frames;
    }
    printf("[tx]  %15" PRIu64 " %15" PRIu64 " %15" PRIu64 " %15s %15" PRIu64 "\n", in, depth, out, "-", pause_frames);

    // summarize push to kafka; from transmit rings to librdkafka
    kaf_stats(&stats);

    printf("\n       ------------------------------  kafka messages  ------------------------------\n");
    printf("\n      %15s %15s %15s %15s %15s \n", " ----- in -----", " --- queued ---", "----- out -----", "---- drops ----", "---- pause ----");

    printf("[kaf] %15" PRIu64 " %15" PRIu64 " %15" PRIu64 " %15" PRIu64 " %15s\n\n", stats.in, stats.depth, stats.out, stats.drops, "-");
    fflush(stdout);
}

/*
 * Process packets from a single queue.
 */
int receive_worker(rx_worker_params* params)
{
    const unsigned socket_id = rte_socket_id();
    unsigned dev_socket_id;

    volatile bool * quit_signal = params->quit_signal;

    const uint16_t port = params->port;
    const uint16_t queue = params->queue;

    const uint16_t rx_burst_size = params->rx_burst_size;
    struct rte_mbuf * pkts[rx_burst_size];
    struct rte_mbuf * bufptr;

    struct rte_mempool * pause_mbuf_pool = params->pause_mbuf_pool;
    const uint16_t flow_control = params->flow_control;
    const uint16_t pause_burst_size = params->pause_burst_size;
    struct rte_mbuf * pause_mbufs[pause_burst_size];
    struct rte_mbuf * pause_frame = NULL;

    struct rte_ring * pbuf_free_ring = params->pbuf_free_ring;
    struct rte_ring * pbuf_full_ring = params->pbuf_full_ring;
    const uint32_t watermark = params->watermark;

    pcap_buffer * buffer = NULL;
    pcap_packet_header header;
    size_t header_size = sizeof(header);
    struct timeval tv;

    uint16_t i, nb_rx;
    unsigned int flush = 0;

    LOG_INFO("Receive worker started; core=%u, socket=%u, port=%u, queue=%u\n", rte_lcore_id(), socket_id, port, queue);

    // check for cross-socket communication
    dev_socket_id = rte_eth_dev_socket_id(port);
    if (dev_socket_id != socket_id)
        LOG_WARN("Warning: Port %u on different socket from worker; performance will suffer\n", port);

    if (flow_control) {
        pause_frame = rte_pktmbuf_alloc(pause_mbuf_pool);
        if (!pause_frame)
            rte_exit(EXIT_FAILURE, "Error: Could not allocate pause frame buffer " \
                                            "on Core %d\n", rte_lcore_id());
        prepare_pause_frame(port, pause_frame);
        clone_pause_frames(pause_frame, pause_mbufs, pause_burst_size, pause_mbuf_pool);
    }

    if (!rte_ring_sc_dequeue_bulk(pbuf_free_ring, (void **)&buffer, 1, NULL))
        rte_exit(EXIT_FAILURE, "Error: Could not obtain an empty packet buffer (PBUF) " \
                                            "on Core %d\n", rte_lcore_id());

    while (likely(!(*quit_signal))) {

        // receive a 'burst' of packets
        nb_rx = rte_eth_rx_burst(port, queue, pkts, rx_burst_size);

        // add each packet to the ring buffer
        if (likely(nb_rx > 0)) {

            gettimeofday(&tv, NULL);
            header.timestamp = (uint32_t) tv.tv_sec;
            header.microseconds = (uint32_t) tv.tv_usec;

            for (i=0; i < nb_rx; i++) {
                bufptr = pkts[i];
                rte_prefetch0(rte_pktmbuf_mtod(bufptr, void *));

                header.packet_length = bufptr->pkt_len;
                header.packet_length_wire = bufptr->pkt_len;
                rte_mov16(buffer->buffer + buffer->offset, (void*)&header);
                buffer->offset += header_size;

                if (unlikely(bufptr->nb_segs > 1)) {
                    rte_prefetch0(rte_pktmbuf_mtod(bufptr->next, void *));
                    do {
                        rte_memcpy(buffer->buffer + buffer->offset, rte_pktmbuf_mtod(bufptr, void*), bufptr->data_len);
                        buffer->offset += bufptr->data_len;
                        bufptr = bufptr->next;
                    } while (bufptr);
                    bufptr = pkts[i];
                }
                else {
                    rte_memcpy(buffer->buffer + buffer->offset, rte_pktmbuf_mtod(bufptr, void*), header.packet_length);
                    buffer->offset += header.packet_length;
                }

                rte_pktmbuf_free(bufptr);
            }

            /* Update stats */
            params->stats->in += nb_rx;
            params->stats->depth+=nb_rx;
            flush = 0;
        }
        else
            flush++;

        /* Enqueue buffer to be flushed if full and get a new one */
        if (buffer->offset > watermark || (flush > 9999999 && buffer->offset)) {
            buffer->packets = params->stats->depth;

            while(!(rte_ring_sp_enqueue_bulk(pbuf_full_ring, (void **)&buffer, 1, NULL) || unlikely(*quit_signal))) {
                if (flow_control)
                    params->stats->pause_frames += send_pause_frames(port, queue, pause_frame,
                                        pause_mbufs, pause_burst_size, params->pause_mbuf_pool);
            }

            params->stats->out += params->stats->depth;
            params->stats->depth = 0;

            while(!(rte_ring_sc_dequeue_bulk(pbuf_free_ring, (void **)&buffer, 1, NULL) || unlikely(*quit_signal))) {
                if (flow_control)
                    params->stats->pause_frames += send_pause_frames(port, queue, pause_frame,
                                        pause_mbufs, pause_burst_size, params->pause_mbuf_pool);
            }

            buffer->offset = 0;
        }
    }

    if (buffer->offset) {
        buffer->packets = params->stats->depth;
        rte_ring_sp_enqueue_bulk(pbuf_full_ring, (void **)&buffer, 1, NULL);
    }

    LOG_INFO("Receive worker finished; core=%u, socket=%u, port=%u, queue=%u\n", rte_lcore_id(), socket_id, port, queue);
    return 0;
}

/*
 * The transmit worker is responsible for consuming packets from the transmit
 * rings and queueing the packets for bulk delivery to kafka.
 */
int transmit_worker(tx_worker_params *params)
{
    const unsigned socket_id = rte_socket_id();
    unsigned dev_socket_id;

    volatile bool * quit_signal = params->quit_signal;

    const uint16_t port = params->port;
    const uint16_t queue = params->queue;
    const uint16_t kafka_id = params->kafka_id;

    struct rte_ring * pbuf_full_ring = params->pbuf_full_ring;

    const uint16_t tx_burst_size = params->tx_burst_size;
    pcap_buffer * buffers[tx_burst_size];

    struct rte_mempool * pause_mbuf_pool = params->pause_mbuf_pool;
    const uint16_t flow_control = params->flow_control;
    const uint16_t pause_burst_size = params->pause_burst_size;
    struct rte_mbuf * pause_mbufs[pause_burst_size];
    struct rte_mbuf * pause_frame = NULL;

    int i, nb_in;

    LOG_INFO("Transmit worker started; core=%u, socket=%u, port=%u, queue=%u\n", rte_lcore_id(), socket_id, port, queue);

    // check for cross-socket communication
    dev_socket_id = rte_eth_dev_socket_id(port);
    if (dev_socket_id != socket_id)
        LOG_WARN("Warning: Port %u on different socket from worker; performance will suffer\n", port);

    if (flow_control) {
        pause_frame = rte_pktmbuf_alloc(pause_mbuf_pool);
        if (!pause_frame)
            rte_exit(EXIT_FAILURE, "Error: Could not allocate pause frame buffer " \
                                            "on Core %d\n", rte_lcore_id());
        prepare_pause_frame(port, pause_frame);
        clone_pause_frames(pause_frame, pause_mbufs, pause_burst_size, pause_mbuf_pool);
    }

    while (likely(!(*quit_signal))) {

        // dequeue packets from the ring
        nb_in = rte_ring_sc_dequeue_burst(pbuf_full_ring, (void **)&buffers, tx_burst_size, NULL);

        // prepare the packets to be sent to kafka
        if (likely(nb_in > 0)) {
            params->stats->depth = nb_in;
            for(i=0; i<nb_in; i++) {
                params->stats->in += buffers[i]->packets;
                while((kaf_send(buffers[i], kafka_id) && likely(!(*quit_signal)))) {
                    if (flow_control)
                        params->stats->pause_frames += send_pause_frames(port, queue, pause_frame,
                                            pause_mbufs, pause_burst_size, params->pause_mbuf_pool);
                    kaf_poll();
                }
                params->stats->out += buffers[i]->packets;
                params->stats->depth -= 1;
            }
        }
        kaf_poll();
    }

    LOG_INFO("Transmit worker finished; core=%u, socket=%u, port=%u, queue=%u\n", rte_lcore_id(), socket_id, port, queue);
    return 0;
}

/*
 * Monitors the receive and transmit workers.  Executed by the main thread, while
 * other threads are created to perform the actual packet processing.
 */
int monitor_workers(
    app_params* params,
    volatile bool *quit_signal)
{
    LOG_INFO("Starting to monitor workers; core=%u, socket=%u \n", rte_lcore_id(), rte_socket_id());
    while (likely(!(*quit_signal))) {
        print_stats(params);
        sleep(params->stats_period);
    }

    LOG_INFO("Finished monitoring workers; core=%u, socket=%u \n", rte_lcore_id(), rte_socket_id());
    return 0;
}
