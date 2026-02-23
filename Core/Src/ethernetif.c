/**
 ******************************************************************************
 * File Name          : ethernetif.c
 * Description        : This file provides code for the configuration
 *                      of the Target/ethernetif.c MiddleWare.
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by ST under Ultimate Liberty license
 * SLA0044, the "License"; You may not use this file except in compliance with
 * the License. You may obtain a copy of the License at:
 *                             www.st.com/SLA0044
 *
 ******************************************************************************
 */

/* Core/Src/ethernetif.c */
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "lwip/ethip6.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "ethernetif.h"

/* INCLUDE YOUR DRIVER */
#include "enc28j60.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

/* Define those to better describe your network interface. */
#define IFNAME0 'e'
#define IFNAME1 'n'

/* LINK TO YOUR MAIN HANDLE */
extern ENC_HandleTypeDef henc;
extern UART_HandleTypeDef huart2;
extern ENC_HandleTypeDef henc;

/* Helper buffer to flatten pbufs before sending to ENC */
static uint8_t eth_tx_buffer[1514];

/**
 * In this function, the hardware should be initialized.
 * Called from ethernetif_init().
 */
static void low_level_init(struct netif *netif)
{
	uint8_t mac[6] = {0x54, 0x55, 0x58, 0x10, 0x00, 0x24};

	  // 1. Tell LwIP what our MAC is
	  netif->hwaddr_len = ETHARP_HWADDR_LEN;
	  netif->hwaddr[0] = mac[0];
	  netif->hwaddr[1] = mac[1];
	  netif->hwaddr[2] = mac[2];
	  netif->hwaddr[3] = mac[3];
	  netif->hwaddr[4] = mac[4];
	  netif->hwaddr[5] = mac[5];

	  // 2. FORCE Hardware to match using the PUBLIC function
	  // We do NOT use enc_wrbreg here anymore.
	  enc_force_mac_hardware(&henc);

	  // 3. Set standard LwIP flags
	  netif->mtu = 1500;
	  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

	  // Debug Message
	  HAL_UART_Transmit(&huart2, (uint8_t*)"MAC Synced via Driver.\r\n", 24, 100);
}

/**
 * This function should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 */
static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
	const uint16_t MIN_FRAME_LEN = 60;
	  uint16_t len = p->tot_len;

	  /* 1. Flatten pbuf */
	  if (pbuf_copy_partial(p, eth_tx_buffer, len, 0) != len) {
	      return ERR_BUF;
	  }

	  /* 2. MANUAL PADDING (Critical for ARP) */
	  if (len < MIN_FRAME_LEN) {
	      memset(eth_tx_buffer + len, 0, MIN_FRAME_LEN - len);
	      len = MIN_FRAME_LEN;
	  }

	  /* 3. Prepare & Write */
	  if (enc_prepare_txbuffer(&henc, len) != 0) {
	      return ERR_IF;
	  }
	  enc_wrbuffer(eth_tx_buffer, len);
	  henc.transmitLength = len;

	  /* 4. Trigger Transmission */
	  enc_transmit(&henc);

	  return ERR_OK;
}

/**
 * Should allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 */
static struct pbuf * low_level_input(struct netif *netif)
{
	struct pbuf *p = NULL;
	  struct pbuf *q;
	  uint16_t len;
	  char debug_msg[64];

	  /* 1. Check if packet exists */
	  if (!enc_packet_receive_status(&henc)) {
	      return NULL; // Corrected: Return NULL if no packet
	  }

	  /* 2. Read Packet Length */
	  len = enc_get_packet_length(&henc);

	  /* DEBUG: Print Incoming Packet Info */
	  if (len > 70) {
//	      sprintf(debug_msg, "RX EVENT! Len: %d\r\n", len);
//	      HAL_UART_Transmit(&huart2, (uint8_t*)debug_msg, strlen(debug_msg), 100);
	  }

	  /* 3. Allocate Buffer */
	  if (len > 0) {
	      p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
	  }

	  /* 4. Read Payload into pbuf */
	  if (p != NULL) {
	      for (q = p; q != NULL; q = q->next) {
	          enc_rd_packet_payload(&henc, (uint8_t *)q->payload, q->len);
	      }

	      // Acknowledge that we finished reading
	      enc_read_packet_end(&henc);
	  } else {
	      // Allocation failed or length 0, but we must flush the packet from hardware
	      enc_read_packet_end(&henc);
	  }

	  return p; // Return the pbuf (or NULL) to LwIP
}

/**
 * This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface. Then the type of the received packet is determined and
 * the appropriate input function is called.
 */
void ethernetif_input(struct netif *netif)
{
  struct pbuf *p;

  // Poll the hardware
  p = low_level_input(netif);

  if (p != NULL)
  {
    // Send it to the LwIP stack
    if (netif->input(p, netif) != ERR_OK)
    {
      pbuf_free(p);
    }
  }
}

/**
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 */
err_t ethernetif_init(struct netif *netif)
{
  LWIP_ASSERT("netif != NULL", (netif != NULL));

#if LWIP_NETIF_HOSTNAME
  /* Initialize interface hostname */
  netif->hostname = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */

  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;

  /* We directly use etharp_output() here to save a function call.
   * You can instead declare your own output function. */
  netif->output = etharp_output;
  netif->linkoutput = low_level_output;

  /* initialize the hardware */
  low_level_init(netif);

  return ERR_OK;
}

/* USER CODE END 9 */
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

