/*
 * Copyright (c) 2021 Sandeep Mistry
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _PICO_RMII_ETHERNET_NETIF_H_
#define _PICO_RMII_ETHERNET_NETIF_H_

#include "hardware/pio.h"
#include "lwip/netif.h"

void arch_pico_init();
void arch_pico_info(struct netif *netif);

err_t netif_rmii_ethernet_init(struct netif *netif);

void netif_rmii_ethernet_poll();

void netif_rmii_ethernet_loop();

uint16_t netif_rmii_ethernet_mdio_read(uint addr, uint reg);
void netif_rmii_ethernet_mdio_write(uint addr, uint reg, uint val);

extern int phy_address;
#endif
