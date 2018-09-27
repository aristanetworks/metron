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

#include "args.h"

/*
 * Print usage information to the user.
 */
static void print_usage(void)
{
    printf("fastcapa [EAL options] -- [APP options]\n"
           " -p PORT_MASK        Bitmask of ports to bind                     [%s]\n"
           " -b RX_BURST_SIZE    Burst size of receive worker                 [%d]\n"
           " -a PAUSE_BURST_SIZE Burst size of soft pause frames              [%d]\n"
           " -d NB_RX_DESC       Num of descriptors for receive ring          [%d]\n"
           " -m NB_MBUFS         Num of pkt receive bufs per queue (2^N)      [%d]\n"
           " -i MBUF_LEN         Size (in bytes) of each MBUF                 [%d]\n"
           "                     Default value is 2KB + RTE_PKTMBUF_HEADROOM      \n"
           " -n NB_PBUFS         Num of pkt collection bufs per queue (2^N)   [%d]\n"
           "                     (used to batch packets before transmitting)      \n"
           " -j PBUF_LEN         Size (in bytes) of each PBUF (2^N)           [%d]\n"
           " -q QUEUES_PER_PORT  Num of RX/TX queues (cores) per port         [%d]\n"
           " -s STATS_PERIOD     Interval for printing stats (0:Disabled)     [%d]\n"
           " -t KAFKA_TOPIC      Name of the kafka topic                      [%s]\n"
           " -c KAFKA_CONF       File containing configs for kafka client         \n"
           " -f KAFKA_STATS      Append kafka client stats to a file              \n"
           " -z                  Use pause frames for flow control                \n"
           " -h                  Print this help message                          \n",
        STR(DEFAULT_PORT_MASK),
        DEFAULT_RX_BURST_SIZE,
        DEFAULT_PAUSE_BURST_SIZE,
        DEFAULT_NB_RX_DESC,
        DEFAULT_NB_MBUFS,
        DEFAULT_MBUF_LEN,
        DEFAULT_NB_PBUFS,
        DEFAULT_PBUF_LEN,
        DEFAULT_QUEUES_PER_PORT,
        DEFAULT_STATS_PERIOD,
        STR(DEFAULT_KAFKA_TOPIC));
}

/*
 * Parse the 'portmask' command line argument.
 */
static int parse_portmask(const char* portmask)
{
    char* end = NULL;
    unsigned long pm;

    // parse hexadecimal string
    pm = strtoul(portmask, &end, 16);

    if ((portmask[0] == '\0') || (end == NULL) || (*end != '\0')) {
        return -1;
    }
    else if (pm == 0) {
        return -1;
    }
    else {
        return pm;
    }
}

/*
 * Check if a file exists
 */
static bool file_exists(const char* filepath)
{
    struct stat buf;
    return (stat(filepath, &buf) == 0);
}

/*
 * Parse the command line arguments passed to the application; the arguments
 * which no not go directly to DPDK's EAL.
 */
