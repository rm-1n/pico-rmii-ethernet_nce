/*
 * Copyright (c) Rob Scott, with portions copyrighted as below
 *
 * Copyright (c) 2021 Sandeep Mistry
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>

#include "lan8720a.h"

#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#include "hardware/sync.h" 

// For setting 1.8v threshold
#include "hardware/vreg.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/pads_bank0.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/unique_id.h"

#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"

#include "rmii_ethernet_phy_rx.pio.h"

// Select source of RMII clk: tx PIO program or LAN8720a module
// Edit rmii_ethernet_phy_rx.pio undefine/define the variable below
#ifdef GENERATE_RMII_CLK
#include "rmii_ethernet_phy_tx.pio.h"
#else
#include "rmii_ethernet_phy_tx_ext.pio.h"
#endif

#include "rmii_ethernet/netif.h"

// Uncomment to enable setting I/O thresholds to 1.8v
#define EN_1V8

// Select PIO to use for Ethernet
#define PICO_RMII_ETHERNET_PIO        pio0

// Uncomment to set MAC address
//#define PICO_RMII_ETHERNET_MAC_ADDR   {0xb8, 0x27, 0xeb, 0xde, 0xad, 0x00}

// Should be able to double buffer at least two full Ethernet frames
#define RX_BUF_SIZE_POW 12
#define RX_BUF_SIZE (1 << RX_BUF_SIZE_POW)
#define RX_BUF_MASK (RX_BUF_SIZE - 1)

// Make an aligned RX ring buffer
// Alignment allows the DMA engine to use wrapped addressing
static volatile uint8_t rx_ring[RX_BUF_SIZE] __attribute__((aligned (RX_BUF_SIZE)));

// Pointers to packets in the RX ring
// Needs to be large enough to hold the expected number of packets
// received while processing a max sized packet 
// The default assumption is that the receive buffer will contain
// 64 byte packets so we can just divide buffer size by 64 (i.e. 2^6)
#define RX_NUM_PTR_POW (RX_BUF_SIZE_POW - 6)
#define RX_NUM_PTR (1 << RX_NUM_PTR_POW)
#define RX_NUM_MASK (RX_NUM_PTR - 1)

typedef struct {
  uint16_t pkt_addr;  // Ring buffer address of packet
  uint16_t pkt_len;   // Length of packet in bytes
} pkt_ptr_t;


// Written by ISR, read by packet processing routine
static volatile pkt_ptr_t rx_pkt_ptr[RX_NUM_PTR];
static volatile uint32_t rx_curr_pkt_ptr = 0;

// Start of current packet. Used only by ISR
static uint32_t rx_addr = 0;

// Used by ethernet_poll()
static uint32_t rx_prev_pkt_ptr = 0;

// Max Ethernet frame size is:
// mac src + mac dst + type + payload + crc
//    6         6        2      1500     4 = 1518
//
// For full overlap, need to hold at least two full 1518 byte Ethernet frames
// Specify size in bytes, so 4096 is 2^12
#define TX_BUF_SIZE_POW 12
#define TX_BUF_SIZE (1 << TX_BUF_SIZE_POW)
#define TX_BUF_MASK (TX_BUF_SIZE - 1)

// Make an aligned TX ring buffer
// Alignment allows the DMA engine to use wrapped addressing
static volatile uint8_t tx_ring[TX_BUF_SIZE] __attribute__((aligned (TX_BUF_SIZE)));

// Pointers to length of the packets in the TX ring
// Should be enough to hold the maximum number of minimum sized packets
// i.e. 4096/64 = 64 * 4 bytes, or 2^(6 + 2)
#define TX_NUM_PTR_POW (TX_BUF_SIZE_POW - 6)
#define TX_NUM_PTR (1 << TX_NUM_PTR_POW)
#define TX_NUM_MASK (TX_NUM_PTR - 1)
// Above, in bytes
#define TX_NUM_PTR_POW_BYTES (TX_NUM_PTR_POW + 2)

// Holds transmit packet length
// Must be aligned to be used as a ring buffer
static volatile uint32_t tx_pkt_ptr[TX_NUM_PTR] __attribute__((aligned (TX_NUM_PTR)));

// Tx ring buffer management - used by ethernet output routine
volatile uint32_t tx_addr = 0;
volatile uint32_t tx_curr_pkt_ptr = 0;

static struct netif *rmii_eth_netif;

static uint rx_sm_offset;
static uint tx_sm_offset;

static int rx_dma_chan;
static int rx_chain_chan;
static int tx_dma_chan;
static int tx_chain_chan;

static dma_channel_config rx_dma_channel_config;
static dma_channel_config rx_chain_channel_config;
static dma_channel_config tx_dma_channel_config;
static dma_channel_config tx_chain_channel_config;

static int pbuf_chan;
static dma_channel_config pbuf_rx_channel_config;
static dma_channel_config pbuf_tx_channel_config;
static dma_channel_config pbuf_tx_no_inc_channel_config;

// Reload the RX DMA engine with this value
uint32_t rx_ctl_reload;

// LAN8720 PHY address
int phy_address = 0xffff;

// Right shift CRC check value, complemented
// See https://en.wikipedia.org/wiki/Ethernet_frame#Frame_check_sequence
static const uint32_t crc_check_value = 0xdebb20e3;

// Select one of the two CRC calculation methods below
// Enable using the DMA sniffer for CRC calculations
#define USE_DMA_CRC

// Enable using the CPU for CRC calculations
//#define USE_CPU_CRC

#ifdef USE_CPU_CRC
static uint32_t crc32Lookup[256] =
{ 0x00000000,0x77073096,0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,
  0x0EDB8832,0x79DCB8A4,0xE0D5E91E,0x97D2D988,0x09B64C2B,0x7EB17CBD,0xE7B82D07,0x90BF1D91,
  0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,
  0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,
  0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,
  0x35B5A8FA,0x42B2986C,0xDBBBC9D6,0xACBCF940,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,
  0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F4B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,
  0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,
  0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
  0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0DBB,0x086D3D2D,0x91646C97,0xE6635C01,
  0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,
  0x65B0D9C6,0x12B7E950,0x8BBEB8EA,0xFCB9887C,0x62DD1DDF,0x15DA2D49,0x8CD37CF3,0xFBD44C65,
  0x4DB26158,0x3AB551CE,0xA3BC0074,0xD4BB30E2,0x4ADFA541,0x3DD895D7,0xA4D1C46D,0xD3D6F4FB,
  0x4369E96A,0x346ED9FC,0xAD678846,0xDA60B8D0,0x44042D73,0x33031DE5,0xAA0A4C5F,0xDD0D7CC9,
  0x5005713C,0x270241AA,0xBE0B1010,0xC90C2086,0x5768B525,0x206F85B3,0xB966D409,0xCE61E49F,
  0x5EDEF90E,0x29D9C998,0xB0D09822,0xC7D7A8B4,0x59B33D17,0x2EB40D81,0xB7BD5C3B,0xC0BA6CAD,
  0xEDB88320,0x9ABFB3B6,0x03B6E20C,0x74B1D29A,0xEAD54739,0x9DD277AF,0x04DB2615,0x73DC1683,
  0xE3630B12,0x94643B84,0x0D6D6A3E,0x7A6A5AA8,0xE40ECF0B,0x9309FF9D,0x0A00AE27,0x7D079EB1,
  0xF00F9344,0x8708A3D2,0x1E01F268,0x6906C2FE,0xF762575D,0x806567CB,0x196C3671,0x6E6B06E7,
  0xFED41B76,0x89D32BE0,0x10DA7A5A,0x67DD4ACC,0xF9B9DF6F,0x8EBEEFF9,0x17B7BE43,0x60B08ED5,
  0xD6D6A3E8,0xA1D1937E,0x38D8C2C4,0x4FDFF252,0xD1BB67F1,0xA6BC5767,0x3FB506DD,0x48B2364B,
  0xD80D2BDA,0xAF0A1B4C,0x36034AF6,0x41047A60,0xDF60EFC3,0xA867DF55,0x316E8EEF,0x4669BE79,
  0xCB61B38C,0xBC66831A,0x256FD2A0,0x5268E236,0xCC0C7795,0xBB0B4703,0x220216B9,0x5505262F,
  0xC5BA3BBE,0xB2BD0B28,0x2BB45A92,0x5CB36A04,0xC2D7FFA7,0xB5D0CF31,0x2CD99E8B,0x5BDEAE1D,
  0x9B64C2B0,0xEC63F226,0x756AA39C,0x026D930A,0x9C0906A9,0xEB0E363F,0x72076785,0x05005713,
  0x95BF4A82,0xE2B87A14,0x7BB12BAE,0x0CB61B38,0x92D28E9B,0xE5D5BE0D,0x7CDCEFB7,0x0BDBDF21,
  0x86D3D2D4,0xF1D4E242,0x68DDB3F8,0x1FDA836E,0x81BE16CD,0xF6B9265B,0x6FB077E1,0x18B74777,
  0x88085AE6,0xFF0F6A70,0x66063BCA,0x11010B5C,0x8F659EFF,0xF862AE69,0x616BFFD3,0x166CCF45,
  0xA00AE278,0xD70DD2EE,0x4E048354,0x3903B3C2,0xA7672661,0xD06016F7,0x4969474D,0x3E6E77DB,
  0xAED16A4A,0xD9D65ADC,0x40DF0B66,0x37D83BF0,0xA9BCAE53,0xDEBB9EC5,0x47B2CF7F,0x30B5FFE9,
  0xBDBDF21C,0xCABAC28A,0x53B39330,0x24B4A3A6,0xBAD03605,0xCDD70693,0x54DE5729,0x23D967BF,
  0xB3667A2E,0xC4614AB8,0x5D681B02,0x2A6F2B94,0xB40BBE37,0xC30C8EA1,0x5A05DF1B,0x2D02EF8D
};
#endif

uint32_t count = 10;

// Fetch data from ring buffer, calculate CRC, write data to destination pbuf
// Return length (valid) or zero (invalid CRC)
static uint __not_in_flash_func(ethernet_frame_to_pbuf)
     (volatile uint8_t *data, 
      struct pbuf *buf,
      int len, int addr) {
  
  uint crc = 0xffffffff;  /* Initial value. */
  uint index = 0;
  uint inverted_crc;
  struct pbuf *p;
  size_t buf_copy_len;
  size_t total_copy_len = len;
  uint32_t i, j;
  uint8_t curr_data;
  uint32_t lsb;
  uint32_t offset;
  uint8_t *wr_ptr;

