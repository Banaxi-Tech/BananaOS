#include "net.h"
#include "pci.h"
#include <stdint.h>
#include <stddef.h>

// ===== I/O Helpers =====
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outl(uint16_t port, uint32_t val) {
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// ===== Memory helpers =====
static void net_memset(void* dst, uint8_t val, uint32_t len) {
    uint8_t* d = (uint8_t*)dst;
    for (uint32_t i = 0; i < len; i++) d[i] = val;
}

static void net_memcpy(void* dst, const void* src, uint32_t len) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int net_strlen(const char* s) {
    int i = 0; while (s[i]) i++; return i;
}

static int net_strncasecmp(const char* a, const char* b, int n) {
    while (n-- > 0 && *a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    if (n < 0) return 0;
    return (uint8_t)*a - (uint8_t)*b;
}

static char* net_strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack, *n = needle;
            while (*h && *n && *h == *n) { h++; n++; }
            if (!*n) return (char*)haystack;
        }
    }
    return NULL;
}

// ===== Network State =====
NetState net_state;

// ===== Descriptor Rings & Buffers (static BSS allocation) =====
// Aligned to 16 bytes as required by the E1000 hardware
static struct e1000_rx_desc rx_descs[E1000_NUM_RX_DESC] __attribute__((aligned(16)));
static struct e1000_tx_desc tx_descs[E1000_NUM_TX_DESC] __attribute__((aligned(16)));
static uint8_t rx_buffers[E1000_NUM_RX_DESC][E1000_RX_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buffers[E1000_NUM_TX_DESC][E1000_TX_BUF_SIZE] __attribute__((aligned(16)));

// Temp buffer for building outgoing packets
static uint8_t tx_packet_buf[2048] __attribute__((aligned(4)));

// ===== MMIO Read/Write =====
static inline void e1000_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(net_state.mmio_base + reg) = val;
}

static inline uint32_t e1000_read(uint32_t reg) {
    return *(volatile uint32_t*)(net_state.mmio_base + reg);
}

// ===== Byte-swap helpers (network byte order) =====
static inline uint16_t htons(uint16_t v) {
    return (v >> 8) | (v << 8);
}
#define ntohs(x) htons(x)

static inline uint32_t htonl(uint32_t v) {
    return ((v >> 24) & 0xFF) |
           ((v >> 8)  & 0xFF00) |
           ((v << 8)  & 0xFF0000) |
           ((v << 24) & 0xFF000000);
}
#define ntohl(x) htonl(x)

// ===== IP Checksum =====
static uint16_t ip_checksum(const void* data, int len) {
    const uint16_t* p = (const uint16_t*)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const uint8_t*)p;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

// ===== Forward Declarations =====
static void net_send_tcp(uint8_t flags, const void* payload, uint16_t payload_len);
static int net_send_udp(const uint8_t* dst_ip, uint16_t src_port, uint16_t dst_port, const void* payload, uint16_t payload_len);
static int net_get_mac_for_ip(const uint8_t* ip, uint8_t* out_mac);
static int net_ensure_gateway_mac(void);

// ===== PCI Scan for E1000 =====
static int e1000_pci_find(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t reg0 = pci_config_read(bus, slot, 0, 0);
            uint16_t vendor = reg0 & 0xFFFF;
            uint16_t device = (reg0 >> 16) & 0xFFFF;

            if (vendor == 0xFFFF) continue;

            // Intel E1000 variants
            if (vendor == 0x8086 &&
                (device == 0x100E ||  // 82540EM (QEMU default)
                 device == 0x100F ||  // 82545EM
                 device == 0x10D3 ||  // 82574L
                 device == 0x153A)) { // E1000e
                net_state.pci_bus = bus;
                net_state.pci_slot = slot;
                net_state.pci_func = 0;
                return 1;
            }
        }
    }
    return 0;
}

// ===== Enable PCI Bus Mastering =====
static void e1000_pci_enable(void) {
    uint32_t cmd = pci_config_read(net_state.pci_bus, net_state.pci_slot, net_state.pci_func, 0x04);
    cmd |= (1 << 2) | (1 << 1) | (1 << 0); // Bus Master + Memory Space + I/O Space
    pci_config_write(net_state.pci_bus, net_state.pci_slot, net_state.pci_func, 0x04, cmd);
}

// ===== Read MAC address from E1000 =====
static void e1000_read_mac(void) {
    // Try reading from RAL/RAH registers first
    uint32_t ral = e1000_read(E1000_RAL);
    uint32_t rah = e1000_read(E1000_RAH);

    if (ral != 0 && ral != 0xFFFFFFFF) {
        net_state.mac[0] = ral & 0xFF;
        net_state.mac[1] = (ral >> 8) & 0xFF;
        net_state.mac[2] = (ral >> 16) & 0xFF;
        net_state.mac[3] = (ral >> 24) & 0xFF;
        net_state.mac[4] = rah & 0xFF;
        net_state.mac[5] = (rah >> 8) & 0xFF;
        return;
    }

    // Fallback: read from EEPROM
    for (int i = 0; i < 3; i++) {
        e1000_write(E1000_EERD, (1) | ((uint32_t)i << 8));
        uint32_t val;
        int timeout = 10000;
        do {
            val = e1000_read(E1000_EERD);
        } while (!(val & (1 << 4)) && --timeout > 0);

        uint16_t data = (val >> 16) & 0xFFFF;
        net_state.mac[i * 2]     = data & 0xFF;
        net_state.mac[i * 2 + 1] = (data >> 8) & 0xFF;
    }
}

