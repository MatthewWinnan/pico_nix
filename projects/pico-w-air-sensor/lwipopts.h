#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// Based on the pico-examples lwipopts for CYW43 threadsafe background mode.
// LWIP_MQTT is enabled so the built-in MQTT client app is compiled in.

#define NO_SYS                      1
#define LWIP_SOCKET                 0

// MEM_LIBC_MALLOC is incompatible with threadsafe background (interrupt-driven) mode
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    16000

#define MEMP_NUM_SYS_TIMEOUT        17
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              24

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1

#define TCP_WND                     (8 * TCP_MSS)
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1

#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_NETCONN                0

// Enable the lwIP MQTT application layer
#define LWIP_MQTT                   1
// Keep MQTT connection alive (ping every 60s)
#define MQTT_CONNECT_TIMOUT         100
// Output ring buffer: default 2048 is too small for multiple discovery payloads
#define MQTT_OUTPUT_RINGBUF_SIZE    8192

#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0
#define LWIP_STATS                  0
#define LWIP_DEBUG                  0
#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_PROVIDE_ERRNO          1

#endif // _LWIPOPTS_H