#ifdef USE_DMA_CRC    
  dma_channel_wait_for_finish_blocking(pbuf_chan);
  dma_hw->sniff_data = 0xffffffff;
#endif

  // This is from LWIP's pbuf.c, pbuf_take(...) routine
  for (p = buf; total_copy_len != 0; p = p->next) {
    wr_ptr = (uint8_t *)p->payload;

    buf_copy_len = total_copy_len;
    if (buf_copy_len > p->len) {
      /* this pbuf cannot hold all remaining data */
      buf_copy_len = p->len;
    }

#ifdef USE_DMA_CRC
    // Setup DMA to copy this portion of the pbuf
    dma_channel_wait_for_finish_blocking(pbuf_chan);
    dma_channel_hw_addr(pbuf_chan)->read_addr = (uint32_t)&(data[addr]);
    dma_channel_hw_addr(pbuf_chan)->write_addr = (uint32_t)(wr_ptr);
    dma_channel_hw_addr(pbuf_chan)->transfer_count = buf_copy_len;

    // Fire it off
    dma_channel_set_config(pbuf_chan, &pbuf_rx_channel_config, true);

    addr = (addr + buf_copy_len) & RX_BUF_MASK;
    total_copy_len -= buf_copy_len;
#endif

#ifdef USE_CPU_CRC
    // Calculate CRC over payload
    for (i = 0; i < buf_copy_len; i++) {
      // Get data from ring buffer
      curr_data = data[addr];

      // Wrap address around ring buffer
      addr = (addr + 1) & RX_BUF_MASK;

      // Save data
      *wr_ptr++ = curr_data;

      // Include packet CRC (i.e. last 4 bytes) in CRC calculation
      if (total_copy_len > 0) {
	// Calculate CRC
	offset = (crc & 0xff) ^ curr_data;
	crc = (crc >> 8) ^ crc32Lookup[offset];
      }

      total_copy_len--;
    }
#endif
  }

#ifdef USE_DMA_CRC
  dma_channel_wait_for_finish_blocking(pbuf_chan);
  crc = dma_hw->sniff_data;
#endif

  // Compare CRC against check value
  if (crc != crc_check_value) len = 0;

  return len;
}

