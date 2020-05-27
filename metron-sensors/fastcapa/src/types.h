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

#ifndef METRON_TYPES_H
#define METRON_TYPES_H

#include <rte_common.h>

/*
 * Allow strings to be used for preprocessor #define's
 */
#define STR_EXPAND(tok) #tok
#define STR(tok) STR_EXPAND(tok)

typedef int bool;
#define true 1
#define false 0

#define valid(s) (s == NULL ? false : strlen(s) > 1)

/*
 * Allows unused function parameters to be marked as unused to
 * avoid unnecessary compile-time warnings.
 */
#ifdef __GNUC__
#  define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#  define UNUSED(x) UNUSED_ ## x
#endif

/*
 * Logging definitions
 */
#define RTE_LOGTYPE_FASTCAPA RTE_LOGTYPE_USER1

#define LOG_ERR(fmt, args...) RTE_LOG(ERR, FASTCAPA, fmt, ##args)
#define LOG_WARN(fmt, args...) RTE_LOG(WARNING, FASTCAPA, fmt, ##args)
#define LOG_INFO(fmt, args...) RTE_LOG(INFO, FASTCAPA, fmt, ##args)

#ifdef DEBUG
#define LOG_LEVEL RTE_LOG_DEBUG
#define LOG_DEBUG(fmt, args...) RTE_LOG(DEBUG, FASTCAPA, fmt, ##args)
#else
#define LOG_LEVEL RTE_LOG_INFO
#define LOG_DEBUG(fmt, args...) do {} while (0)
#endif

/*
 * Pcap packet header
 */
typedef struct {
    uint32_t seconds;
    uint32_t nanoseconds;
    uint32_t packet_length;
    uint32_t packet_length_wire;
} __rte_packed pcap_packet_header;

/*
 * Tracks packet processing metrics.
 */
typedef struct {
    uint64_t in;
    uint64_t out;
    uint64_t depth;
    uint64_t drops;
    uint64_t pause_frames;
} __rte_cache_aligned app_stats;

/*
 * Pcap buffer to store a collection of packets.
 */
typedef struct {

    /* current offset from buffer start */
    uint32_t offset;

    /* packets in the buffer */
    uint32_t packets;

    /* pointer to the actual buffer */
    unsigned char * buffer;

} __rte_cache_aligned pcap_buffer;

/*
 * The parameters required by a receive worker.
 */
typedef struct {

    /* worker identifier */
    uint16_t worker_id;

    /* pointer to the quit flag set by the signal handler */
    volatile bool * quit_signal;

    /* Defines which port packets will be consumed from. */
    uint16_t port;

    /* queue identifier from which packets are fetched and pause frames sent */
    uint16_t queue;

    /* how many packets are pulled off the queue at a time */
    uint16_t rx_burst_size;

    /* how many pause frames are sent at a time */
    uint16_t pause_burst_size;

    /* Use flow control */
    uint16_t flow_control;

    /* the ring onto which full packet buffers are enqueued */
    struct rte_ring * pbuf_full_ring;

    /* the ring from which empty packet buffers are dequeued */
    struct rte_ring * pbuf_free_ring;

    /* packet buffer watermark */
    uint32_t watermark;

    /* the memory pool used to send out pause frames */
    struct rte_mempool * pause_mbuf_pool;

    /* metrics */
    app_stats * stats;

} __rte_cache_aligned rx_worker_params;

/*
 * The parameters required by a transmit worker.
 */
typedef struct {

    /* worker identifier */
    uint16_t worker_id;

    /* pointer to the quit flag set by the signal handler */
    volatile bool * quit_signal;

    /* Defines which port packets will be consumed from. */
    uint16_t port;

    /* queue identifier from which pause frames are sent */
    uint16_t queue;

    /* identifies the kafka client connection used by the worker */
    uint16_t kafka_id;

    /* how many packets buffers are pulled off the queue at a time */
    uint16_t tx_burst_size;

    /* how many pause frames are sent at a time */
    uint16_t pause_burst_size;

    /* Use flow control */
    uint16_t flow_control;

    /* the ring onto which full packet buffers are enqueued */
    struct rte_ring * pbuf_full_ring;

    /* the ring from which empty packet buffers are dequeued */
    struct rte_ring * pbuf_free_ring;

    /* the memory pool used to send out pause frames */
    struct rte_mempool * pause_mbuf_pool;

    /* worker metrics */
    app_stats * stats;

} __rte_cache_aligned tx_worker_params;

/*
 * Application configuration parameters.
 */
typedef struct {

    /* The number of receive descriptors to allocate for the receive ring. */
    uint16_t nb_rx_desc;

    /* The number of NIC ports from which packets will be consumed. */
    uint16_t nb_ports;

    /* The list of selected ports */
    uint16_t * port_list;

    /* The number of rx and tx queues to set up for each ethernet device. */
    uint16_t nb_queues_per_port;

    /* The total number of connections */
    uint32_t nb_queues;

    /* The number of packet buffers per queue (must be a power of 2). */
    uint32_t nb_mbufs;

    /* The number of packet collection buffers per queue (must be a power of 2). */
    uint16_t nb_pbufs;

    /* The size of each packet buffer */
    uint16_t mbuf_len;

    /* The size of each packet collection buffer */
    uint32_t pbuf_len;

    /* The maximum number of packets retrieved by the receive worker. */
    uint16_t rx_burst_size;

    /* how many pause frames are sent at a time */
    uint16_t pause_burst_size;

    /* packet buffer watermark */
    uint32_t watermark;

    /* Defines which ports packets will be consumed from. */
    uint64_t portmask;

    /* The name of the Kafka topic that packet data is sent to. */
    const char * kafka_topic;

    /* A file containing configuration values for the Kafka client. */
    char * kafka_config_path;

    /* A file to which the Kafka stats are appended to. */
    char * kafka_stats_path;

    /* Use flow control */
    uint16_t flow_control;

    /* Print statistics periodically */
    uint16_t stats_period;

    /* List of rings onto which full packet buffers are enqueued */
    struct rte_ring ** pbuf_full_rings;

    /* List of rings which empty packet buffers are dequeued */
    struct rte_ring ** pbuf_free_rings;

    /* List of pcap buffers for collecting packets */
    pcap_buffer ** buffers;

    /* List of rx memory buffer pools */
    struct rte_mempool ** rx_pools;

    /* List of pause frame memory buffer pools */
    struct rte_mempool ** tx_pools;

    /* List of rx worker configurations */
    rx_worker_params * rxw_params;

    /* List of tx worker configurations */
    tx_worker_params * txw_params;

    /* List of rx worker stats */
    app_stats * rxw_stats;

    /* List of tx worker stats */
    app_stats * txw_stats;

} __rte_cache_aligned app_params;

#endif
