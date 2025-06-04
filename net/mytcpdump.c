#include <pcap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <signal.h>

pcap_t *global_handle = NULL;

void signal_handler(int signum) {
    printf("\nReceived signal %d, stopping capture...\n", signum);
    if (global_handle) {
        pcap_breakloop(global_handle);
    }
}

void packet_handler(u_char *user_data, const struct pcap_pkthdr *pkthdr, const u_char *packet) {
    static int packet_count = 0;
    packet_count++;
    if (pkthdr->caplen < 14) {
        printf("Packet #%d: too short for Ethernet header\n", packet_count);
        return;
    }
    const u_char *ip_header = packet + 14;
    if (pkthdr->caplen < 14 + sizeof(struct iphdr)) {
        printf("Packet #%d: too short for IP header\n", packet_count);
        return;
    }
    struct iphdr *ip = (struct iphdr *)ip_header;
    char src_ip[INET_ADDRSTRLEN];
    char dst_ip[INET_ADDRSTRLEN];
    struct in_addr src_addr, dst_addr;
    src_addr.s_addr = ip->saddr;
    dst_addr.s_addr = ip->daddr;
    inet_ntop(AF_INET, &src_addr, src_ip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &dst_addr, dst_ip, INET_ADDRSTRLEN);
    if (strcmp(src_ip, "0.0.0.0") == 0 || strcmp(dst_ip, "0.0.0.0") == 0) {
        return;
    }
    switch (ip->protocol) {
        case IPPROTO_TCP: {
            printf("%02d:%02d:%02d.%06ld IP ", 
                ((int)(pkthdr->ts.tv_sec % 86400) / 3600 + 8)%24,
                (int)(pkthdr->ts.tv_sec % 3600) / 60,
                (int)(pkthdr->ts.tv_sec % 60),
                pkthdr->ts.tv_usec);
            if (pkthdr->caplen < 14 + (ip->ihl * 4) + sizeof(struct tcphdr)) {
                printf("%s > %s: tcp %d\n", src_ip, dst_ip, ntohs(ip->tot_len) - (ip->ihl * 4));
                return;
            }
            struct tcphdr *tcp = (struct tcphdr *)(ip_header + (ip->ihl * 4));
            int tcp_header_len = tcp->doff * 4;
            int data_len = ntohs(ip->tot_len) - (ip->ihl * 4) - tcp_header_len;
            printf("%s.%d > %s.%d: tcp %d\n", src_ip, ntohs(tcp->source), dst_ip, ntohs(tcp->dest), data_len);
            break;
        }
        case IPPROTO_UDP: {
            printf("%02d:%02d:%02d.%06ld IP ", 
                ((int)(pkthdr->ts.tv_sec % 86400) / 3600 + 8)%24,
                (int)(pkthdr->ts.tv_sec % 3600) / 60,
                (int)(pkthdr->ts.tv_sec % 60),
                pkthdr->ts.tv_usec);
            if (pkthdr->caplen < 14 + (ip->ihl * 4) + sizeof(struct udphdr)) {
                printf("%s > %s: udp %d\n", src_ip, dst_ip, ntohs(ip->tot_len) - (ip->ihl * 4));
                return;
            }
            struct udphdr *udp = (struct udphdr *)(ip_header + (ip->ihl * 4));
            int udp_data_len = ntohs(udp->len) - sizeof(struct udphdr);
            printf("%s.%d > %s.%d: UDP, length %d\n", src_ip, ntohs(udp->source), dst_ip, ntohs(udp->dest), udp_data_len);
            break;
        }
        case IPPROTO_ICMP:
            // printf("%s > %s: icmp", src_ip, dst_ip);
            break;
        default:
            // printf("%s > %s: proto %d", src_ip, dst_ip, ip->protocol);
            break;
    }
    // printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <interface> <filter>\n", argv[0]);
        printf("Example: %s eth0 \"tcp port 80\"\n", argv[0]);
        return 1;
    }
    char *interface = argv[1];
    char *filter = argv[2];
    char errbuf[PCAP_ERRBUF_SIZE];
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    printf("Starting packet capture on interface: %s with filter: %s\n", interface, filter);
    pcap_t *handle = pcap_open_live(interface, BUFSIZ, 1, 1000, errbuf);
    if (handle == NULL) {
        fprintf(stderr, "Error: pcap_open_live() failed: %s\n", errbuf);
        return 1;
    }
    global_handle = handle;
    bpf_u_int32 net;
    bpf_u_int32 mask;
    if (pcap_lookupnet(interface, &net, &mask, errbuf) == -1) {
        fprintf(stderr, "Warning: pcap_lookupnet() failed: %s\n", errbuf);
        net = 0;
        mask = 0;
    }
    struct bpf_program fp;
    if (pcap_compile(handle, &fp, filter, 0, net) == -1) {
        fprintf(stderr, "Error: pcap_compile() failed: %s\n", pcap_geterr(handle));
        pcap_close(handle);
        return 1;
    }
    if (pcap_setfilter(handle, &fp) == -1) {
        fprintf(stderr, "Error: pcap_setfilter() failed: %s\n", pcap_geterr(handle));
        pcap_freecode(&fp);
        pcap_close(handle);
        return 1;
    }
    printf("Capture started. Press Ctrl+C to stop.\n");
    int result = pcap_loop(handle, -1, packet_handler, NULL);
    if (result == -1) {
        fprintf(stderr, "Error: pcap_loop() failed: %s\n", pcap_geterr(handle));
    } else if (result == -2) {
        printf("Capture interrupted by user\n");
    }
    pcap_freecode(&fp);
    pcap_close(handle);
    printf("Capture finished.\n");
    return 0;
}