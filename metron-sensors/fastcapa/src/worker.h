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

#ifndef METRON_WORKER_H
#define METRON_WORKER_H

#include <malloc.h>
#include <unistd.h>

#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>

#include "types.h"
#include "kafka.h"

#define ETHER_TYPE_FLOW_CONTROL 0x8808
#define OPCODE_PAUSE 0x0001
#define PAUSE_TIME 65535

/*
 * Process packets from a single queue.
 */
int receive_worker(rx_worker_params* params);

/*
 * The transmit worker is responsible for consuming packets from the transmit
 * rings and queueing the packets for bulk delivery to kafka.
 */
int transmit_worker(tx_worker_params *params);

/*
 * Monitors the receive and transmit workers.  Executed by the main thread, while
 * other threads are created to perform the actual packet processing.
 */
int monitor_workers(
    app_params* params,
    volatile bool *quit_signal);

#endif
