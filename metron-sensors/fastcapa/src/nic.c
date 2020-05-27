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

#include "nic.h"

/**
 * Default receive queue settings.
 */
static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
        .mq_mode = ETH_MQ_RX_NONE,
        .max_rx_pkt_len = RTE_ETHER_MAX_LEN,
    },
    .txmode = {
        .mq_mode = ETH_MQ_TX_NONE,
    },
};

/*
 * Initialize a NIC port.
 */
int port_init(
    const uint16_t port,
    struct rte_mempool ** mbuf_pools,
    const uint16_t rx_queues,
    const uint16_t nb_rx_desc,
    unsigned int flow_control)
{
    struct rte_ether_addr addr;
    struct rte_eth_conf port_conf = port_conf_default;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_rxconf rxq_conf;
    struct rte_eth_txconf txq_conf;
    struct rte_eth_fc_conf fc_conf;
    struct rte_eth_link link;
    uint16_t socket, q, tx_queues = 0;
    int retval, retry = 5;

    if (flow_control)
        tx_queues = rx_queues;

    if (rte_eth_dev_is_valid_port(port) == 0) {
        LOG_ERR("Invalid Port; port=%u \n", port);
        return -EINVAL;
    }

    // get device info and valid config
    socket = rte_eth_dev_socket_id(port);
    rte_eth_dev_info_get(port, &dev_info);

    // print diagnostics
    rte_eth_macaddr_get(port, &addr);
    LOG_INFO("Port %u:, mac=%02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8
                    " %02" PRIx8 " %02" PRIx8 ", RXdesc/queue=%d\n",
        (unsigned int) port,
        addr.addr_bytes[0], addr.addr_bytes[1],
        addr.addr_bytes[2], addr.addr_bytes[3],
        addr.addr_bytes[4], addr.addr_bytes[5],
        nb_rx_desc);

    LOG_INFO("TX Desc Info : Max = %hu, Min = %hu, Multiple of %hu\n",
            dev_info.tx_desc_lim.nb_max,
            dev_info.tx_desc_lim.nb_min,
            dev_info.tx_desc_lim.nb_align);

    LOG_INFO("TX Queue Info : Max = %hu\n", dev_info.max_tx_queues);

    LOG_INFO("RX Desc Info : Max = %hu, Min = %hu, Multiple of %hu\n",
            dev_info.rx_desc_lim.nb_max,
            dev_info.rx_desc_lim.nb_min,
            dev_info.rx_desc_lim.nb_align);

    LOG_INFO("RX Queue Info : Max = %hu\n", dev_info.max_rx_queues);

    /* Check that the requested number of RX queues is valid */
    if (rx_queues > dev_info.max_rx_queues) {
        LOG_ERR("Too many RX queues for device; port=%u, rx_queues=%u, max_queues=%u \n",
            port, rx_queues, dev_info.max_rx_queues);
        return -EINVAL;
    }

    /* Check that the requested number of TX queues is valid */
    if (tx_queues > dev_info.max_tx_queues) {
        LOG_ERR("Too many TX queues for device; port=%u, tx_queues=%u, max_queues=%u \n",
            port, tx_queues, dev_info.max_tx_queues);
        return -EINVAL;
    }

    /* Configure multiqueue (Activate Receive Side Scaling on UDP/TCP fields) */
    if (rx_queues > 1) {
        port_conf.rxmode.mq_mode = ETH_MQ_RX_RSS;
        port_conf.rx_adv_conf.rss_conf.rss_key = NULL;
        port_conf.rx_adv_conf.rss_conf.rss_hf = dev_info.flow_type_rss_offloads;
    }

    /* Check if the number of requested RX descriptors is valid */
    if(nb_rx_desc > dev_info.rx_desc_lim.nb_max ||
            nb_rx_desc < dev_info.rx_desc_lim.nb_min ||
                nb_rx_desc % dev_info.rx_desc_lim.nb_align != 0) {
        LOG_ERR("Port %d cannot be configured with %d RX "\
                "descriptors per queue (min:%d, max:%d, align:%d)\n",
                port, nb_rx_desc, dev_info.rx_desc_lim.nb_min,
                dev_info.rx_desc_lim.nb_max, dev_info.rx_desc_lim.nb_align);
        return -EINVAL;
    }

    /* Enable jumbo frames */
    port_conf.rxmode.max_rx_pkt_len = dev_info.max_mtu;
    if (dev_info.rx_offload_capa & DEV_RX_OFFLOAD_JUMBO_FRAME)
        port_conf.rxmode.offloads |= DEV_RX_OFFLOAD_JUMBO_FRAME;

    /* Enable scatter gather */
    if (dev_info.rx_offload_capa & DEV_RX_OFFLOAD_SCATTER)
        port_conf.rxmode.offloads |= DEV_RX_OFFLOAD_SCATTER;

    /* Configure the Ethernet device. */
    retval = rte_eth_dev_configure(port, rx_queues, tx_queues, &port_conf);
    if (retval) {
        LOG_ERR("Cannot configure device; port=%u, err=%s \n", port, rte_strerror(-retval));
        return retval;
    }

    /* Allocate and set up RX queues. */
    rxq_conf = dev_info.default_rxconf;
    rxq_conf.offloads = port_conf.rxmode.offloads;
    if (flow_control)
        rxq_conf.rx_drop_en = 0;

    for (q = 0; q < rx_queues; q++) {
        retval = rte_eth_rx_queue_setup(port, q, nb_rx_desc, socket, &rxq_conf, mbuf_pools[q]);
        if (retval) {
            LOG_ERR("Cannot setup RX queue; port=%u, err=%s \n", port, rte_strerror(-retval));
            return retval;
        }
    }

    /* Allocate and set up TX queues */
    txq_conf = dev_info.default_txconf;
    txq_conf.offloads = port_conf.txmode.offloads;

    for (q = 0; q < tx_queues; q++) {
        retval = rte_eth_tx_queue_setup(port, q, TX_DESC_DEFAULT, socket, &txq_conf);
        if (retval) {
            LOG_ERR("Cannot setup TX queue; port=%u, err=%s \n", port, rte_strerror(-retval));
            return retval;
        }
    }

    /* Get link status */
    do {
        rte_eth_link_get_nowait(port, &link);
    } while (retry-- > 0 && !link.link_status && !sleep(1));

    // if still no link information, must be down
    if (!link.link_status) {
        LOG_ERR("Link down; port=%u \n", port);
        return -ENOLINK;
    }

    // enable promisc mode
    rte_eth_promiscuous_enable(port);

    // enable flow control
    retval = rte_eth_dev_flow_ctrl_get(port, &fc_conf);
    if (retval) {
        LOG_ERR("Cannot get flow control parameters; port=%u, err=%s \n", port, rte_strerror(-retval));
        return retval;
    }

    if (flow_control) {
        fc_conf.mode = RTE_FC_FULL;
        fc_conf.pause_time = 65535;
        fc_conf.send_xon = 0;
        fc_conf.mac_ctrl_frame_fwd = 1;
        fc_conf.autoneg = 0;
    }
    else
        fc_conf.mode = RTE_FC_NONE;

    retval = rte_eth_dev_flow_ctrl_set(port, &fc_conf);
    if (retval) {
        LOG_ERR("Cannot set flow control parameters; port=%u, err=%s \n", port, rte_strerror(-retval));
        return retval;
    }

    // start the receive and transmit units on the device
    retval = rte_eth_dev_start(port);
    if (retval < 0) {
        LOG_ERR("Cannot start device; port=%u, err=%s \n", port, rte_strerror(-retval));
        return retval;
    }

    return 0;
}
