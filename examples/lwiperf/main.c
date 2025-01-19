/*
 * Copyright (c) 2024 Rob Scott, portions copyrighted as below:
 *
 * Copyright (c) 2021 Sandeep Mistry
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "lwip/dhcp.h"
#include "lwip/init.h"

#include "iperf.h"
#include "rmii_ethernet/netif.h"

void netif_link_callback(struct netif *netif) {
  printf("netif link status changed %s\n",
         netif_is_link_up(netif) ? "up" : "down");
}

void netif_status_callback(struct netif *netif) {
  printf("netif status changed %s\n", ip4addr_ntoa(netif_ip4_addr(netif)));
}

int main() {
  // LWIP network interface
  struct netif netif;

  // Do board specific init
  arch_pico_init();

  printf("&&& pico rmii ethernet - iperf\n");

  // Initilize LWIP in NO_SYS mode
  lwip_init();

  // Initialize the PIO-based RMII Ethernet network interface
  if (netif_rmii_ethernet_init(&netif) != ERR_OK) {
    printf("Failed to open ethernet interface\n");
    return -1;
  }

  // Report configuration
  arch_pico_info(&netif);
  
  // Assign callbacks for link and status
  netif_set_link_callback(&netif, netif_link_callback);
  netif_set_status_callback(&netif, netif_status_callback);

  // Set the default interface and bring it up
  netif_set_default(&netif);
  netif_set_up(&netif);

  // Start DHCP client and iperf
  dhcp_start(&netif);

  iperf_init();

  // Setup core 1 to monitor the RMII ethernet interface
  // This allows core 0 do other things :)
  multicore_launch_core1(netif_rmii_ethernet_loop);

  while (1) {
    tight_loop_contents();
  }

  return 0;
}
