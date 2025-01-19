### Pico

Clock = 300 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| Ext |  1.1  | 94.9 MBit/s | 0.00% |
| Ext |  1.15 | 94.9 MBit/s | 0.00% |
| Ext |  1.2  | 94.9 MBit/s | 0.00% |
| Ext |  1.2  | 94.9 MBit/s | 0.00% |
| Ext |  1.25 | 94.9 MBit/s | 0.00% |
| Ext |  1.3  | 94.9 MBit/s | 0.00% |

Clock = 300 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| RPI |  1.1  | 94.9 MBit/s | 0.00% |
| RPI |  1.15 | 94.9 MBit/s | 0.00% |
| RPI |  1.2  | 94.9 MBit/s | 0.00% |
| RPI |  1.25 | 94.9 MBit/s | 0.00% |
| RPI |  1.3  | 94.9 MBit/s | 0.00% |

Clock = 250 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| RPI |  1.1  | 94.9 MBit/s | 0.00% |
| RPI |  1.15 | 94.9 MBit/s | 0.00% |
| RPI |  1.2  | 94.9 MBit/s | 0.00% |
| RPI |  1.25 | 94.9 MBit/s | 0.00% |
| RPI |  1.3  | 94.9 MBit/s | 0.00% |

Clock = 200 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| RPI |  1.1  | 94.9 MBit/s | 0.00% |
| RPI |  1.15 | 94.9 MBit/s | 0.00% | Needed two boots |
| RPI |  1.2  | 94.9 MBit/s | 0.00% |
| RPI |  1.25 | 94.9 MBit/s | 0.00% | 
| RPI |  1.3  | 94.9 MBit/s | 0.00% | 

Clock = 150 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| RPI |  1.1  | 94.9 MBit/s | 0.00% |
| RPI |  1.15 | 94.9 MBit/s | 0.00% | Needed two boots |
| RPI |  1.2  | 94.9 MBit/s | 0.00% |
| RPI |  1.25 | 94.9 MBit/s | 0.00% |
| RPI |  1.3  | N/A | N/A | No DHCP lease |

### Pico2

Clock = 300 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| Ext |  1.1  | 94.9 MBit/s | 0.00% |
| Ext |  1.15 | 94.9 MBit/s | 0.00% |
| Ext |  1.2  | 94.9 MBit/s | 0.00% |
| Ext |  1.2  | 94.9 MBit/s | 0.00% |
| Ext |  1.25 | 94.9 MBit/s | 0.00% |
| Ext |  1.3  | 94.9 MBit/s | 0.00% |

Clock = 300 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| RPI |  1.1  | 94.9 MBit/s | 0.00% |
| RPI |  1.15 | 94.9 MBit/s | 0.00% |
| RPI |  1.2  | 94.9 MBit/s | 0.00% |
| RPI |  1.25 | 94.9 MBit/s | 0.00% |
| RPI |  1.3  | 94.9 MBit/s | 0.00% |

Clock = 200 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| RPI |  1.1  | 94.9 MBit/s | 0.00% |
| RPI |  1.15 | 94.9 MBit/s | 0.00% |
| RPI |  1.2  | 94.9 MBit/s | 0.00% |
| RPI |  1.25 | 94.9 MBit/s | 0.00% |
| RPI |  1.3  | 94.9 MBit/s | 0.00% |

Clock = 150 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| RPI |  1.1  | N/A | N/A | No DHCP lease |
| RPI |  1.15 | 24.9 MBit/s | 0.02% | CRC errors Needed three boots |
| RPI |  1.2  | 94.9 MBit/s | 0.00% | Needed four boots |
| RPI |  1.25 | 94.9 MBit/s | 0.00% | Needed four boots |
| RPI |  1.3  | 94.9 MBit/s | 0.00% | Needed five boots |

With CPU CRC:

Clock = 300 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| RPI |  1.1  | 45.2 Kbit/s | 0.00% | |
| RPI |  1.15 | 94.9 MBit/s | 0.00% | |
| RPI |  1.2  | 94.9 MBit/s | 0.00% | |
| RPI |  1.25 | 94.9 MBit/s | 0.00% | |
| RPI |  1.3  | 94.9 MBit/s | 0.00% | |

