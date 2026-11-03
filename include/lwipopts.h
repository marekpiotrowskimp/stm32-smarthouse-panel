/* Konfiguracja lwIP 2.1: FreeRTOS (CMSIS-RTOS2), Ethernet, DHCP, API socketów. */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#include <stdlib.h>

#define NO_SYS                      0
#define SYS_LIGHTWEIGHT_PROT        1
#define LWIP_TCPIP_CORE_LOCKING     1
#define LWIP_SINGLE_NETIF           1

/* Pamięć */
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (16 * 1024)
#define MEMP_NUM_PBUF               16
#define MEMP_NUM_UDP_PCB            6
#define MEMP_NUM_TCP_PCB            16
#define MEMP_NUM_TCP_PCB_LISTEN     2
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_NETCONN            16
#define PBUF_POOL_SIZE              24
#define PBUF_POOL_BUFSIZE           1536

/* Protokoły */
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_DHCP                   1
#define LWIP_DNS                    0
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_IGMP                   0
#define LWIP_RAND()                 ((u32_t)rand())

#define TCP_MSS                     1460
#define TCP_SND_BUF                 (4 * TCP_MSS)
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_SND_QUEUELEN            (2 * TCP_SND_BUF / TCP_MSS)

/* Interfejs */
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define ETH_PAD_SIZE                0

/* TX: sumy wstawia MAC (CIC = full, app_platform.c) – przy obejściu silnika
 * sprzęt i tak nadpisywał sumę ICMP zerem. RX: weryfikacja programowa. */
#define CHECKSUM_GEN_IP             0
#define CHECKSUM_GEN_UDP            0
#define CHECKSUM_GEN_TCP            0
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1
#define CHECKSUM_CHECK_TCP          1
#define CHECKSUM_GEN_ICMP           0

/* API */
#define LWIP_NETCONN                1
#define LWIP_SOCKET                 1
#define LWIP_POSIX_SOCKETS_IO_NAMES 0
#define LWIP_SO_RCVTIMEO            1
#define LWIP_SO_SNDTIMEO            1

/* Wątki (rozmiary stosów w bajtach – CMSIS-RTOS2) */
#define TCPIP_THREAD_NAME           "tcpip"
#define TCPIP_THREAD_STACKSIZE      4096
#define TCPIP_THREAD_PRIO           40 /* osPriorityHigh */
#define TCPIP_MBOX_SIZE             16
#define DEFAULT_THREAD_STACKSIZE    2048
#define DEFAULT_THREAD_PRIO         24 /* osPriorityNormal */
#define DEFAULT_TCP_RECVMBOX_SIZE   8
#define DEFAULT_UDP_RECVMBOX_SIZE   8
#define DEFAULT_RAW_RECVMBOX_SIZE   8
#define DEFAULT_ACCEPTMBOX_SIZE     8

#define LWIP_STATS                  0

#endif /* LWIPOPTS_H */