// Copy packet data to ring buffer, adding pkt len, and CRC, for transmission
// Assumes space availability check done before calling this function
// Returns final length, including pkt len and CRC
static uint __not_in_flash_func(ethernet_frame_copy_ring_pbuf)
     (volatile uint8_t *data, 
      struct pbuf *p,
      int addr) {
  uint crc = 0xffffffff;  /* Initial value. */
  uint inverted_crc;
  uint8_t buf_dat;
  uint32_t tot_len = 0;
  int j;
  int i;
  uint32_t lsb;
  uint8_t offset;

#ifdef USE_DMA_CRC    
  // Make sure we've finished previous transaction
  dma_channel_wait_for_finish_blocking(pbuf_chan);
  dma_hw->sniff_data = 0xffffffff;
#endif

  // Add length parameter space to start of buffer
  uint32_t p_addr = addr;
  addr = (addr + 2) & TX_BUF_MASK;

  // Get the payload from lwip, generating CRC along the way
  for (struct pbuf *q = p; q != NULL; q = q->next) {
#ifdef USE_DMA_CRC
    // Setup DMA to copy this portion of the pbuf
    dma_channel_wait_for_finish_blocking(pbuf_chan);
    dma_channel_hw_addr(pbuf_chan)->read_addr = (uint32_t)(q->payload);
    dma_channel_hw_addr(pbuf_chan)->write_addr = (uint32_t)(&data[addr]);
    dma_channel_hw_addr(pbuf_chan)->transfer_count = q->len;
    dma_channel_set_config(pbuf_chan, &pbuf_tx_channel_config, true);

    // Wrap address around ring buffer
    addr = (addr + q->len) & TX_BUF_MASK;
    tot_len += q->len;
#endif

#ifdef USE_CPU_CRC
    for (j = 0; j < q->len; j++) {
      buf_dat = *(uint8_t *)(q->payload++);
      data[addr] = buf_dat;
      tot_len++;

      // Wrap address around ring buffer
      addr = (addr + 1) & TX_BUF_MASK;

      // Accumulate CRC
      offset = (crc & 0xff) ^ buf_dat;
      crc = (crc >> 8) ^ crc32Lookup[offset];
    }
#endif
  }    

#ifdef USE_CPU_CRC
  // Make sure we have enough bytes to meet minimum packet size, minus CRC
  while (tot_len < 60) {
    buf_dat = 0; // padding byte
    data[addr] = buf_dat;
    tot_len++;

    // Wrap address around ring buffer
    addr = (addr + 1) & TX_BUF_MASK;

    // Accumulate CRC
    offset = (crc & 0xff) ^ buf_dat;
    crc = (crc >> 8) ^ crc32Lookup[offset];
  }
#endif

#ifdef USE_DMA_CRC
  // Make sure we have enough bytes to meet minimum packet size, minus CRC
  // Note that we push the fill bytes through the DMA engine in order
  // to include them in the DMA sniffer CRC calculation
  if (tot_len < 60) {
    uint32_t zero = 0;
    uint32_t remainder = 60 - tot_len;

    dma_channel_wait_for_finish_blocking(pbuf_chan);
    dma_channel_hw_addr(pbuf_chan)->read_addr = (uint32_t)(&zero);
    dma_channel_hw_addr(pbuf_chan)->transfer_count = remainder;

    // Fire off buffer fill
    dma_channel_set_config(pbuf_chan, &pbuf_tx_no_inc_channel_config, true);

    // Wrap address around ring buffer
    addr = (addr + remainder) & TX_BUF_MASK;
    tot_len += remainder;
  }

  // Wait for transfer to complete, and save final CRC
  dma_channel_wait_for_finish_blocking(pbuf_chan);
  crc = dma_hw->sniff_data;
#endif

  // Insert CRC into packet
  inverted_crc = ~crc;

  for (int i = 0; i < 4; i++) {
    data[addr] = inverted_crc >> (i * 8);
    // Wrap address around ring buffer
    addr = (addr + 1) & TX_BUF_MASK;
  }

  // Add CRC len
  tot_len += 4;

  // Compute packet length dibits - 1 for PIO transmit loop
  uint16_t pkt_len = (tot_len * 4) - 1;

  // Save packet length for PIO transmit at start of packet
  data[p_addr] = pkt_len;
  p_addr = (p_addr + 1) & TX_BUF_MASK;
  data[p_addr] = pkt_len >> 8;

  // Add parameter len to buffer occupancy
  tot_len += 2;

  return tot_len;
}


// MDIO state machine definitions
enum md_states {
  MD_IDLE, MD_START, MD_PREAMB, MD_SOF, MD_OPCODE,
  MD_PHY_ADDR, MD_REG_ADDR, MD_TURN, MD_DATA
};

// Read or write flag. Read or write during MD_DATA state, else write
enum md_pin_states {
  MD_READ, MD_WRITE
};

// Read/write MDIO on MDC falling edge
static uint32_t md_clocks;               // Number of clocks for this state
static uint32_t md_data;                 // Data to send to MDIO pin
static enum md_states md_state;          // Current state
static enum md_pin_states md_pin_state;  // Current input/output MDIO pin state

// Parameters for read/write routines to pass to ISR
static uint32_t md_phy_addr;             // Phy to read or write
static uint32_t md_reg_addr;             // Register to read or write
static enum md_pin_states md_rd_wr;      // Read or write
static uint16_t md_rd_data;              // Data from MDIO pin during data state
static uint32_t md_wr_data;              // Data to MDIO during data state
// Set by ISR, read by non-ISR
volatile static uint32_t md_rd_return;   // Data from data state stored here
volatile static uint32_t md_last_addr;   // Address of last register read
volatile static uint32_t md_sm_busy = 0; // Set by caller, cleared by MD_IDLE

static void md_sm(void);

#ifdef MD_STATE_DEBUG
static void state_alpha(enum md_pin_states state) {
  switch (state) {
  case MD_IDLE:     printf("MD_IDLE    "); break;
  case MD_START:    printf("MD_START   "); break;
  case MD_PREAMB:   printf("MD_PREAMB  "); break; 
  case MD_SOF:      printf("MD_SOF     "); break;
  case MD_OPCODE:   printf("MD_OPCODE  "); break;
  case MD_PHY_ADDR: printf("MD_PHY_ADDR"); break;
  case MD_REG_ADDR: printf("MD_REG_ADDR"); break;
  case MD_TURN:     printf("MD_TURN    "); break;
  case MD_DATA:     printf("MD_DATA    "); break;
  };
}
#endif

// Interrupt service routine, called on falling edge of MD clock 
// Pushes command bits during non MD_DATA states, read/writes during.
// Note: MSB data is sent first
static void netif_rmii_ethernet_mdc_falling() {
  uint32_t bit;

#ifdef MD_STATE_DEBUG
  state_alpha(md_state);
  printf(" Istate/clk/entry/exit pin/rd: %d %d %d ", md_state, md_clocks,
	 gpio_get(PICO_RMII_ETHERNET_MDIO_PIN));
#endif

  // Set pin direction for all states before read/writing data
  if (md_pin_state == MD_READ) {
    gpio_set_dir(PICO_RMII_ETHERNET_MDIO_PIN, GPIO_IN);
  } else {
    gpio_set_dir(PICO_RMII_ETHERNET_MDIO_PIN, GPIO_OUT);
  }

  // Accumulate data during data read state
  if ((md_state == MD_DATA) && (md_pin_state == MD_READ)) {
    // Get bit, put into read data
    bit = gpio_get(PICO_RMII_ETHERNET_MDIO_PIN);
    md_rd_data = (md_rd_data << 1) | bit;
  } else {
    // Otherwise, get MSB from write data, send to chip
    bit = (md_data & (1 << (md_clocks - 1))) ? 1 : 0;
    gpio_put(PICO_RMII_ETHERNET_MDIO_PIN, bit);
  }

  // Bump clocks for this state
  md_clocks--;

  // If done with this set of clocks, do state transistion
  if (md_clocks == 0) {
    // Actions to be done at last clock of a given MD_STATE
    if (md_state == MD_DATA) {
      if (md_pin_state == MD_READ) {
	// Save read register address
	md_last_addr = md_reg_addr;
	// Save data
	md_rd_return = md_rd_data;
      } else {
      // If we did a write to the saved address, invalidate it
	if (md_reg_addr == md_last_addr) {
	  md_reg_addr = -1;
	}
      }
      // Signal SM done
      md_sm_busy = 0;
#ifdef MD_STATE_DEBUG
      printf("md_rd_return: rd/wr data: %d %04x\n", md_rd_wr, md_rd_return);
#endif
    }

    if (md_state == MD_IDLE) {
      // No more edges
      gpio_set_irq_enabled_with_callback(PICO_RMII_ETHERNET_MDC_PIN, 
					 GPIO_IRQ_EDGE_FALL, false, 
					 netif_rmii_ethernet_mdc_falling);
    } 

    // Get next state
    md_sm();
  }
}