// ===== Initialize RX Descriptors =====
static void e1000_rx_init(void) {
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        rx_descs[i].addr = (uint64_t)(uint32_t)&rx_buffers[i][0];
        rx_descs[i].status = 0;
    }

    e1000_write(E1000_RDBAL, (uint32_t)&rx_descs[0]);
    e1000_write(E1000_RDBAH, 0);
    e1000_write(E1000_RDLEN, E1000_NUM_RX_DESC * sizeof(struct e1000_rx_desc));
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, E1000_NUM_RX_DESC - 1);

    net_state.rx_cur = 0;

    // Enable receiver: accept unicast, broadcast, strip CRC, 2048-byte buffers
    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC;
    e1000_write(E1000_RCTL, rctl);
}

// ===== Initialize TX Descriptors =====
static void e1000_tx_init(void) {
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        tx_descs[i].addr = (uint64_t)(uint32_t)&tx_buffers[i][0];
        tx_descs[i].status = E1000_TXD_STAT_DD; // Mark as done so first send works
        tx_descs[i].cmd = 0;
    }

    e1000_write(E1000_TDBAL, (uint32_t)&tx_descs[0]);
    e1000_write(E1000_TDBAH, 0);
    e1000_write(E1000_TDLEN, E1000_NUM_TX_DESC * sizeof(struct e1000_tx_desc));
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);

    net_state.tx_cur = 0;

    // Enable transmitter with recommended settings
    uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP | (15 << 4) | (64 << 12);
    e1000_write(E1000_TCTL, tctl);

    // Set Inter-Packet Gap (recommended values: 10, 10, 10)
    e1000_write(E1000_TIPG, 10 | (10 << 10) | (10 << 20));
}

// ===== Public: Initialize E1000 =====
void e1000_init(void) {
    net_memset(&net_state, 0, sizeof(NetState));

    // Default IP config for QEMU user-mode networking
    net_state.ip[0] = 10; net_state.ip[1] = 0;
    net_state.ip[2] = 2;  net_state.ip[3] = 15;
    net_state.gateway_ip[0] = 10; net_state.gateway_ip[1] = 0;
    net_state.gateway_ip[2] = 2;  net_state.gateway_ip[3] = 2;
    net_state.dns_server[0] = 10; net_state.dns_server[1] = 0;
    net_state.dns_server[2] = 2;  net_state.dns_server[3] = 3;  // QEMU DNS
    net_state.ip_id = 0x1234;

    if (!e1000_pci_find()) {
        net_state.detected = 0;
        return;
    }
    net_state.detected = 1;

    // Enable PCI bus mastering
    e1000_pci_enable();

    // Read BAR0 (MMIO base)
    uint32_t bar0 = pci_config_read(net_state.pci_bus, net_state.pci_slot, net_state.pci_func, 0x10);
    net_state.mmio_base = bar0 & ~0xF; // Mask lower 4 bits (type/prefetchable flags)

    // Reset the device
    e1000_write(E1000_CTRL, E1000_CTRL_RST);
    // Small delay for reset
    for (volatile int i = 0; i < 100000; i++);

    // Disable interrupts (we use polling)
    e1000_write(E1000_IMC, 0xFFFFFFFF);
    e1000_read(E1000_ICR); // Clear pending

    // Set link up
    uint32_t ctrl = e1000_read(E1000_CTRL);
    ctrl |= E1000_CTRL_SLU;
    ctrl &= ~(1 << 3);  // Clear LRST
    ctrl &= ~(1 << 31); // Clear PHY_RST
    e1000_write(E1000_CTRL, ctrl);

    // Small delay for link to come up
    for (volatile int i = 0; i < 100000; i++);

    // Read MAC address
    e1000_read_mac();

    // Clear Multicast Table Array
    for (int i = 0; i < 128; i++) {
        e1000_write(E1000_MTA + (i * 4), 0);
    }

    // Init RX and TX
    e1000_rx_init();
    e1000_tx_init();

    // Check link status
    uint32_t status = e1000_read(E1000_STATUS);
    net_state.link_up = (status & 1) ? 1 : 0; // Bit 0 = Link Up
}

// ===== Public: Send a raw Ethernet frame =====
int e1000_send(const void* data, uint16_t len) {
    if (!net_state.detected || len > E1000_TX_BUF_SIZE) return -1;

    uint16_t cur = net_state.tx_cur;

    // Wait for descriptor to be available
    int timeout = 100000;
    while (!(tx_descs[cur].status & E1000_TXD_STAT_DD) && --timeout > 0);
    if (timeout <= 0) return -1;

    // Copy packet data into TX buffer
    net_memcpy(&tx_buffers[cur][0], data, len);

    tx_descs[cur].addr = (uint64_t)(uint32_t)&tx_buffers[cur][0];
    tx_descs[cur].length = len;
    tx_descs[cur].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    tx_descs[cur].status = 0;

    net_state.tx_cur = (cur + 1) % E1000_NUM_TX_DESC;
    e1000_write(E1000_TDT, net_state.tx_cur);

    return 0;
}

// ===== Build & Send ARP Request =====
static void net_send_arp_request(const uint8_t* target_ip) {
    struct eth_header* eth = (struct eth_header*)tx_packet_buf;
    struct arp_packet* arp = (struct arp_packet*)(tx_packet_buf + sizeof(struct eth_header));

    // Broadcast destination
    net_memset(eth->dst, 0xFF, 6);
    net_memcpy(eth->src, net_state.mac, 6);
    eth->ethertype = htons(ETH_TYPE_ARP);

    arp->htype = htons(1);       // Ethernet
    arp->ptype = htons(0x0800);  // IPv4
    arp->hlen = 6;
    arp->plen = 4;
    arp->oper = htons(1);        // ARP Request
    net_memcpy(arp->sha, net_state.mac, 6);
    net_memcpy(arp->spa, net_state.ip, 4);
    net_memset(arp->tha, 0, 6);
    net_memcpy(arp->tpa, target_ip, 4);

    e1000_send(tx_packet_buf, sizeof(struct eth_header) + sizeof(struct arp_packet));
}