Clock = 250 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| RPI |  1.1  | 87.7 MBit/s | 0.00% | |
| RPI |  1.15 | 87.7 MBit/s | 0.00% | |
| RPI |  1.2  | 87.7 MBit/s | 0.00% | |
| RPI |  1.25 | 87.7 MBit/s | 0.00% | |
| RPI |  1.3  | 87.7 MBit/s | 0.00% | |

Clock = 200 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| RPI |  1.1  | 67.3 MBit/s | 0.00% | CRC errors Needed three boots |
| RPI |  1.15 | 65.7 MBit/s | 0.00% | CRC errors Needed three boots |
| RPI |  1.2  | 73.6 MBit/s | 0.00% | CRC errors Needed two boots |
| RPI |  1.25 | 76.9 MBit/s | 0.00% | CRC errors Needed three boots |
| RPI |  1.3  | 78.6 MBit/s | 0.00% | CRC errors Needed two boots |

Clock = 150 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| RPI |  1.1  | 14.7 MBit/s | 0.00% | CRC errors Needed three boots |
| RPI |  1.15 | 15.2 MBit/s | 0.00% | CRC errors Needed two boots |
| RPI |  1.2  | 14.9 MBit/s | 0.00% | CRC errors Needed two boots |
| RPI |  1.25 | 21.5 MBit/s | 0.00% | CRC errors Needed two boots |
| RPI |  1.3  | N/A | N/A | No DHCP lease |


### Pimoroni Pico plus 2 

Clock = 300 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| Ext |  1.1  | 94.9 MBit/s | 0.00% |
| Ext |  1.15 | 94.9 MBit/s | 0.00% |
| Ext |  1.2  | 94.9 MBit/s | 0.00% |
| Ext |  1.2  | 94.9 MBit/s | 0.00% |
| Ext |  1.25 | 94.9 MBit/s | 0.00% |
| Ext |  1.3  | 94.9 MBit/s | 0.00% |

Clock = 300 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| RPI |  1.1  | 94.9 MBit/s | 0.00% | |
| RPI |  1.15 | N/A | N/A | No DHCP lease |
| RPI |  1.2  | 240  KBit/s | 0.00%  | CRC errors seen |
| RPI |  1.25 | 94.9 MBit/s | 0.00% | |
| RPI |  1.3  | 94.9 MBit/s | 0.00% | | 

Clock = 200 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| RPI |  1.1  | 94.9 MBit/s | 0.00% | |
| RPI |  1.15 | 94.9 MBit/s | 0.00% | Needed two boots |
| RPI |  1.2  | 94.9 MBit/s | 0.00% | |
| RPI |  1.25 | 94.9 MBit/s | 0.00% | Needed three boots |
| RPI |  1.3  | 94.9 MBit/s | 0.00% | Needed two boots |

With USE_CPU_CRC enabled:

Clock = 300 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| RPI |  1.1  | N/A | N/A | No DHCP lease |
| RPI |  1.15 | 94.9 MBit/s | 0.00% | |
| RPI |  1.2  | 94.9 MBit/s | 0.00% | |
| RPI |  1.2  | 94.9 MBit/s | 0.00% | | 
| RPI |  1.25 | 94.9 MBit/s | 0.00% | | 
| RPI |  1.3  | 94.9 MBit/s | 0.00% | |


Clock = 200 MHz

| CLK Src | Vcore | Iperf | Packet Loss | Comment |
|  :---:  | :---: | :---: |    :---:    | :--- |
| RPI |  1.1  | N/A | N/A | No DHCP lease |
| RPI |  1.15 | 94.9 MBit/s | 0.00% | |
| RPI |  1.2  | 94.9 MBit/s | 0.00% | |
| RPI |  1.2  | 94.9 MBit/s | 0.00% | |
| RPI |  1.25 | 94.9 MBit/s | 0.00% | Needed two boots |
| RPI |  1.3  | 126 KBit/s | 68.0% | |

Above testing done with SWD download to RP2XXX SRAM. Testing was done with:
```
iperf -c <pico reported ip address>
sudo ping -f <pico reported ip address> -c 100000
```