// Walk through MD states after each set of falling edge events
static void md_sm() {

  switch(md_state) {
    // If idle, stay there
  case MD_IDLE:
    md_state = MD_IDLE;
    md_data = 1;
    md_clocks = 1;
    md_pin_state = MD_WRITE;
    break;

    // Start up SM, first do preamble
    // Enable falling edge ISR
  case MD_START:
    md_state = MD_PREAMB;
    md_data = 0xffffffff;
    md_clocks = 32;
    md_pin_state = MD_WRITE;
    gpio_set_irq_enabled_with_callback(PICO_RMII_ETHERNET_MDC_PIN, 
				       GPIO_IRQ_EDGE_FALL, true, 
				       netif_rmii_ethernet_mdc_falling);
    break;

    // Done with preamble, start SOF
  case MD_PREAMB:
    md_state = MD_SOF;
    md_data = 0b01;
    md_clocks = 2;
    md_pin_state = MD_WRITE;
    break;

    // Done with SOF, start opcode
  case MD_SOF:
    md_state = MD_OPCODE;
    md_data = (md_rd_wr == MD_READ) ? 0b10: 0b01;
    md_clocks = 2;
    md_pin_state = MD_WRITE;
    break;

    // Done with OPCODE, start phy address
  case MD_OPCODE:
    md_state = MD_PHY_ADDR;
    md_data = md_phy_addr;
    md_clocks = 5;
    md_pin_state = MD_WRITE;
    break;

    // Done with phy address, start reg address
  case MD_PHY_ADDR:
    md_state = MD_REG_ADDR;
    md_data = md_reg_addr;
    md_clocks = 5;
    md_pin_state = MD_WRITE;
    break;

    // Done with reg address, start data phase turn around
  case MD_REG_ADDR:
    md_state = MD_TURN;
    md_data = 0b00;
    md_clocks = 2;
    md_pin_state = md_rd_wr;
    break;

    // Done with data phase turn around, start data phase
  case MD_TURN:
    md_state = MD_DATA;
    md_data = md_wr_data;
    md_clocks = 16;
    md_pin_state = md_rd_wr;
    break;

    // Done with data phase, start idle
  case MD_DATA:
    md_state = MD_IDLE;
    md_data = 0;
    md_clocks = 1;
    md_pin_state = MD_WRITE;

    break;
  }

}

// Start MDIO state machine if idle, otherwise wait
int md_sm_start(uint addr, uint reg, uint val, 
		enum md_pin_states rd_wr, uint blk) {

  // If busy and non-blocking, return with not started flag
  if ((md_sm_busy == 1) && (blk == 0)) return -1;
  
  // else wait until not busy
  while (md_sm_busy == 1) {
    tight_loop_contents();
  }

  // Start sm
  // Set busy to lock SM
  md_sm_busy = 1;

  // Set up parameters for ISR
  md_phy_addr = addr;
  md_reg_addr = reg;
  md_wr_data = val;
  md_rd_wr = rd_wr;
  md_state = MD_START;
  md_last_addr = -1;

  // Kick off state machine
  md_sm();

  // If blocking , wait until not busy
  if (blk) {
    while (md_sm_busy == 1) {
      tight_loop_contents();
    }
  }    

  // Signal SM successfully started
  return 0; 
}  
  

// Non-blocking MDIO read, return either previous value read or -1
uint32_t netif_rmii_ethernet_mdio_read_nb(uint addr, uint reg) {
  uint32_t ret_val;

  // See if we've read this location previously
  if (reg == md_last_addr) {
    ret_val = md_rd_return;
  } else {
    ret_val = -1;
  }

  // Start mdio to update read data for next time
  md_sm_start(addr, reg, 0, MD_READ, 0);

  return ret_val;
}


// Blocking MDIO read
uint16_t netif_rmii_ethernet_mdio_read(uint addr, uint reg) {

  // Kick off state machine, wait for end
  md_sm_start(addr, reg, 0, MD_READ, 1);

  // Lower bits of response contain register data
  return (uint16_t)md_rd_return;
}

// Non-blocking MDIO write
void netif_rmii_ethernet_mdio_write_nb(uint addr, uint reg, uint val) {

  // Start mdio, might not actually write if it's busy
  md_sm_start(addr, reg, val, MD_WRITE, 0);

}

// Blocking MDIO write
void netif_rmii_ethernet_mdio_write(uint addr, uint reg, uint val) {
  
  // Start mdio, wait until write completes
  md_sm_start(addr, reg, val, MD_WRITE, 1);

}

uint32_t max_cmd = 5;

// Get packet from pbuf, add CRC, put in DMA buffer for transmit
static err_t netif_rmii_ethernet_output(struct netif *netif, struct pbuf *p) {
  uint32_t curr_cmd;
  uint32_t tx_next_pkt_ptr;

  // Test to see if there's space in the buffer for the packet
  // Pbuf length does not include CRC bytes, nor pkt length bytes
  uint32_t plen = p->tot_len + 4 + 2;

  // Nor does it pad to minimum Ethernet frame size + pkt length bytes
  if (plen < 66) plen = 66;

  // Make Tx read addr into ring buffer index
  uint32_t curr_rd = (dma_hw->ch[tx_dma_chan].read_addr) & TX_BUF_MASK;
      
  // Get current ring buffer write address index
  uint32_t curr_wr = tx_addr;

  // Calculate free space available
  uint32_t tx_buf_wrap = (curr_rd ^ curr_wr) & TX_BUF_MASK;
  uint32_t tx_free = (TX_BUF_SIZE -
		      (tx_buf_wrap ? ((curr_rd - curr_wr) & TX_BUF_MASK) :
		       (curr_wr - curr_rd)));

  // Wait for space in buffer
  while (plen > tx_free) {
    sleep_us(10);
    curr_rd = (dma_hw->ch[tx_dma_chan].read_addr) & TX_BUF_MASK;
    tx_buf_wrap = (curr_rd ^ curr_wr) & TX_BUF_MASK;
    tx_free = (TX_BUF_SIZE -
	       (tx_buf_wrap ? ((curr_rd - curr_wr) & TX_BUF_MASK) :
		(curr_wr - curr_rd)));
  }

  // Push frame into ring buffer
  uint32_t len = ethernet_frame_copy_ring_pbuf(tx_ring, p, tx_addr);

  // Bump frame ring buffer addr
  tx_addr = (tx_addr + len) & TX_BUF_MASK;

#ifdef CMD_PKT_DEBUG
  // Get current cmd read and cmd write pointers
  curr_cmd = ((dma_hw->ch[tx_chain_chan].read_addr) >> 2) & TX_NUM_MASK;
  tx_next_pkt_ptr = (tx_curr_pkt_ptr + 1) & TX_NUM_MASK;

  // Compute number of items in command buffer, including EOC
  tx_buf_wrap = (tx_next_pkt_ptr ^ curr_cmd) & TX_NUM_MASK;
  tx_free = (tx_buf_wrap ? ((tx_next_pkt_ptr - curr_cmd) & TX_NUM_MASK) :
	     (tx_next_pkt_ptr - curr_cmd));

  if (tx_free > max_cmd) {
    printf("%d ", tx_free);
    max_cmd = tx_free;
  }
#endif

  // Insert new packet into command list, saving DMA status before/after
  // Generate next packet pointer for EOC (cmd write)
  tx_next_pkt_ptr = (tx_curr_pkt_ptr + 1) & TX_NUM_MASK;

  // Put end of commands (EOC) into command ring, after new command
  tx_pkt_ptr[tx_next_pkt_ptr] = 0;

  // Write new command into cmd ring buffer, with interrupts disabled
  uint32_t irq_save = save_and_disable_interrupts();
  uint32_t before_stat = dma_channel_is_busy(tx_dma_chan);
  tx_pkt_ptr[tx_curr_pkt_ptr] = len;
  uint32_t after_stat = dma_channel_is_busy(tx_dma_chan);

  // Sample busy again if we were busy before and not now
  // This catches the packet DMA idle between chained DMAs case
  if (before_stat && !after_stat) {
    sleep_us(1);
    after_stat = dma_channel_is_busy(tx_dma_chan);
  }
  restore_interrupts(irq_save);

  // Check to see if new command will be executed
  // Case 1: DMA busy before/busy after -> yes, nothing needs to be done
  // Case 2: DMA not busy before/not busy after -> no, start cmd queue DMA
  // Case 3: DMA busy before/not busy after -> no, start cmd queue DMA
  // Case 4: DMA not busy before/busy after -> yes, nothing needs to be done

  // Handle case 3
  // Occurs if we hit the idle time between chained DMAs
  // In this case, the DMA should start up when the chain is completed
  // Hopefully, this will mitigated by the double sampling done above
  if (before_stat && !after_stat) {
    sleep_us(1);
    // Start cmds, only if it really is idle (not likely)
    if (!dma_channel_is_busy(tx_dma_chan)) {
      dma_channel_hw_addr(tx_chain_chan)->al3_read_addr_trig =
	  (uint32_t)&(tx_pkt_ptr[tx_curr_pkt_ptr]);
    }
  }
  
  // Handle case 2
  // Startup when really idle
  if (!before_stat && !after_stat) {
    dma_channel_hw_addr(tx_chain_chan)->al3_read_addr_trig =
      (uint32_t)&(tx_pkt_ptr[tx_curr_pkt_ptr]);
  }

  // Bump command ring buffer address
  tx_curr_pkt_ptr = tx_next_pkt_ptr;

  return ERR_OK;
}