// ===== Build & Send ARP Reply =====
static void net_send_arp_reply(const uint8_t* dst_mac, const uint8_t* dst_ip) {
    struct eth_header* eth = (struct eth_header*)tx_packet_buf;
    struct arp_packet* arp = (struct arp_packet*)(tx_packet_buf + sizeof(struct eth_header));

    net_memcpy(eth->dst, dst_mac, 6);
    net_memcpy(eth->src, net_state.mac, 6);
    eth->ethertype = htons(ETH_TYPE_ARP);

    arp->htype = htons(1);
    arp->ptype = htons(0x0800);
    arp->hlen = 6;
    arp->plen = 4;
    arp->oper = htons(2);        // ARP Reply
    net_memcpy(arp->sha, net_state.mac, 6);
    net_memcpy(arp->spa, net_state.ip, 4);
    net_memcpy(arp->tha, dst_mac, 6);
    net_memcpy(arp->tpa, dst_ip, 4);

    e1000_send(tx_packet_buf, sizeof(struct eth_header) + sizeof(struct arp_packet));
}

// ===== Handle incoming ARP =====
static void net_handle_arp(const uint8_t* pkt, uint16_t len) {
    if (len < sizeof(struct eth_header) + sizeof(struct arp_packet)) return;

    struct arp_packet* arp = (struct arp_packet*)(pkt + sizeof(struct eth_header));
    uint16_t oper = ntohs(arp->oper);

    if (oper == 1) {
        // ARP Request: is it asking for our IP?
        if (arp->tpa[0] == net_state.ip[0] && arp->tpa[1] == net_state.ip[1] &&
            arp->tpa[2] == net_state.ip[2] && arp->tpa[3] == net_state.ip[3]) {
            net_send_arp_reply(arp->sha, arp->spa);
        }
    } else if (oper == 2) {
        // ARP Reply: cache gateway MAC
        if (arp->spa[0] == net_state.gateway_ip[0] && arp->spa[1] == net_state.gateway_ip[1] &&
            arp->spa[2] == net_state.gateway_ip[2] && arp->spa[3] == net_state.gateway_ip[3]) {
            net_memcpy(net_state.gateway_mac, arp->sha, 6);
            net_state.gateway_mac_valid = 1;
        }
    }
}

// ===== Handle incoming ICMP =====
static void net_handle_icmp(const uint8_t* pkt, uint16_t len) {
    struct eth_header* eth = (struct eth_header*)pkt;
    struct ip_header* ip = (struct ip_header*)(pkt + sizeof(struct eth_header));
    uint16_t ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
    struct icmp_header* icmp = (struct icmp_header*)(pkt + sizeof(struct eth_header) + ip_hdr_len);

    uint16_t ip_total = ntohs(ip->total_len);
    (void)len;

    if (icmp->type == 8) {
        // ICMP Echo Request → send Echo Reply
        uint16_t icmp_data_len = ip_total - ip_hdr_len - sizeof(struct icmp_header);
        uint16_t reply_len = sizeof(struct eth_header) + ip_hdr_len + sizeof(struct icmp_header) + icmp_data_len;

        if (reply_len > sizeof(tx_packet_buf)) return;

        // Build reply
        struct eth_header* reth = (struct eth_header*)tx_packet_buf;
        net_memcpy(reth->dst, eth->src, 6);
        net_memcpy(reth->src, net_state.mac, 6);
        reth->ethertype = htons(ETH_TYPE_IP);

        struct ip_header* rip = (struct ip_header*)(tx_packet_buf + sizeof(struct eth_header));
        rip->ver_ihl = 0x45;
        rip->tos = 0;
        rip->total_len = htons(ip_hdr_len + sizeof(struct icmp_header) + icmp_data_len);
        rip->id = ip->id;
        rip->flags_frag = 0;
        rip->ttl = 64;
        rip->protocol = 1; // ICMP
        rip->checksum = 0;
        net_memcpy(rip->src, net_state.ip, 4);
        net_memcpy(rip->dst, ip->src, 4);
        rip->checksum = ip_checksum(rip, ip_hdr_len);

        struct icmp_header* ricmp = (struct icmp_header*)(tx_packet_buf + sizeof(struct eth_header) + ip_hdr_len);
        ricmp->type = 0; // Echo Reply
        ricmp->code = 0;
        ricmp->id = icmp->id;
        ricmp->seq = icmp->seq;
        ricmp->checksum = 0;

        // Copy ICMP data payload
        const uint8_t* icmp_data = pkt + sizeof(struct eth_header) + ip_hdr_len + sizeof(struct icmp_header);
        uint8_t* reply_data = tx_packet_buf + sizeof(struct eth_header) + ip_hdr_len + sizeof(struct icmp_header);
        net_memcpy(reply_data, icmp_data, icmp_data_len);

        ricmp->checksum = ip_checksum(ricmp, sizeof(struct icmp_header) + icmp_data_len);

        e1000_send(tx_packet_buf, reply_len);

    } else if (icmp->type == 0) {
        // ICMP Echo Reply — this is the response to our ping
        if (net_state.ping_active && ntohs(icmp->seq) == net_state.ping_seq) {
            net_state.ping_replied = 1;
        }
    }
}

