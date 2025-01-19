# Pico RMII Ethernet library, Neon Chrome Edition (NCE)

## Introduction

This is an update to the existing pico-rmii-ethernet library by Sandeep Mistry.
Please see [README_orig.md](./README_orig.md) for a description of that
library. 

## Improvements present in this library:

1. Achieves 94.9 Mbit/sec transfer rate as measured by iperf when using the
RP2XXX DMA sniffer, or overclocked. 
2. An unmodified LAN8720a module with RP2XXX operating at 300 MHz can
achieve 94.9 Mbit/sec transfer rate.
3. When operating with modified LAN8720a module, greater flexiblity in choosing
system clocks: 200, 250, 300 MHz.
4. Interpacket gaps correctly inserted into transmit stream.
5. Interrupt driven MDIO interface.
6. Project specific iperf available in the examples directory.

## Discussion

This library uses DMA driven ring buffers for both transmit and receive. The
transmit side is entirely DMA driven, while the receive side uses a per-packet
interrupt to finalize a received packet. Additionally, the DMA sniffer
subsystem may be enabled to off-load CRC calculations from the CPU. To further
reduce CPU utilization, an interrupt driven MDIO subsystem is implemented.

## Resource Utilization

The library uses:

1. Five DMA channels: 2 receive, 3 transmit. Two channels are used per Tx/Rx for
ring buffer management, and the third Tx channel assists in generating the
Ethernet Interpacket Gap (IPG).
2. Optionally, the DMA "sniffer" logic may be used. 
2. Two interrupts: 1 shared for MDIO, and 1 exclusive for the end-of-packet
processing.
3. Two 4KB aligned memory regions for Tx/Rx data, 32/64 long word pointer
buffers, and a 256 long word CRC table (if CPU CRC calculation is enabled). 
4. One PWM timer used as MD clock, if internal MDIO clock generation is enabled.
5. One DMA timer used to assist with IPG generation.
6. For internal RMII clock: 12 PIO instructions for Tx, 5 for Rx, total 17.
7. For external RMII clock: 12 PIO instructions for Tx, 7 for Rx, total 19.

At 300 MHz, almost all of core 1 is used when CPU CRC generation is used.
It is possible to use about 6 usec per packet poll, verified by placing a
sleep_us(6) call in netif_rmii_ethernet_loop() and running iperf. With DMA CRC
generation, a sleep_us(10) may be used with minimal impact. If a 90 Mb/s
iperf rate is acceptable, then sleep_us interval may be increased to 30.
Core 0, of course, remains available for user applications.

## Configuration

LAN8720a module GPIO assignments are found in
[src/rmii_ethernet_phy_rx.pio.](src/rmii_ethernet_phy_rx.pio)
These assignments are in this file to provide PIO code access to the RMII
clock pin define, as there is no convientent method to pass defines from
C to .pio files.

This file also enables LAN8720a module generated RMII (external) clock or RP2XXX
generated RMII (internal) clock, via the GENERATE_RMII_CLK define. When
operating with an internal clock, it is necessary to drive the LAN8720a
reset pin with a GPIO. The LAN8720a requires the RMII clock to be running
while reset is asserted, and the module reset RC network time constant
may not be long enough enough to allow the RP2XXX to setup the clock. Also,
if the RP2XXX is reset after power up, the clock will be stopped until the
clock setup code is executed, which puts the LAN8720a chip into an
undefined condition. A partial mitigation is enabled when there isn't a
defined reset pin, but the workaround doesn't always restore correct operation.

It is also possible to disable generation of MDIO interface clock, via
the GENERATE_MDIO_CLK define. This allows the pin to be used by other
RP2XXX programs, assuming they generate a continous clock at about 50 KHz.
Only the falling edge is used, so duty cycle is not important. Note
that clock rate directly affects the packet poll rate.

DMA or CPU driven CRC calculation is selected by uncommenting one of
define USE_DMA_CRC or define USE_CPU_CRC, found in rmii_ethernet.c. In this
same file, clock speed selection is determined by the value of the target_clk
variable, and Vcore is set by the vreg_set_voltage() call. The default
values are set to 300 MHz and 1.2v, respectively.

If using an unmodified LAN8720a module, only a system clock of 300 MHz provides
enough PIO instruction cycles to reliably clock Ethernet receive data.

With a modified LAN8720a module (see [README_orig.md](./README_orig.md))
system clocks of 200, 250, 300 MHz are available. 150 MHz has been found
to be only marginally usable. If you choose to experiment with 150 MHz,
please comment out the last four lines of the top level CMakefile. This
will select the standard flash memory code, enabling normal flash memory
access speed. When complete, should look like this:
```
# Enable running with flash at higher system clock frequencies
#pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
#target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)

# Apply to all apps
#pico_set_boot_stage2(pico_rmii_ethernet_lwiperf slower_boot2)
#pico_set_boot_stage2(pico_rmii_ethernet_httpd slower_boot2)
```
Without the above edit, iperf performance will be approximately 70.1 MBit/sec.

## Limitations

Does not recover from link down/link up events.

DMA chain channels could possibly be eliminated on RP2350.

For best performance, must be run directly from RP2XXX SRAM. This
can be done by adding the following to the application's CMakelists:
```
        pico_set_binary_type(pico_rmii_ethernet_lwiperf no_flash)
```
and loading via a debugger or
```
        pico_set_binary_type(pico_rmii_ethernet_lwiperf copy_to_ram)
```
and flashing. 

## Compiling

A script to (re)build a Pico executable:
```
#!/bin/bash
rm -rf build_rp2040
cmake -B build_rp2040 -DPICO_PLATFORM=rp2040 -S .
make -j -C build_rp2040 pico_rmii_ethernet_lwiperf
```
The result will be in:
```
$PWD/build_rp2040/examples/lwiperf/pico_rmii_ethernet_lwiperf.elf
```

A script to (re)build a Pico 2 executable:
```
#!/bin/bash
rm -rf build_rp2350
cmake -B build_rp2350 -DPICO_BOARD=pico2 -S .
make -j -C build_rp2350 pico_rmii_ethernet_lwiperf
```
The result will be in:
```
$PWD/build_rp2350/examples/lwiperf/pico_rmii_ethernet_lwiperf.elf
```

## Experimental Observations

The code has been run on Pico, Pico2, and Pimoroni Pico Plus boards. Both
modified and unmodified LAN8720a boards were used. Modified boards enable
driving the LAN8720a RMII clock as well as reset. (The NC pin on the module
was used for the reset signal). Results are in [results.md](./results.md)

Note that some LAN8720a modules lack a conntection to the RC network used
to generate a power up reset. These modules have the 50 MHz oscillator
on the top of the board, vs. the bottom. To determine if a module has
a functional RC network, measure resistance from the LAN8720a reset pin
(15) to VCC. If the resistance is around 1.5 K, the RC network is present.
 

## Acknowledgements

Sandeep Mistry - who determined that using an external clock for the RMII
board would enable much better performance. Repo: https://github.com/sandeepmistry/pico-rmii-ethernet

https://github.com/messani/pico-lan8720 - provided external RMII clock input
example.

## History
1. Original version
2. Added DMA driven CRC, removed 100 MHz system clock configuration.
3. Added external RMII clock input configuration, clean-up.