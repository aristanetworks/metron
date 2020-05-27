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

#ifndef METRON_ARGS_H
#define METRON_ARGS_H

#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>

#include <rte_common.h>
#include <rte_ethdev.h>

#include "types.h"

#define DEFAULT_PORT_MASK 0x01
#define DEFAULT_RX_BURST_SIZE 128
#define DEFAULT_PAUSE_BURST_SIZE 128
#define DEFAULT_NB_RX_DESC 1024
#define DEFAULT_NB_MBUFS 65536
#define DEFAULT_MBUF_LEN RTE_MBUF_DEFAULT_BUF_SIZE
#define DEFAULT_NB_PBUFS 512
#define DEFAULT_PBUF_LEN 4 * 1024 * 1024
#define DEFAULT_QUEUES_PER_PORT 1
#define DEFAULT_STATS_PERIOD 0
#define DEFAULT_KAFKA_TOPIC pcap
#define DEFAULT_KAFKA_STATS_PATH 0
#define DEFAULT_FLOW_CONTROL 0
#define DEFAULT_MW_TIMESTAMP 0
#define MAX_BURST_SIZE 8192

/**
 * Parse the command line arguments passed to the application.
 */
int parse_args(int argc, char** argv, app_params* app);

#endif