// ===== Handle incoming UDP =====
static void net_handle_udp(const uint8_t* pkt, uint16_t len) {
    struct ip_header* ip = (struct ip_header*)(pkt + sizeof(struct eth_header));
    uint16_t ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
    struct udp_header* udp = (struct udp_header*)(pkt + sizeof(struct eth_header) + ip_hdr_len);

    uint16_t src_port = ntohs(udp->src_port);
    uint16_t udp_len = ntohs(udp->length);
    (void)len;

    if (src_port == 53 && net_state.dns_pending) {
        // DNS Response
        const uint8_t* dns_data = (const uint8_t*)udp + sizeof(struct udp_header);
        uint16_t dns_len = udp_len - sizeof(struct udp_header);

        if (dns_len < 12) return;

        // Check transaction ID
        uint16_t txid = (dns_data[0] << 8) | dns_data[1];
        if (txid != net_state.dns_txid) return;

        // Check flags: QR bit must be set (response)
        if (!(dns_data[2] & 0x80)) return;

        // Check RCODE (lower 4 bits of byte 3)
        if ((dns_data[3] & 0x0F) != 0) {
            net_state.dns_pending = 0;
            return; // Error response
        }

        uint16_t qdcount = (dns_data[4] << 8) | dns_data[5];
        uint16_t ancount = (dns_data[6] << 8) | dns_data[7];

        if (ancount == 0) {
            net_state.dns_pending = 0;
            return;
        }

        // Skip header (12 bytes)
        int pos = 12;

        // Skip question section
        for (uint16_t q = 0; q < qdcount; q++) {
            while (pos < (int)dns_len) {
                if (dns_data[pos] == 0) { pos++; break; }
                if ((dns_data[pos] & 0xC0) == 0xC0) { pos += 2; break; }
                pos += dns_data[pos] + 1;
            }
            pos += 4; // Skip QTYPE + QCLASS
        }

        // Parse answers — look for A record (type 1)
        for (uint16_t a = 0; a < ancount && pos < (int)dns_len; a++) {
            while (pos < (int)dns_len) {
                if (dns_data[pos] == 0) { pos++; break; }
                if ((dns_data[pos] & 0xC0) == 0xC0) { pos += 2; break; }
                pos += dns_data[pos] + 1;
            }

            if (pos + 10 > (int)dns_len) break;

            uint16_t rtype = (dns_data[pos] << 8) | dns_data[pos + 1];
            uint16_t rdlength = (dns_data[pos + 8] << 8) | dns_data[pos + 9];
            pos += 10; // Skip TYPE(2) + CLASS(2) + TTL(4) + RDLENGTH(2)

            if (rtype == 1 && rdlength == 4 && pos + 4 <= (int)dns_len) {
                // A record — IPv4 address
                net_state.dns_result[0] = dns_data[pos];
                net_state.dns_result[1] = dns_data[pos + 1];
                net_state.dns_result[2] = dns_data[pos + 2];
                net_state.dns_result[3] = dns_data[pos + 3];
                net_state.dns_resolved = 1;
                net_state.dns_pending = 0;
                return;
            }

            pos += rdlength; // Skip RDATA
        }

        net_state.dns_pending = 0; // No A record found
    }
}

// ===== TCP Checksum (with pseudo-header) =====
static uint16_t tcp_checksum(const uint8_t* src_ip, const uint8_t* dst_ip,
                             const void* tcp_seg, uint16_t tcp_len) {
    uint32_t sum = 0;
    const uint16_t* p;

    // Pseudo-header
    p = (const uint16_t*)src_ip;
    sum += p[0]; sum += p[1];
    p = (const uint16_t*)dst_ip;
    sum += p[0]; sum += p[1];
    sum += htons(6); // Protocol TCP
    sum += htons(tcp_len);

    // TCP segment
    p = (const uint16_t*)tcp_seg;
    int remaining = tcp_len;
    while (remaining > 1) {
        sum += *p++;
        remaining -= 2;
    }
    if (remaining == 1) {
        sum += *(const uint8_t*)p;
    }

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

// ===== Send a TCP packet =====
// (Removed old TCP send functions, moved below net_get_mac_for_ip)


// ===== Handle incoming TCP =====
static void net_handle_tcp(const uint8_t* pkt, uint16_t len) {
    struct ip_header* ip = (struct ip_header*)(pkt + sizeof(struct eth_header));
    uint16_t ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
    struct tcp_header* tcp = (struct tcp_header*)(pkt + sizeof(struct eth_header) + ip_hdr_len);

    uint16_t src_port = ntohs(tcp->src_port);
    uint16_t dst_port = ntohs(tcp->dst_port);
    uint32_t seq = ntohl(tcp->seq);
    uint32_t ack = ntohl(tcp->ack);
    uint8_t flags = tcp->flags;
    uint16_t tcp_hdr_len_bytes = ((tcp->data_offset >> 4) & 0xF) * 4;

    uint16_t ip_total = ntohs(ip->total_len);
    uint16_t tcp_data_len = ip_total - ip_hdr_len - tcp_hdr_len_bytes;

    (void)len;

    // Only handle packets for our active TCP connection
    if (net_state.tcp_state == TCP_STATE_CLOSED) return;
    if (src_port != net_state.tcp_remote_port || dst_port != net_state.tcp_local_port) return;

    if (flags & TCP_RST) {
        net_state.tcp_state = TCP_STATE_CLOSED;
        net_state.http_done = 1;
        return;
    }

    if (net_state.tcp_state == TCP_STATE_SYN_SENT) {
        // Expecting SYN-ACK
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            net_state.tcp_remote_seq = seq + 1;
            net_state.tcp_local_seq = ack;
            net_state.tcp_state = TCP_STATE_ESTABLISHED;
            // Send ACK
            net_send_tcp(TCP_ACK, NULL, 0);
        }
    } else if (net_state.tcp_state == TCP_STATE_ESTABLISHED) {
        // Handle data
        if (tcp_data_len > 0) {
            const uint8_t* data = pkt + sizeof(struct eth_header) + ip_hdr_len + tcp_hdr_len_bytes;

            // Only accept in-order data
            if (seq == net_state.tcp_remote_seq) {
                // Copy to HTTP buffer
                if (net_state.http_buf && net_state.http_buf_len + tcp_data_len < HTTP_BUF_SIZE) {
                    net_memcpy(net_state.http_buf + net_state.http_buf_len, data, tcp_data_len);
                    net_state.http_buf_len += tcp_data_len;
                }
                net_state.tcp_remote_seq += tcp_data_len;
            }
            // ACK the data
            net_send_tcp(TCP_ACK, NULL, 0);
        }

        // Handle FIN
        if (flags & TCP_FIN) {
            net_state.tcp_remote_seq++;
            net_send_tcp(TCP_ACK | TCP_FIN, NULL, 0);
            net_state.tcp_local_seq++;
            net_state.tcp_state = TCP_STATE_CLOSED;
            net_state.http_done = 1;
        }
    } else if (net_state.tcp_state == TCP_STATE_FIN_WAIT) {
        if (flags & TCP_ACK) {
            if (flags & TCP_FIN) {
                net_state.tcp_remote_seq++;
                net_send_tcp(TCP_ACK, NULL, 0);
            }
            net_state.tcp_state = TCP_STATE_CLOSED;
            net_state.http_done = 1;
        }
    }
}