// Do end of received packet processing
// Time critical - must be in SRAM, otherwise we get CRC errors
static void __not_in_flash_func(netif_rmii_ethernet_eof_isr)() {
  uint32_t prev_rx_addr;
  uint32_t rx_packet_byte_count;

  // Save old write address (aka start of current packet)
  prev_rx_addr = rx_addr;

  // Save new write address (aka start of next packet)
  rx_addr = (uint32_t)dma_hw->ch[rx_dma_chan].write_addr-(uint32_t)&rx_ring[0];

  // Do wrapped length calc
  if (rx_addr < prev_rx_addr) {
    rx_packet_byte_count = (RX_BUF_SIZE + rx_addr) - prev_rx_addr;
  } else {
    rx_packet_byte_count = rx_addr - prev_rx_addr;
  }

  // Only save packets with good length
  if ((rx_packet_byte_count > 63) && (rx_packet_byte_count < 1519)) {
    // Save start/len in packet pointer ring buffer
    rx_pkt_ptr[rx_curr_pkt_ptr].pkt_addr = prev_rx_addr;
    rx_pkt_ptr[rx_curr_pkt_ptr].pkt_len = rx_packet_byte_count;

    // Bump pointer
    rx_curr_pkt_ptr = (rx_curr_pkt_ptr + 1) & RX_NUM_MASK;
  }

  // Clear PIO received packet flag
  pio_interrupt_clear(PICO_RMII_ETHERNET_PIO, 0);

}


void arch_pico_init() {

#ifdef PICO_RMII_ETHERNET_RST_PIN
  // Assert LAN8720a reset
  gpio_init(PICO_RMII_ETHERNET_RST_PIN);
  gpio_put(PICO_RMII_ETHERNET_RST_PIN, 0);
  gpio_set_dir(PICO_RMII_ETHERNET_RST_PIN, GPIO_OUT);
#endif

#ifdef PICO_RMII_ETHERNET_PWR_PIN
  // Turn off LAN8720a power
  gpio_init(PICO_RMII_ETHERNET_PWR_PIN);
  gpio_set_dir(PICO_RMII_ETHERNET_PWR_PIN, GPIO_OUT);
  gpio_put(PICO_RMII_ETHERNET_PWR_PIN, 0);
#endif

#ifdef GENERATE_RMII_CLK
  // Set up system clock
  //uint32_t target_clk = 100000000;
  //uint32_t target_clk = 125000000; 
  //uint32_t target_clk = 150000000; 
  //uint32_t target_clk = 200000000;
  //uint32_t target_clk = 250000000;
  //uint32_t target_clk = 300000000;

  //set_sys_clock_khz(target_clk/1000, true);

  // Enable a bit of a voltage boost when overclocking
  //vreg_set_voltage(VREG_VOLTAGE_1_10);
  //vreg_set_voltage(VREG_VOLTAGE_1_15);
  //vreg_set_voltage(VREG_VOLTAGE_1_20);
  //vreg_set_voltage(VREG_VOLTAGE_1_25);
  //vreg_set_voltage(VREG_VOLTAGE_1_30);
  
#else
  // Must have 6 sysclks per RMII clock for sampling RMII bus properly
  uint32_t target_clk = 300000000;
  vreg_set_voltage(VREG_VOLTAGE_1_20);
  set_sys_clock_khz(target_clk/1000, true);
#endif

#ifdef EN_1V8
  // Set 1.8v threshold for I/O pads
  io_rw_32* addr = (io_rw_32 *)(PADS_BANK0_BASE + PADS_BANK0_VOLTAGE_SELECT_OFFSET);
  *addr = PADS_BANK0_VOLTAGE_SELECT_VALUE_1V8 << PADS_BANK0_VOLTAGE_SELECT_LSB;
#endif

  // Let clock settle
  sleep_ms(10);

  // Initialize stdio after the clock change
  // RP2XXX stdio initialization takes around 2 seconds
  stdio_init_all();
  sleep_ms(2000);

}

void arch_pico_info(struct netif *netif) {

  printf("mac: %02x:%02x:%02x:%02x:%02x:%02x\n",
	 netif->hwaddr[0], netif->hwaddr[1], netif->hwaddr[2],
	 netif->hwaddr[3], netif->hwaddr[4], netif->hwaddr[5]
	 );

  printf("System clk: %4.2f MHz\n", (float)clock_get_hz(clk_sys)/1e6);

  printf("phy addr: %d\n", phy_address);

#ifdef REPORT_BUF_SIZE
  printf("rx buf start/end/size: %08x %08x %d\n", (uint32_t)&rx_ring[0],
	 (uint32_t)&rx_ring[RX_BUF_MASK],
	 (uint32_t)&rx_ring[RX_BUF_MASK] - (uint32_t)&rx_ring[0] + 1);	 

  printf("rx ptr start/end/size: %08x %08x %d\n", (uint32_t)&rx_pkt_ptr[0],
	 (uint32_t)&rx_pkt_ptr[RX_NUM_MASK],
	 ((uint32_t)&rx_pkt_ptr[RX_NUM_MASK + 1] - (uint32_t)&rx_pkt_ptr[0])/
	 sizeof(rx_pkt_ptr[0]));

  printf("tx buf start/end/size: %08x %08x %d\n", (uint32_t)&tx_ring[0],
	 (uint32_t)&tx_ring[TX_BUF_MASK],
	 (uint32_t)&tx_ring[TX_BUF_MASK] - (uint32_t)&tx_ring[0] + 1);	 

  printf("tx ptr start/end/size: %08x %08x %d\n", (uint32_t)&tx_pkt_ptr[0],
	 (uint32_t)&tx_pkt_ptr[TX_NUM_MASK],
	 ((uint32_t)&tx_pkt_ptr[TX_NUM_MASK + 1] - (uint32_t)&tx_pkt_ptr[0])/
	 sizeof(tx_pkt_ptr[0]));
#endif

#ifdef GENERATE_RMII_CLK
  printf("Setup to generate RMII clock on GPIO %d\n",
	 PICO_RMII_ETHERNET_RETCLK_PIN);
#if !defined(PICO_RMII_ETHERNET_RST_PIN) && !defined(PICO_RMII_ETHERNET_PWR_PIN)
  printf("Warning: no GPIO controlled LAN8720a reset pin. Operation maybe erratic.\n");
#endif
#else
  printf("Setup to receive RMII clock on GPIO %d\n",
	 PICO_RMII_ETHERNET_RETCLK_PIN);
#endif
  
#ifdef PICO_RMII_ETHERNET_RST_PIN
  printf("LAN8720a reset pin connected to GPIO %d\n",
	 PICO_RMII_ETHERNET_RST_PIN);
#endif

#ifdef PICO_RMII_ETHERNET_PWR_PIN
  printf("LAN8720a power pin connected to GPIO %d\n",
	 PICO_RMII_ETHERNET_PWR_PIN);
#endif

}


