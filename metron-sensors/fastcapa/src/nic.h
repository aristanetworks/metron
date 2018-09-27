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

#ifndef METRON_NIC_H
#define METRON_NIC_H

#include <unistd.h>

#include <rte_common.h>
#include <rte_ethdev.h>

#include "types.h"

#define TX_DESC_DEFAULT 1024

/*
 * Initialize a NIC port.
 */
int port_init(
    const uint16_t port,
    struct rte_mempool ** mbuf_pools,
    const uint16_t rx_queues,
    const uint16_t nb_rx_desc,
    unsigned int flow_control);

#endif