// ===== Handle incoming IP packet =====
static void net_handle_ip(const uint8_t* pkt, uint16_t len) {
    if (len < sizeof(struct eth_header) + sizeof(struct ip_header)) return;

    struct ip_header* ip = (struct ip_header*)(pkt + sizeof(struct eth_header));
    uint16_t ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;

    if (ip->protocol == 1) {
        // ICMP
        if (len >= sizeof(struct eth_header) + ip_hdr_len + sizeof(struct icmp_header)) {
            net_handle_icmp(pkt, len);
        }
    } else if (ip->protocol == 17) {
        // UDP
        if (len >= sizeof(struct eth_header) + ip_hdr_len + sizeof(struct udp_header)) {
            net_handle_udp(pkt, len);
        }
    } else if (ip->protocol == 6) {
        // TCP
        if (len >= sizeof(struct eth_header) + ip_hdr_len + sizeof(struct tcp_header)) {
            net_handle_tcp(pkt, len);
        }
    }
}

// ===== Handle a received packet =====
static void net_handle_packet(const uint8_t* pkt, uint16_t len) {
    if (len < sizeof(struct eth_header)) return;

    struct eth_header* eth = (struct eth_header*)pkt;
    uint16_t type = ntohs(eth->ethertype);

    if (type == ETH_TYPE_ARP) {
        net_handle_arp(pkt, len);
    } else if (type == ETH_TYPE_IP) {
        net_handle_ip(pkt, len);
    }
}

// ===== Public: Poll for received packets =====
void e1000_poll(void) {
    if (!net_state.detected) return;

    while (1) {
        uint16_t cur = net_state.rx_cur;
        if (!(rx_descs[cur].status & E1000_RXD_STAT_DD)) break;

        uint16_t len = rx_descs[cur].length;
        uint8_t* buf = rx_buffers[cur];

        net_handle_packet(buf, len);

        // Reset descriptor
        rx_descs[cur].status = 0;
        uint16_t old_cur = cur;
        net_state.rx_cur = (cur + 1) % E1000_NUM_RX_DESC;
        e1000_write(E1000_RDT, old_cur);
    }
}