static int parse_app_args(int argc, char** argv, app_params* p)
{
    int opt;
    char** argvopt;
    int option_index;
    static struct option lgopts[] = {
        { NULL, 0, 0, 0 }
    };

    // set default args
    p->nb_rx_desc = DEFAULT_NB_RX_DESC;
    p->portmask = parse_portmask(STR(DEFAULT_PORT_MASK));
    p->kafka_topic = STR(DEFAULT_KAFKA_TOPIC);
    p->rx_burst_size = DEFAULT_RX_BURST_SIZE;
    p->pause_burst_size = DEFAULT_PAUSE_BURST_SIZE;
    p->nb_queues_per_port = DEFAULT_QUEUES_PER_PORT;
    p->nb_mbufs = DEFAULT_NB_MBUFS;
    p->mbuf_len = DEFAULT_MBUF_LEN;
    p->nb_pbufs = DEFAULT_NB_PBUFS;
    p->pbuf_len = DEFAULT_PBUF_LEN;
    p->flow_control = DEFAULT_FLOW_CONTROL;
    p->stats_period = DEFAULT_STATS_PERIOD;
    p->nb_ports = 0;

    // parse arguments to this application
    argvopt = argv;
    while ((opt = getopt_long(argc, argvopt, "a:b:c:d:f:h:i:j:m:n:p:q:s:t:z", lgopts, &option_index)) != EOF) {
        switch (opt) {

            // help
            case 'h':
                print_usage();
                return -1;

            // rx burst size
            case 'b':
                p->rx_burst_size = atoi(optarg);
                if (p->rx_burst_size < 1 || p->rx_burst_size > MAX_BURST_SIZE) {
                    fprintf(stderr, "Invalid rx burst size; burst=%u must be in [1, %u]. \n", p->rx_burst_size, MAX_BURST_SIZE);
                    print_usage();
                    return -1;
                }
                break;

            // pause burst size
            case 'a':
                p->pause_burst_size = atoi(optarg);
                if (p->pause_burst_size < 1 || p->pause_burst_size > MAX_BURST_SIZE) {
                    fprintf(stderr, "Invalid pause frame burst size; burst=%u must be in [1, %u]. \n", p->pause_burst_size, MAX_BURST_SIZE);
                    print_usage();
                    return -1;
                }
                break;

            // number of receive descriptors
            case 'd':
                p->nb_rx_desc = atoi(optarg);
                if (p->nb_rx_desc < 1) {
                    fprintf(stderr, "Invalid num of receive descriptors: '%s' \n", optarg);
                    print_usage();
                    return -1;
                }
                break;

            // number of memory buffers in mempool
            case 'm':
                p->nb_mbufs = atoi(optarg);
                if (p->nb_mbufs % 2) {
                    fprintf(stderr, "Invalid num of packet receive buffers (MBUFS): '%s' \n", optarg);
                    print_usage();
                    return -1;
                }
                break;

            // length of each memory buffer
            case 'i':
                p->mbuf_len = atoi(optarg);
                if (p->mbuf_len % 2) {
                    fprintf(stderr, "Invalid packet receive buffer length (MBUF_LEN): '%s' \n", optarg);
                    print_usage();
                    return -1;
                }
                break;

            // number of packet collection buffers
            case 'n':
                p->nb_pbufs = atoi(optarg);
                if (p->nb_pbufs % 2) {
                    fprintf(stderr, "Invalid num of packet collection buffers (PBUFS): '%s' \n", optarg);
                    print_usage();
                    return -1;
                }
                break;

            // length of each packet buffer
            case 'j':
                p->pbuf_len = atoi(optarg);
                if (p->pbuf_len % 2) {
                    fprintf(stderr, "Invalid packet collection buffer length (PBUF_LEN): '%s' \n", optarg);
                    print_usage();
                    return -1;
                }
                break;

            // number of queues per port
            case 'q':
                p->nb_queues_per_port = atoi(optarg);
                if (p->nb_queues_per_port < 1) {
                    fprintf(stderr, "Invalid num of queues per port: '%s' \n", optarg);
                    print_usage();
                    return -1;
                }
                break;

            // stats period
            case 's':
                p->stats_period = atoi(optarg);
                break;

            // port mask
            case 'p':
                p->portmask = parse_portmask(optarg);
                if (p->portmask == 0) {
                    fprintf(stderr, "Invalid portmask: '%s'\n", optarg);
                    print_usage();
                    return -1;
                }
                break;

            // flow control
            case 'z':
                p->flow_control = 1;
                break;

            // kafka topic
            case 't':
                p->kafka_topic = strdup(optarg);
                if (!valid(p->kafka_topic)) {
                    printf("Invalid kafka topic: '%s'\n", optarg);
                    print_usage();
                    return -1;
                }
                break;

            // kafka config path
            case 'c':
                p->kafka_config_path = strdup(optarg);
                if (!valid(p->kafka_config_path) || !file_exists(p->kafka_config_path)) {
                    fprintf(stderr, "Invalid kafka config: '%s'\n", optarg);
                    print_usage();
                    return -1;
                }
                break;

            // kafka stats path
            case 'f':
                p->kafka_stats_path = strdup(optarg);
                break;

            default:
                print_usage();
                return -1;
        }
    }

    uint16_t port, required_cores;
    uint32_t rx_burst_len;
    const uint16_t avail_ports = rte_eth_dev_count_avail();

    if (avail_ports == 0)
         rte_exit(EXIT_FAILURE, "No ethernet ports detected.\n");

    p->port_list = calloc(64, sizeof(uint16_t));
    for (port = 0; port < avail_ports; port++)
        if (p->portmask & (uint64_t)(1ULL << port))
            p->port_list[p->nb_ports++] = port;

    if (p->nb_ports < 1)
        rte_exit(EXIT_FAILURE, "Error: No available enabled ports. Portmask set?\n");
    else
        LOG_INFO("Listening on %u ports.\n", p->nb_ports);

    rx_burst_len = p->mbuf_len * p->rx_burst_size;

    if (p->pbuf_len < 2 * rx_burst_len)
        rte_exit(EXIT_FAILURE, "Packet buffer length should be atleast %d B.\n",
                            2 * rx_burst_len);

    p->nb_queues = p->nb_ports * p->nb_queues_per_port;
    p->watermark = p->pbuf_len - rx_burst_len;

    LOG_INFO("Portmask: %ld Num RX descriptors: %d\n",
                            p->portmask, p->nb_rx_desc);
    LOG_INFO("Cores/Queues Per Port: %d RX Burst Size: %d\n",
                            p->nb_queues_per_port, p->rx_burst_size);
    LOG_INFO("MBufs: Num: %d Len: %d B  PBufs: Num: %d Len: %d B\n",
                            p->nb_mbufs, p->mbuf_len, p->nb_pbufs, p->pbuf_len);
    LOG_INFO("RX Burst Len: %d Watermark: %d\n",
                            rx_burst_len, p->watermark);
    LOG_INFO("Flow control: %s Pause Burst Size: %d\n",
                            p->flow_control?"ON":"OFF", p->pause_burst_size);

    /* Check number of allocated cores */
    required_cores = p->nb_queues * 2 + 1;
    if (required_cores > rte_lcore_count())
        rte_exit(EXIT_FAILURE, "Additional lcore(s) required; found=%u, required=%u \n",
            rte_lcore_count(), required_cores);

    LOG_INFO("Using %u cores out of %d allocated\n",
                            required_cores, rte_lcore_count());

    LOG_INFO("Kafka Topic: %s\n", p->kafka_topic);
    LOG_INFO("Kafka Config: %s\n", p->kafka_config_path);
    LOG_INFO("Kafka Stats: %s\n", p->kafka_stats_path);

    // reset getopt lib
    optind = 0;
    return 0;
}

/*
 * Parse the command line arguments passed to the application.
 */
int parse_args(int argc, char** argv, app_params* p)
{
  // initialize the environment
  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
      rte_exit(EXIT_FAILURE, "Failed to initialize EAL: %i\n", ret);
  }

  // advance past the environmental settings
  argc -= ret;
  argv += ret;

  // parse arguments to the application
  ret = parse_app_args(argc, argv, p);
  if (ret < 0) {
      rte_exit(EXIT_FAILURE, "\n");
  }

  return 0;
}