static err_t netif_rmii_ethernet_low_init(struct netif *netif) {

  // Prepare the interface
  rmii_eth_netif = netif;

  netif->linkoutput = netif_rmii_ethernet_output;
  netif->output     = etharp_output;
  netif->mtu        = 1500; 
  netif->flags      = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
    NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP | NETIF_FLAG_MLD6;

  // Setup the MAC address
#ifdef PICO_RMII_ETHERNET_MAC_ADDR
  uint8_t mac[6] = PICO_RMII_ETHERNET_MAC_ADDR;
  memcpy(netif->hwaddr, mac, 6);
#else
  // Generate MAC from unique board id
  pico_unique_board_id_t board_id;
  pico_get_unique_board_id(&board_id);

  // Set default MAC identifier to Raspberry Pi Foundation
  netif->hwaddr[0] = 0xb8;
  netif->hwaddr[1] = 0x27;
  netif->hwaddr[2] = 0xeb;
  memcpy(&netif->hwaddr[3], &board_id.id[5], 3);
#endif  

  netif->hwaddr_len = ETH_HWADDR_LEN;

  // Init TX command buffer
  for (int i = 0; i < TX_NUM_PTR; i++) {
    tx_pkt_ptr[i] = 0;
  }

  // Init the RMII PIO programs
  rx_sm_offset = pio_add_program(PICO_RMII_ETHERNET_PIO,
				 &rmii_ethernet_phy_rx_data_program);
  tx_sm_offset = pio_add_program(PICO_RMII_ETHERNET_PIO,
				 &rmii_ethernet_phy_tx_data_program);

  // Configure the DMA channels
  rx_dma_chan = dma_claim_unused_channel(true);
  rx_chain_chan = dma_claim_unused_channel(true);
  tx_dma_chan = dma_claim_unused_channel(true);
  tx_chain_chan = dma_claim_unused_channel(true);

  // Reset them
  dma_channel_abort(rx_dma_chan);
  dma_channel_abort(rx_chain_chan);
  dma_channel_abort(tx_dma_chan);
  dma_channel_abort(tx_chain_chan);

  dma_channel_hw_addr(rx_dma_chan)->al1_ctrl = 0;
  dma_channel_hw_addr(rx_chain_chan)->al1_ctrl = 0;
  dma_channel_hw_addr(tx_dma_chan)->al1_ctrl = 0;
  dma_channel_hw_addr(tx_chain_chan)->al1_ctrl = 0;

  // Get default config for RX receive channel
  // Defaults: 32 bit xfer, unpaced, no write inc, read inc, no chain
  rx_dma_channel_config = dma_channel_get_default_config(rx_dma_chan);

  // Read from FIFO, don't increment read address
  channel_config_set_read_increment(&rx_dma_channel_config, false);

  // Write to buffer, increment write address
  channel_config_set_write_increment(&rx_dma_channel_config, true);

  // Wrap write address on ring size byte boundary
  channel_config_set_ring(&rx_dma_channel_config, true, RX_BUF_SIZE_POW);

  // Fetch from rx PIO FIFO
  channel_config_set_dreq(&rx_dma_channel_config,
			  pio_get_dreq(PICO_RMII_ETHERNET_PIO,
				       PICO_RMII_ETHERNET_SM_RX, false));

  // Byte transfers
  channel_config_set_transfer_data_size(&rx_dma_channel_config, DMA_SIZE_8);

  // Chain to the rx reload channel to restart DMA for next packet
  channel_config_set_chain_to(&rx_dma_channel_config, rx_chain_chan);

  dma_channel_configure
    (
     rx_dma_chan, &rx_dma_channel_config,
     &rx_ring[0],
     // PIO fills upper byte of RX FIFO
     ((uint8_t*)&PICO_RMII_ETHERNET_PIO->rxf[PICO_RMII_ETHERNET_SM_RX]) + 3,
     RX_BUF_SIZE * 16, // Arbitrary - just keep filling the ring
     false
     );

  // Save the channel config, with EN asserted, for the chain reload value
  rx_ctl_reload = dma_hw->ch[rx_dma_chan].al1_ctrl | DMA_CH0_CTRL_TRIG_EN_BITS;

  // Get default config for chain DMA channel
  // Defaults: 32 bit xfer, unpaced, no write inc, read inc, no chain
  rx_chain_channel_config = dma_channel_get_default_config(rx_chain_chan);
    
  // Read from single address, don't increment read address
  channel_config_set_read_increment(&rx_chain_channel_config, false);

  // Write to single address, don't increment write address
  channel_config_set_write_increment(&rx_chain_channel_config, false);

  dma_channel_configure(rx_chain_chan, &rx_chain_channel_config,
			&dma_hw->ch[rx_dma_chan].ctrl_trig,
			&rx_ctl_reload, // Contains control register reload
			1,
			false
			);

  // Get default config for tx packet data DMA channel
  // Defaults: 32 bit xfer, unpaced, no write inc, read inc, no chain
  tx_dma_channel_config = dma_channel_get_default_config(tx_dma_chan);

  // Read from packet ring, increment read address
  channel_config_set_read_increment(&tx_dma_channel_config, true);

  // Write to PIO FIFO, don't increment write address
  channel_config_set_write_increment(&tx_dma_channel_config, false);

  // Wrap read address on ring size byte boundary
  channel_config_set_ring(&tx_dma_channel_config, false, TX_BUF_SIZE_POW);

  // Let TX PIO engine request data
  channel_config_set_dreq(&tx_dma_channel_config,
			  pio_get_dreq(PICO_RMII_ETHERNET_PIO,
				       PICO_RMII_ETHERNET_SM_TX, true));

  // Eight bit transfers
  channel_config_set_transfer_data_size(&tx_dma_channel_config, DMA_SIZE_8);

  // Chain to tx command channel
  channel_config_set_chain_to(&tx_dma_channel_config, tx_chain_chan);

  // Setup the DMA to send the frame via the PIO RMII tansmitter
  dma_channel_configure(
			tx_dma_chan, &tx_dma_channel_config,
			// PIO leaves data in upper byte of FIFO word
			((uint8_t*)&PICO_RMII_ETHERNET_PIO->txf[PICO_RMII_ETHERNET_SM_TX]) + 3,
			&tx_ring[0],
			1518, // Will be over-written by tx chain channel
			false
			);

  // Get default config for TX chain DMA channel
  // Defaults: 32 bit xfer, unpaced, no write inc, read inc, no chain
  tx_chain_channel_config = dma_channel_get_default_config(tx_chain_chan);
    
  // Read from ring address, increment read address
  channel_config_set_read_increment(&tx_chain_channel_config, true);

  // Wrap read address on ring size byte boundary
  channel_config_set_ring(&tx_chain_channel_config, false, TX_NUM_PTR_POW_BYTES);

  // Write to single address, don't increment write address
  channel_config_set_write_increment(&tx_chain_channel_config, false);

  dma_channel_configure(tx_chain_chan, &tx_chain_channel_config,
			&dma_hw->ch[tx_dma_chan].al1_transfer_count_trig,
			&tx_pkt_ptr[0],
			1, // Will be over-written by packet output routine
			false
			);

    
#ifdef USE_DMA_CRC
  // Setup the DMA channel to be used for ring/pbuf transfers
  pbuf_chan = dma_claim_unused_channel(true);
  dma_channel_abort(pbuf_chan);
  dma_channel_hw_addr(pbuf_chan)->al1_ctrl = 0;

  // Get default config for pbuf copy DMA channel
  // Defaults: 32 bit xfer, unpaced, no write inc, read inc, no chain
  pbuf_rx_channel_config = dma_channel_get_default_config(pbuf_chan);
    
  // Read from ring address, increment read address
  channel_config_set_read_increment(&pbuf_rx_channel_config, true);

  // Wrap read address on ring size byte boundary
  channel_config_set_ring(&pbuf_rx_channel_config, false, RX_BUF_SIZE_POW);

  // Write to buffer, increment write address
  channel_config_set_write_increment(&pbuf_rx_channel_config, true);

  // Eight bit transfers
  channel_config_set_transfer_data_size(&pbuf_rx_channel_config, DMA_SIZE_8);

  // Select this channel for sniffing
  channel_config_set_sniff_enable(&pbuf_rx_channel_config, true);

  // Enable the DMA sniffer to calculate Ethernet CRC
  dma_sniffer_enable(pbuf_chan, DMA_SNIFF_CTRL_CALC_VALUE_CRC32R, true);
  dma_sniffer_set_output_reverse_enabled(true);

  // Get default config for pbuf copy DMA channel
  // Defaults: 32 bit xfer, unpaced, no write inc, read inc, no chain
  pbuf_tx_channel_config = dma_channel_get_default_config(pbuf_chan);
    
  // Read from buffer address, increment read address
  channel_config_set_read_increment(&pbuf_tx_channel_config, true);

  // Write to ring buffer, increment write address
  channel_config_set_write_increment(&pbuf_tx_channel_config, true);

  // Wrap write address on ring size byte boundary
  channel_config_set_ring(&pbuf_tx_channel_config, true, TX_BUF_SIZE_POW);

  // Eight bit transfers
  channel_config_set_transfer_data_size(&pbuf_tx_channel_config, DMA_SIZE_8);

  // Select this channel for sniffing
  channel_config_set_sniff_enable(&pbuf_tx_channel_config, true);

  // Make a no-inc read version, for padding tx buffers
  pbuf_tx_no_inc_channel_config = pbuf_tx_channel_config;
  channel_config_set_read_increment(&pbuf_tx_no_inc_channel_config, false);
#endif

  // Run Tx PIO state machine at 2x RMII clk (i.e. 100 MHz)
  float tx_div = (float)clock_get_hz(clk_sys)/100e6;

#ifdef GENERATE_RMII_CLK
  // Run Rx PIO state machine at 2x RMII clk (i.e. 100 MHz)
  float rx_div = (float)clock_get_hz(clk_sys)/100e6;
#else
  // Run Rx PIO state machine at 6x RMII clk (i.e. 300 MHz)
  float rx_div = (float)clock_get_hz(clk_sys)/300e6;
#endif

  // Configure the RMII TX state machine
  rmii_ethernet_phy_tx_init(PICO_RMII_ETHERNET_PIO,
			    PICO_RMII_ETHERNET_SM_TX,
			    // Start of PIO program
			    tx_sm_offset,
			    // Pass in the TX pio entry point
			    rmii_ethernet_phy_tx_data_offset_tx_start,
			    PICO_RMII_ETHERNET_TX_PIN,
			    PICO_RMII_ETHERNET_RETCLK_PIN,
			    tx_div);

  // Configure the RMII RX state machine
  rmii_ethernet_phy_rx_init(PICO_RMII_ETHERNET_PIO,
			    PICO_RMII_ETHERNET_SM_RX,
			    rx_sm_offset,
			    PICO_RMII_ETHERNET_RX_PIN,
			    rx_div);

#ifdef PICO_RMII_ETHERNET_RST_PIN
  // Deassert reset after a minimum of 25 ms with the RMII clock active
  sleep_ms(25);
  //gpio_put(PICO_RMII_ETHERNET_RST_PIN, 1);
  // Allow on-board pull up to hold reset high
  gpio_set_dir(PICO_RMII_ETHERNET_RST_PIN, GPIO_IN);
#endif

#ifdef PICO_RMII_ETHERNET_PWR_PIN
  // Enable power after RMII clock is running
  // On-board reset should deassert after 25 ms
  gpio_put(PICO_RMII_ETHERNET_PWR_PIN, 1);
#endif

  // Add handler for PIO SM interrupt
  // We use PIO IRQ 0, which maps to system IRQ 7/9
  if (PICO_RMII_ETHERNET_PIO == pio0) {
    irq_set_exclusive_handler(PIO0_IRQ_0, netif_rmii_ethernet_eof_isr);
    pio_set_irq0_source_enabled(pio0, pis_interrupt0, 1);
    irq_set_enabled(PIO0_IRQ_0, true);
  } else {
    irq_set_exclusive_handler(PIO1_IRQ_0, netif_rmii_ethernet_eof_isr);
    pio_set_irq0_source_enabled(pio1, pis_interrupt0, 1);
    irq_set_enabled(PIO1_IRQ_0, true);
  }

  // Enable PIO RX FIFO DMA
  dma_channel_start(rx_chain_chan);

#ifdef GENERATE_MDIO_CLK
  // Setup 50 kHz clock for MDIO clock 
  // First, enable PWM
  gpio_set_function(PICO_RMII_ETHERNET_MDC_PIN, GPIO_FUNC_PWM);
  pwm_config pwm_cnfg = pwm_get_default_config();
  uint32_t pwm_slice_num = pwm_gpio_to_slice_num(PICO_RMII_ETHERNET_MDC_PIN);

  // Set PWM clock to 10 MHz
  float div_10M = (float)clock_get_hz(clk_sys)/10000000;
  pwm_config_set_clkdiv(&pwm_cnfg, div_10M);

  // Divide 10 MHz clock by 200 to get 50 kHz                           
  // Wrap at count of 200, level at 100 to give 50 kHz/50% duty cycle
  pwm_config_set_wrap(&pwm_cnfg, 199);
  pwm_init(pwm_slice_num, &pwm_cnfg, true);
  pwm_set_gpio_level(PICO_RMII_ETHERNET_MDC_PIN, 100);
#endif

  // Setup MDIO pin
  gpio_init(PICO_RMII_ETHERNET_MDIO_PIN);

  // Wait for LAN8720A to wake up
  sleep_ms(100);

  // Get LAN8720A PHY address by looking for a response to reg 0
  for (int i = 0; i < 32; i++) {
    if (netif_rmii_ethernet_mdio_read(i, 0) != 0xffff) {
      phy_address = i;
      break;
    }
  }

  if (phy_address == 0xffff) {
    printf("Failed to find a PHY register\n");
    arch_pico_info(netif);
    return ERR_IF;
  }
   
#if defined(GENERATE_RMII_CLK) && !defined(PICO_RMII_ETHERNET_RST_PIN) && !defined(PICO_RMII_ETHERNET_PWR_PIN)

  // Enable limited workaround for lack of PHY reset
  printf("Enabling no PHY reset pin mitigation\n");

  // Do a soft reset
  netif_rmii_ethernet_mdio_write(phy_address, LAN8720A_BASIC_CONTROL_REG,
				 0x8000);

  // Wait for it to settle
  sleep_ms(1);
#endif

  // Default mode is 10Mbps, auto-negociate disabled
  // Uncomment this to switch to 100Mbps, auto-negociate disabled
  // netif_rmii_ethernet_mdio_write(phy_address, LAN8720A_BASIC_CONTROL_REG, 0x2000); // 100 Mbps, auto-negeotiate disabled

  // Or keep the following config to auto-negotiate 10/100Mbps
  // 0b0000_0001_1110_0001
  //           | |||⁻⁻⁻⁻⁻\__ 0b00001=IEEE 802.3
  //           | || \_______ 10BASE-T ability
  //           | | \________ 10BASE-T Full-Duplex ability
  //           |  \_________ 100BASE-T ability
  //            \___________ 100BASE-T Full-Duplex ability

  //    printf("Auto reg, before write: %08x\n", 
  //	   netif_rmii_ethernet_mdio_read(phy_address, LAN8720A_AUTO_NEGO_REG)); 


  //    netif_rmii_ethernet_mdio_write(phy_address, LAN8720A_AUTO_NEGO_REG, 0);
#if 1
  netif_rmii_ethernet_mdio_write(phy_address, LAN8720A_AUTO_NEGO_REG, 
				 LAN8720A_AUTO_NEGO_REG_IEEE802_3
				 // TODO: the PIO RX and TX are hardcoded to 100Mbps, make it configurable to uncomment this
				 // | LAN8720A_AUTO_NEGO_REG_10_ABI | LAN8720A_AUTO_NEGO_REG_10_FD_ABI
				 | LAN8720A_AUTO_NEGO_REG_100_ABI | LAN8720A_AUTO_NEGO_REG_100_FD_ABI
				 );
#endif

  // Enable auto-negotiate
  netif_rmii_ethernet_mdio_write(phy_address, LAN8720A_BASIC_CONTROL_REG, 0x1000);

#if 0
  printf("Auto reg: %08x\n", 
	 netif_rmii_ethernet_mdio_read(phy_address, LAN8720A_AUTO_NEGO_REG)); 

  printf("Ctl reg: %08x\n", 
	 netif_rmii_ethernet_mdio_read(phy_address, LAN8720A_BASIC_CONTROL_REG));
#endif

  return ERR_OK;
}