// ===== Public: Send an ICMP Echo Request (ping) =====
void net_send_ping(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3) {
    if (!net_state.detected) return;

    uint8_t target_ip[4] = {ip0, ip1, ip2, ip3};

    // Check if target is the gateway or on local subnet
    // For simplicity, always ARP for the target (or gateway)
    // In QEMU user-mode, all targets go through the gateway
    uint8_t* next_hop = target_ip;

    // If not on local subnet, use gateway
    if (target_ip[0] != net_state.ip[0] || target_ip[1] != net_state.ip[1] ||
        target_ip[2] != net_state.ip[2]) {
        next_hop = net_state.gateway_ip;
    }

    // ARP for next hop if needed
    if (!net_state.gateway_mac_valid ||
        (next_hop[0] != net_state.gateway_ip[0] || next_hop[1] != net_state.gateway_ip[1] ||
         next_hop[2] != net_state.gateway_ip[2] || next_hop[3] != net_state.gateway_ip[3])) {
        // For non-gateway, we just use gateway in QEMU user-mode
        next_hop = net_state.gateway_ip;
    }

    if (!net_state.gateway_mac_valid) {
        // Send ARP request for gateway
        net_send_arp_request(net_state.gateway_ip);

        // Poll for ARP reply (with timeout)
        for (int i = 0; i < 500000 && !net_state.gateway_mac_valid; i++) {
            e1000_poll();
        }

        if (!net_state.gateway_mac_valid) {
            // Could not resolve gateway MAC
            return;
        }
    }

    // Build ICMP echo request
    net_state.ping_seq++;
    net_state.ping_active = 1;
    net_state.ping_replied = 0;

    uint16_t icmp_data_len = 32; // 32 bytes of payload
    uint16_t ip_total = 20 + sizeof(struct icmp_header) + icmp_data_len;
    uint16_t total_len = sizeof(struct eth_header) + ip_total;

    net_memset(tx_packet_buf, 0, total_len);

    // Ethernet header
    struct eth_header* eth = (struct eth_header*)tx_packet_buf;
    net_memcpy(eth->dst, net_state.gateway_mac, 6);
    net_memcpy(eth->src, net_state.mac, 6);
    eth->ethertype = htons(ETH_TYPE_IP);

    // IP header
    struct ip_header* ip = (struct ip_header*)(tx_packet_buf + sizeof(struct eth_header));
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons(ip_total);
    ip->id = htons(net_state.ping_seq);
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = 1; // ICMP
    ip->checksum = 0;
    net_memcpy(ip->src, net_state.ip, 4);
    ip->dst[0] = ip0; ip->dst[1] = ip1; ip->dst[2] = ip2; ip->dst[3] = ip3;
    ip->checksum = ip_checksum(ip, 20);

    // ICMP header
    struct icmp_header* icmp = (struct icmp_header*)(tx_packet_buf + sizeof(struct eth_header) + 20);
    icmp->type = 8;  // Echo Request
    icmp->code = 0;
    icmp->id = htons(0xBEEF);
    icmp->seq = htons(net_state.ping_seq);
    icmp->checksum = 0;

    // Fill ICMP payload
    uint8_t* payload = tx_packet_buf + sizeof(struct eth_header) + 20 + sizeof(struct icmp_header);
    for (uint16_t i = 0; i < icmp_data_len; i++) {
        payload[i] = (uint8_t)(i & 0xFF);
    }

    icmp->checksum = ip_checksum(icmp, sizeof(struct icmp_header) + icmp_data_len);

    e1000_send(tx_packet_buf, total_len);
}

// ===== Ensure gateway MAC is resolved =====
static int net_ensure_gateway_mac(void) {
    if (net_state.gateway_mac_valid) return 1;

    net_send_arp_request(net_state.gateway_ip);
    for (int i = 0; i < 500000 && !net_state.gateway_mac_valid; i++) {
        e1000_poll();
    }
    return net_state.gateway_mac_valid;
}

// ===== Resolve MAC for an IP (Local or Gateway) =====
static int net_get_mac_for_ip(const uint8_t* ip, uint8_t* out_mac) {
    if (!net_state.detected) return 0;

    // Check if on local subnet (10.0.2.x assumed)
    int local = (ip[0] == net_state.ip[0] && ip[1] == net_state.ip[1] && ip[2] == net_state.ip[2]);
    
    if (!local) {
        // Route through gateway
        if (!net_ensure_gateway_mac()) return 0;
        net_memcpy(out_mac, net_state.gateway_mac, 6);
        return 1;
    }

    // Local subnet (currently we only support gateway MAC for simplicity in QEMU user-net)
    // Most QEMU user-net services respond to the backend's MAC which is the same as gateway's.
    if (!net_ensure_gateway_mac()) return 0;
    net_memcpy(out_mac, net_state.gateway_mac, 6);
    return 1;
}

// --- Send a TCP packet --- (internal helper)
// --- Send a TCP packet ---
static void net_send_tcp(uint8_t flags, const void* payload, uint16_t payload_len) {
    uint8_t dst_mac[6];
    if (!net_get_mac_for_ip(net_state.tcp_remote_ip, dst_mac)) return;
    
    uint16_t tcp_hdr_len = 20;
    uint16_t tcp_total = tcp_hdr_len + payload_len;
    uint16_t ip_total = 20 + tcp_total;
    uint16_t frame_len = sizeof(struct eth_header) + ip_total;

    if (frame_len > sizeof(tx_packet_buf)) return;
    net_memset(tx_packet_buf, 0, frame_len);

    struct eth_header* eth = (struct eth_header*)tx_packet_buf;
    net_memcpy(eth->dst, dst_mac, 6);
    net_memcpy(eth->src, net_state.mac, 6);
    eth->ethertype = htons(ETH_TYPE_IP);

    struct ip_header* ip = (struct ip_header*)(tx_packet_buf + sizeof(struct eth_header));
    ip->ver_ihl = 0x45;
    ip->total_len = htons(ip_total);
    ip->id = htons(net_state.ip_id++);
    ip->flags_frag = htons(0x4000);
    ip->ttl = 64;
    ip->protocol = 6;
    ip->checksum = 0;
    net_memcpy(ip->src, net_state.ip, 4);
    net_memcpy(ip->dst, net_state.tcp_remote_ip, 4);
    ip->checksum = ip_checksum(ip, 20);

    struct tcp_header* tcp = (struct tcp_header*)(tx_packet_buf + sizeof(struct eth_header) + 20);
    tcp->src_port = htons(net_state.tcp_local_port);
    tcp->dst_port = htons(net_state.tcp_remote_port);
    tcp->seq = htonl(net_state.tcp_local_seq);
    tcp->ack = htonl(net_state.tcp_remote_seq);
    tcp->data_offset = (tcp_hdr_len / 4) << 4;
    tcp->flags = flags;
    tcp->window = htons(8192);
    tcp->checksum = 0;
    tcp->urgent = 0;

    if (payload && payload_len > 0)
        net_memcpy(tx_packet_buf + sizeof(struct eth_header) + 20 + tcp_hdr_len, payload, payload_len);

    tcp->checksum = tcp_checksum(net_state.ip, net_state.tcp_remote_ip, tcp, tcp_total);
    e1000_send(tx_packet_buf, frame_len);
}

