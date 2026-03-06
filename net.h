#ifndef NET_H
#define NET_H

#include <stdint.h>

// ===== Intel E1000 Register Offsets =====
#define E1000_CTRL     0x0000   // Device Control
#define E1000_STATUS   0x0008   // Device Status
#define E1000_EERD     0x0014   // EEPROM Read
#define E1000_ICR      0x00C0   // Interrupt Cause Read
#define E1000_IMS      0x00D0   // Interrupt Mask Set
#define E1000_IMC      0x00D8   // Interrupt Mask Clear
#define E1000_RCTL     0x0100   // Receive Control
#define E1000_TCTL     0x0400   // Transmit Control
#define E1000_TIPG     0x0410   // TX Inter-Packet Gap
#define E1000_RDBAL    0x2800   // RX Descriptor Base Low
#define E1000_RDBAH    0x2804   // RX Descriptor Base High
#define E1000_RDLEN    0x2808   // RX Descriptor Length
#define E1000_RDH      0x2810   // RX Descriptor Head
#define E1000_RDT      0x2818   // RX Descriptor Tail
#define E1000_TDBAL    0x3800   // TX Descriptor Base Low
#define E1000_TDBAH    0x3804   // TX Descriptor Base High
#define E1000_TDLEN    0x3808   // TX Descriptor Length
#define E1000_TDH      0x3810   // TX Descriptor Head
#define E1000_TDT      0x3818   // TX Descriptor Tail
#define E1000_RAL      0x5400   // Receive Address Low
#define E1000_RAH      0x5404   // Receive Address High
#define E1000_MTA      0x5200   // Multicast Table Array (128 entries)

// CTRL bits
#define E1000_CTRL_SLU    (1 << 6)   // Set Link Up
#define E1000_CTRL_RST    (1 << 26)  // Device Reset

// RCTL bits
#define E1000_RCTL_EN     (1 << 1)   // Receiver Enable
#define E1000_RCTL_SBP    (1 << 2)   // Store Bad Packets
#define E1000_RCTL_UPE    (1 << 3)   // Unicast Promiscuous
#define E1000_RCTL_MPE    (1 << 4)   // Multicast Promiscuous
#define E1000_RCTL_BAM    (1 << 15)  // Broadcast Accept Mode
#define E1000_RCTL_BSIZE  (3 << 16)  // Buffer Size (00 = 2048)
#define E1000_RCTL_SECRC  (1 << 26)  // Strip Ethernet CRC

// TCTL bits
#define E1000_TCTL_EN     (1 << 1)   // Transmit Enable
#define E1000_TCTL_PSP    (1 << 3)   // Pad Short Packets

// TX Descriptor status/command bits
#define E1000_TXD_CMD_EOP  (1 << 0)  // End of Packet
#define E1000_TXD_CMD_IFCS (1 << 1)  // Insert FCS
#define E1000_TXD_CMD_RS   (1 << 3)  // Report Status
#define E1000_TXD_STAT_DD  (1 << 0)  // Descriptor Done

// RX Descriptor status bits
#define E1000_RXD_STAT_DD  (1 << 0)  // Descriptor Done
#define E1000_RXD_STAT_EOP (1 << 1)  // End of Packet

// ===== Descriptor Counts =====
#define E1000_NUM_RX_DESC  8
#define E1000_NUM_TX_DESC  8
#define E1000_RX_BUF_SIZE  2048
#define E1000_TX_BUF_SIZE  2048

// ===== Ethernet =====
#define ETH_TYPE_ARP   0x0806
#define ETH_TYPE_IP    0x0800

// ===== Hardware Descriptors =====
struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

// ===== Protocol Headers =====
struct eth_header {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;
} __attribute__((packed));

struct arp_packet {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[6];
    uint8_t  spa[4];
    uint8_t  tha[6];
    uint8_t  tpa[4];
} __attribute__((packed));

struct ip_header {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint8_t  src[4];
    uint8_t  dst[4];
} __attribute__((packed));

struct icmp_header {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed));

struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset; // upper 4 bits = header len in 32-bit words
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

// TCP flags
#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10

// TCP connection states
#define TCP_STATE_CLOSED      0
#define TCP_STATE_SYN_SENT    1
#define TCP_STATE_ESTABLISHED 2
#define TCP_STATE_FIN_WAIT    3

// HTTP receive buffer size
#define HTTP_BUF_SIZE 8192

// ===== Network State =====
typedef struct {
    int      detected;        // 1 if E1000 found on PCI bus
    int      link_up;         // 1 if link is up
    uint8_t  mac[6];          // MAC address
    uint8_t  ip[4];           // Our IP (default: 10.0.2.15)
    uint8_t  gateway_ip[4];   // Gateway IP (default: 10.0.2.2)
    uint8_t  gateway_mac[6];  // Cached gateway MAC
    int      gateway_mac_valid;
    uint32_t mmio_base;       // MMIO base address
    uint8_t  pci_bus;
    uint8_t  pci_slot;
    uint8_t  pci_func;
    uint16_t rx_cur;          // Current RX descriptor index
    uint16_t tx_cur;          // Current TX descriptor index
    // Ping state
    int       ping_active;
    int       ping_replied;
    uint16_t  ping_seq;
    uint32_t  ping_sent_tick;
    // DNS state
    uint8_t  dns_server[4];    // DNS server IP (default: 1.1.1.1)
    uint8_t  dns_result[4];    // Resolved IP address
    int      dns_resolved;     // 1 when DNS reply received
    int      dns_pending;      // 1 when waiting for DNS reply
    uint16_t dns_txid;         // Transaction ID
    // TCP state (single connection)
    int      tcp_state;
    uint32_t tcp_local_seq;
    uint32_t tcp_remote_seq;
    uint16_t tcp_local_port;
    uint16_t tcp_remote_port;
    uint8_t  tcp_remote_ip[4];
    uint8_t  tcp_remote_mac[6];
    // HTTP receive state
    uint8_t* http_buf;          // Points to static buffer
    int      http_buf_len;      // Bytes received so far
    int      http_done;         // 1 when transfer complete
    uint16_t ip_id;             // Sequential IP ID
} NetState;

extern NetState net_state;

// ===== Public API =====
void e1000_init(void);
void e1000_poll(void);
int  e1000_send(const void* data, uint16_t len);
void net_send_ping(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3);
int  net_dns_resolve(const char* hostname, uint8_t* out_ip);
int  net_http_get(const char* host, const char* path, uint8_t* out_buf, int max_len);

#endif