err_t netif_rmii_ethernet_init(struct netif *netif) {

  // To set up a static IP, uncomment the folowing lines and
  // comment the one using DHCP
  // const ip_addr_t ip = IPADDR4_INIT_BYTES(169, 254, 145, 200);
  // const ip_addr_t mask = IPADDR4_INIT_BYTES(255, 255, 0, 0);
  // const ip_addr_t gw = IPADDR4_INIT_BYTES(169, 254, 145, 164);
  // netif_add(netif, &ip, &mask, &gw, NULL, netif_rmii_ethernet_low_init, netif_input);

  // Set up the interface using DHCP
  if (netif_add(netif, IP4_ADDR_ANY, IP4_ADDR_ANY, IP4_ADDR_ANY, NULL,
		netif_rmii_ethernet_low_init, netif_input) == NULL) {
    return ERR_IF;
  }

  netif->name[0] = 'e';
  netif->name[1] = '0';
  
  return ERR_OK;
}

// Test the RX ring buffer for packets, send to LWIP if available
absolute_time_t next_mdio_time = 0;
void netif_rmii_ethernet_poll() {
  uint32_t rx_packet_count;
  uint32_t rx_packet_byte_count;
  uint32_t rx_packet_addr;
  uint32_t deferred_read;
  uint16_t link_status;

  // Test if time to read MDIO
  absolute_time_t curr_time = get_absolute_time();
  int64_t diff_time = absolute_time_diff_us(curr_time, next_mdio_time);
  if (diff_time < 0) {
    // Schedule next read
    next_mdio_time = make_timeout_time_ms(500);

    // Use non-blocking MDIO read to get link status
    deferred_read = netif_rmii_ethernet_mdio_read_nb(phy_address, 1);
    if (deferred_read != -1) {
      link_status = (deferred_read & 0x04) >> 2;
      if (netif_is_link_up(rmii_eth_netif) ^ link_status) {
	if (link_status) {
	  // printf("netif_set_link_up\n");
	  netif_set_link_up(rmii_eth_netif);
	} else {
	  // printf("netif_set_link_down\n");
	  netif_set_link_down(rmii_eth_netif);
	}
      }
    }
  }


  // Get number of packets received since last poll
  // Read curr pkt ptr once, to avoid ISR updating while we're using it
  uint32_t safe_rx_curr_pkt_ptr = rx_curr_pkt_ptr;
    
  // Deal with pointer wrap
  if (safe_rx_curr_pkt_ptr < rx_prev_pkt_ptr) {
    rx_packet_count = (RX_NUM_PTR + safe_rx_curr_pkt_ptr) - rx_prev_pkt_ptr;
  } else {
    rx_packet_count = safe_rx_curr_pkt_ptr - rx_prev_pkt_ptr;
  }

  // Process all the packets outstanding
  while (rx_packet_count > 0) {
    // Get current packet parameters
    rx_packet_byte_count = rx_pkt_ptr[rx_prev_pkt_ptr].pkt_len;
    rx_packet_addr = rx_pkt_ptr[rx_prev_pkt_ptr].pkt_addr;

    // Bump pkt ptr/count
    rx_prev_pkt_ptr = (rx_prev_pkt_ptr + 1) & RX_NUM_MASK;
    rx_packet_count--;
      
    struct pbuf* p = pbuf_alloc(PBUF_RAW, rx_packet_byte_count, PBUF_POOL);

    // Push packet from ring buffer into LWIP pbuf
    uint32_t rx_len = ethernet_frame_to_pbuf(rx_ring,
					     p,
					     rx_packet_byte_count,
					     rx_packet_addr);

    if (rmii_eth_netif->input(p, rmii_eth_netif) != ERR_OK) {
      pbuf_free(p);
    }

    // Indicate CRC errors
    if (rx_len == 0) {
      printf("*");
      pbuf_free(p);
    }
  }

  sys_check_timeouts();
}

void netif_rmii_ethernet_loop() {
  while (1) {
    netif_rmii_ethernet_poll();
    //sleep_us(2);
  }
}