// ===== Send a UDP packet =====
static int net_send_udp(const uint8_t* dst_ip, uint16_t src_port, uint16_t dst_port,
                        const void* payload, uint16_t payload_len) {
    if (!net_state.detected) return -1;
    if (!net_ensure_gateway_mac()) return -1;

    uint16_t udp_len = sizeof(struct udp_header) + payload_len;
    uint16_t ip_total = 20 + udp_len;
    uint16_t total_len = sizeof(struct eth_header) + ip_total;

    if (total_len > sizeof(tx_packet_buf)) return -1;

    net_memset(tx_packet_buf, 0, total_len);

    // Ethernet
    uint8_t dst_mac[6];
    if (!net_get_mac_for_ip(dst_ip, dst_mac)) return -1;

    struct eth_header* eth = (struct eth_header*)tx_packet_buf;
    net_memcpy(eth->dst, dst_mac, 6);
    net_memcpy(eth->src, net_state.mac, 6);
    eth->ethertype = htons(ETH_TYPE_IP);

    // IP
    struct ip_header* ip = (struct ip_header*)(tx_packet_buf + sizeof(struct eth_header));
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons(ip_total);
    ip->id = htons(net_state.ip_id++);
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = 17; // UDP
    ip->checksum = 0;
    net_memcpy(ip->src, net_state.ip, 4);
    net_memcpy(ip->dst, dst_ip, 4);
    ip->checksum = ip_checksum(ip, 20);

    // UDP
    struct udp_header* udp = (struct udp_header*)(tx_packet_buf + sizeof(struct eth_header) + 20);
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons(udp_len);
    udp->checksum = 0; // Optional for UDP over IPv4

    // Payload
    net_memcpy(tx_packet_buf + sizeof(struct eth_header) + 20 + sizeof(struct udp_header),
               payload, payload_len);

    return e1000_send(tx_packet_buf, total_len);
}

// ===== Build DNS Query =====
static int dns_build_query(uint8_t* buf, int max_len, const char* hostname, uint16_t txid) {
    // DNS header: 12 bytes
    if (max_len < 12) return -1;

    int pos = 0;
    // Transaction ID
    buf[pos++] = (txid >> 8) & 0xFF;
    buf[pos++] = txid & 0xFF;
    // Flags: standard query, recursion desired
    buf[pos++] = 0x01; // RD = 1
    buf[pos++] = 0x00;
    // QDCOUNT = 1
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;
    // ANCOUNT = 0
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    // NSCOUNT = 0
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    // ARCOUNT = 0
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    // QNAME: encode hostname as DNS labels
    const char* p = hostname;
    while (*p) {
        // Find next dot or end
        const char* dot = p;
        int label_len = 0;
        while (*dot && *dot != '.') { dot++; label_len++; }

        if (label_len == 0 || label_len > 63) return -1;
        if (pos + 1 + label_len >= max_len) return -1;

        buf[pos++] = (uint8_t)label_len;
        for (int i = 0; i < label_len; i++) {
            buf[pos++] = p[i];
        }

        p = dot;
        if (*p == '.') p++;
    }
    buf[pos++] = 0; // Null terminator

    // QTYPE: A (1)
    if (pos + 4 > max_len) return -1;
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;
    // QCLASS: IN (1)
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;

    return pos;
}

// ===== Public: Resolve hostname to IP via DNS =====
int net_dns_resolve(const char* hostname, uint8_t* out_ip) {
    if (!net_state.detected) return 0;

    // Build DNS query
    static uint8_t dns_buf[256];
    net_state.dns_txid++;
    int qlen = dns_build_query(dns_buf, sizeof(dns_buf), hostname, net_state.dns_txid);
    if (qlen < 0) return 0;

    net_state.dns_resolved = 0;
    net_state.dns_pending = 1;

    // Send DNS query via UDP to the DNS server (port 53)
    if (net_send_udp(net_state.dns_server, 12345, 53, dns_buf, qlen) < 0) {
        net_state.dns_pending = 0;
        return 0;
    }

    // Poll for response with timeout
    for (int i = 0; i < 40000000 && !net_state.dns_resolved; i++) {
        e1000_poll();
    }

    net_state.dns_pending = 0;

    if (net_state.dns_resolved) {
        out_ip[0] = net_state.dns_result[0];
        out_ip[1] = net_state.dns_result[1];
        out_ip[2] = net_state.dns_result[2];
        out_ip[3] = net_state.dns_result[3];
        return 1;
    }

    return 0;
}

// (Removed redundant net_strlen)

// ===== Static HTTP receive buffer =====
static uint8_t http_recv_buf[HTTP_BUF_SIZE];

// ===== Ephemeral port counter =====
static uint16_t next_ephemeral_port = 49152;

// ===== Public: HTTP GET (with Redirect Support) =====
int net_http_get(const char* host, const char* path, uint8_t* out_buf, int max_len) {
    if (!net_state.detected || !net_state.link_up) return -1;

    char curr_host[128];
    char curr_path[256];
    int redirect_count = 0;

    int hl = net_strlen(host); if (hl > 127) hl = 127;
    net_memcpy(curr_host, host, hl); curr_host[hl] = 0;

    int pl = net_strlen(path); if (pl > 255) pl = 255;
    net_memcpy(curr_path, path, pl); curr_path[pl] = 0;

    while (redirect_count < 5) {
        // Ensure gateway MAC is resolved
        if (!net_ensure_gateway_mac()) return -1;

        // Resolve hostname
        uint8_t server_ip[4];
        if (!net_dns_resolve(curr_host, server_ip)) return -1;

        // Setup TCP connection state
        net_state.tcp_state = TCP_STATE_CLOSED;
        net_memcpy(net_state.tcp_remote_ip, server_ip, 4);
        net_state.tcp_remote_port = 80;
        net_state.tcp_local_port = next_ephemeral_port++;
        if (next_ephemeral_port > 60000) next_ephemeral_port = 49152;

        net_state.tcp_local_seq = (uint32_t)net_state.tcp_local_port * 1000 + 1;
        net_state.tcp_remote_seq = 0;

        net_state.http_buf = http_recv_buf;
        net_state.http_buf_len = 0;
        net_state.http_done = 0;
        net_memset(http_recv_buf, 0, HTTP_BUF_SIZE);

        net_state.tcp_state = TCP_STATE_SYN_SENT;
        net_send_tcp(TCP_SYN, NULL, 0);
        net_state.tcp_local_seq++;

        for (int i = 0; i < 3000000 && net_state.tcp_state == TCP_STATE_SYN_SENT; i++) {
            e1000_poll();
        }
        if (net_state.tcp_state != TCP_STATE_ESTABLISHED) {
            net_state.tcp_state = TCP_STATE_CLOSED;
            return -1;
        }

        char request[512];
        int rp = 0;
        const char* g = "GET "; while (*g) request[rp++] = *g++;
        const char* pp = curr_path; while (*pp) request[rp++] = *pp++;
        const char* v = " HTTP/1.0\r\n"; while (*v) request[rp++] = *v++;
        const char* hh = "Host: "; while (*hh) request[rp++] = *hh++;
        const char* hp = curr_host; while (*hp) request[rp++] = *hp++;
        request[rp++] = '\r'; request[rp++] = '\n';
        const char* cc = "Connection: close\r\n\r\n"; while (*cc) request[rp++] = *cc++;

        net_send_tcp(TCP_ACK | TCP_PSH, request, (uint16_t)rp);
        net_state.tcp_local_seq += rp;

        for (int i = 0; i < 10000000 && !net_state.http_done; i++) {
            e1000_poll();
        }

        // --- Check for Redirect ---
        char* resp = (char*)http_recv_buf;
        // Verify we got "HTTP/1.x 30"
        if (net_state.http_buf_len > 12 && net_strncasecmp(resp, "HTTP/", 5) == 0) {
            char* status = net_strstr(resp, " ");
            if (status && (status[1] == '3' && (status[2] == '0' || status[2] == '1' || status[2] == '2'))) {
                // It's a redirect! Find Location header
                char* loc_hdr = net_strstr(resp, "\r\nLocation: ");
                if (!loc_hdr) loc_hdr = net_strstr(resp, "\nLocation: ");
                
                if (loc_hdr) {
                    char* loc_val = net_strstr(loc_hdr, ": ") + 2;
                    char* loc_end = net_strstr(loc_val, "\r");
                    if (!loc_end) loc_end = net_strstr(loc_val, "\n");

                    if (loc_val && loc_end) {
                        int vlen = loc_end - loc_val;
                        char new_url[256];
                        if (vlen > 255) vlen = 255;
                        net_memcpy(new_url, loc_val, vlen);
                        new_url[vlen] = 0;

                        // Parse new host and path
                        char* p = new_url;
                        if (net_strncasecmp(p, "http://", 7) == 0) {
                            p += 7;
                            char* slash = net_strstr(p, "/");
                            if (slash) {
                                int hlen = slash - p;
                                if (hlen > 127) hlen = 127;
                                net_memcpy(curr_host, p, hlen); curr_host[hlen] = 0;
                                int pathlen = net_strlen(slash);
                                if (pathlen > 255) pathlen = 255;
                                net_memcpy(curr_path, slash, pathlen); curr_path[pathlen] = 0;
                            } else {
                                int hlen = net_strlen(p);
                                if (hlen > 127) hlen = 127;
                                net_memcpy(curr_host, p, hlen); curr_host[hlen] = 0;
                                curr_path[0] = '/'; curr_path[1] = 0;
                            }
                        } else if (*p == '/') {
                            // Relative redirect
                            int pathlen = net_strlen(p);
                            if (pathlen > 255) pathlen = 255;
                            net_memcpy(curr_path, p, pathlen); curr_path[pathlen] = 0;
                        }

                        // Close old connection before redirecting
                        if (net_state.tcp_state != TCP_STATE_CLOSED) {
                            net_send_tcp(TCP_ACK | TCP_FIN, NULL, 0);
                            net_state.tcp_state = TCP_STATE_CLOSED;
                        }
                        
                        redirect_count++;
                        continue; // Do the redirect
                    }
                }
            }
        }

        // If we reach here, no redirect or max reached
        break;
    }

    int copy_len = net_state.http_buf_len;
    if (copy_len > max_len - 1) copy_len = max_len - 1;
    net_memcpy(out_buf, http_recv_buf, copy_len);
    out_buf[copy_len] = 0;

    if (net_state.tcp_state != TCP_STATE_CLOSED) {
        net_send_tcp(TCP_ACK | TCP_FIN, NULL, 0);
        net_state.tcp_local_seq++;
        net_state.tcp_state = TCP_STATE_FIN_WAIT;
        for (int i = 0; i < 500000 && net_state.tcp_state != TCP_STATE_CLOSED; i++) {
            e1000_poll();
        }
        net_state.tcp_state = TCP_STATE_CLOSED;
    }

    return copy_len;
}
