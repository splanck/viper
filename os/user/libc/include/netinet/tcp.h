/*
 * ViperOS C Library - netinet/tcp.h
 * TCP protocol definitions and socket options
 */

#ifndef _NETINET_TCP_H
#define _NETINET_TCP_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TCP socket options (for use with setsockopt/getsockopt at IPPROTO_TCP level)
 */
#define TCP_NODELAY         1   /* Don't delay send to coalesce packets (disable Nagle) */
#define TCP_MAXSEG          2   /* Set maximum segment size */
#define TCP_CORK            3   /* Control sending of partial frames (Linux) */
#define TCP_KEEPIDLE        4   /* Start keepalives after this period (seconds) */
#define TCP_KEEPINTVL       5   /* Interval between keepalives (seconds) */
#define TCP_KEEPCNT         6   /* Number of keepalives before death */
#define TCP_SYNCNT          7   /* Number of SYN retransmits */
#define TCP_LINGER2         8   /* Life time of orphaned FIN-WAIT-2 state */
#define TCP_DEFER_ACCEPT    9   /* Wake up listener only when data arrives */
#define TCP_WINDOW_CLAMP    10  /* Bound advertised window */
#define TCP_INFO            11  /* Information about connection */
#define TCP_QUICKACK        12  /* Quick ACK mode */
#define TCP_CONGESTION      13  /* Congestion control algorithm */
#define TCP_MD5SIG          14  /* TCP MD5 signature (RFC2385) */
#define TCP_THIN_LINEAR_TIMEOUTS 16 /* Use linear timeouts for thin streams */
#define TCP_THIN_DUPACK     17  /* Reduce dupACK threshold for thin streams */
#define TCP_USER_TIMEOUT    18  /* Time before aborting unacked data (ms) */
#define TCP_REPAIR          19  /* TCP socket repair */
#define TCP_REPAIR_QUEUE    20  /* Queue for repair mode */
#define TCP_QUEUE_SEQ       21  /* Sequence number for repair mode */
#define TCP_REPAIR_OPTIONS  22  /* Repair options for repair mode */
#define TCP_FASTOPEN        23  /* TCP Fast Open (RFC7413) */
#define TCP_TIMESTAMP       24  /* TCP timestamp */
#define TCP_NOTSENT_LOWAT   25  /* Not-sent low-water mark */
#define TCP_CC_INFO         26  /* Congestion control info */
#define TCP_SAVE_SYN        27  /* Save SYN packet */
#define TCP_SAVED_SYN       28  /* Get saved SYN packet */
#define TCP_REPAIR_WINDOW   29  /* Repair window data */
#define TCP_FASTOPEN_CONNECT 30 /* TFO connect */
#define TCP_ULP             31  /* Upper layer protocol */
#define TCP_MD5SIG_EXT      32  /* TCP MD5 signature with extension */
#define TCP_FASTOPEN_KEY    33  /* TFO key */
#define TCP_FASTOPEN_NO_COOKIE 34 /* TFO without cookie */
#define TCP_ZEROCOPY_RECEIVE 35 /* Zero-copy receive */
#define TCP_INQ             36  /* Get incoming queue size */

/* TCP connection states */
#define TCP_ESTABLISHED     1
#define TCP_SYN_SENT        2
#define TCP_SYN_RECV        3
#define TCP_FIN_WAIT1       4
#define TCP_FIN_WAIT2       5
#define TCP_TIME_WAIT       6
#define TCP_CLOSE           7
#define TCP_CLOSE_WAIT      8
#define TCP_LAST_ACK        9
#define TCP_LISTEN          10
#define TCP_CLOSING         11

/* TCP header flags */
#define TH_FIN              0x01
#define TH_SYN              0x02
#define TH_RST              0x04
#define TH_PUSH             0x08
#define TH_ACK              0x10
#define TH_URG              0x20
#define TH_ECE              0x40
#define TH_CWR              0x80

/* Compatibility aliases */
#define TCPOPT_EOL          0   /* End of options */
#define TCPOPT_NOP          1   /* No operation */
#define TCPOPT_MAXSEG       2   /* Maximum segment size */
#define TCPOPT_WINDOW       3   /* Window scale */
#define TCPOPT_SACK_PERMITTED 4 /* SACK permitted */
#define TCPOPT_SACK         5   /* SACK */
#define TCPOPT_TIMESTAMP    8   /* Timestamp */

#define TCPOLEN_MAXSEG      4
#define TCPOLEN_WINDOW      3
#define TCPOLEN_SACK_PERMITTED 2
#define TCPOLEN_TIMESTAMP   10

/* TCP header structure */
struct tcphdr {
    uint16_t th_sport;      /* Source port */
    uint16_t th_dport;      /* Destination port */
    uint32_t th_seq;        /* Sequence number */
    uint32_t th_ack;        /* Acknowledgment number */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t  th_x2:4;       /* Reserved */
    uint8_t  th_off:4;      /* Data offset (header length / 4) */
#else
    uint8_t  th_off:4;      /* Data offset */
    uint8_t  th_x2:4;       /* Reserved */
#endif
    uint8_t  th_flags;      /* Flags */
    uint16_t th_win;        /* Window size */
    uint16_t th_sum;        /* Checksum */
    uint16_t th_urp;        /* Urgent pointer */
};

/* Alternative naming convention (BSD style) */
#define tcp_seq uint32_t

/* TCP info structure for TCP_INFO socket option */
struct tcp_info {
    uint8_t  tcpi_state;            /* TCP state */
    uint8_t  tcpi_ca_state;         /* Congestion avoidance state */
    uint8_t  tcpi_retransmits;      /* Number of retransmits */
    uint8_t  tcpi_probes;           /* Probes sent */
    uint8_t  tcpi_backoff;          /* Backoff */
    uint8_t  tcpi_options;          /* TCP options */
    uint8_t  tcpi_snd_wscale : 4;   /* Send window scale */
    uint8_t  tcpi_rcv_wscale : 4;   /* Receive window scale */
    uint8_t  tcpi_delivery_rate_app_limited : 1; /* Delivery rate limited */

    uint32_t tcpi_rto;              /* Retransmission timeout (usec) */
    uint32_t tcpi_ato;              /* ACK timeout (usec) */
    uint32_t tcpi_snd_mss;          /* Send MSS */
    uint32_t tcpi_rcv_mss;          /* Receive MSS */

    uint32_t tcpi_unacked;          /* Unacked packets */
    uint32_t tcpi_sacked;           /* SACKed packets */
    uint32_t tcpi_lost;             /* Lost packets */
    uint32_t tcpi_retrans;          /* Retransmitted packets */
    uint32_t tcpi_fackets;          /* Forward ACKed packets */

    /* Times (msec) */
    uint32_t tcpi_last_data_sent;   /* Time since last data sent */
    uint32_t tcpi_last_ack_sent;    /* Time since last ACK sent (unused) */
    uint32_t tcpi_last_data_recv;   /* Time since last data received */
    uint32_t tcpi_last_ack_recv;    /* Time since last ACK received */

    /* Metrics */
    uint32_t tcpi_pmtu;             /* Path MTU */
    uint32_t tcpi_rcv_ssthresh;     /* Receive slow start threshold */
    uint32_t tcpi_rtt;              /* Round trip time (usec) */
    uint32_t tcpi_rttvar;           /* RTT variance (usec) */
    uint32_t tcpi_snd_ssthresh;     /* Send slow start threshold */
    uint32_t tcpi_snd_cwnd;         /* Send congestion window */
    uint32_t tcpi_advmss;           /* Advertised MSS */
    uint32_t tcpi_reordering;       /* Reordering metric */

    uint32_t tcpi_rcv_rtt;          /* Receive RTT (usec) */
    uint32_t tcpi_rcv_space;        /* Receive buffer space */

    uint32_t tcpi_total_retrans;    /* Total retransmissions */

    uint64_t tcpi_pacing_rate;      /* Pacing rate (bytes/sec) */
    uint64_t tcpi_max_pacing_rate;  /* Max pacing rate */
    uint64_t tcpi_bytes_acked;      /* Bytes ACKed */
    uint64_t tcpi_bytes_received;   /* Bytes received */
    uint32_t tcpi_segs_out;         /* Segments sent */
    uint32_t tcpi_segs_in;          /* Segments received */

    uint32_t tcpi_notsent_bytes;    /* Bytes not yet sent */
    uint32_t tcpi_min_rtt;          /* Minimum RTT (usec) */
    uint32_t tcpi_data_segs_in;     /* Data segments received */
    uint32_t tcpi_data_segs_out;    /* Data segments sent */

    uint64_t tcpi_delivery_rate;    /* Delivery rate (bytes/sec) */

    uint64_t tcpi_busy_time;        /* Busy time (usec) */
    uint64_t tcpi_rwnd_limited;     /* Rwnd limited time (usec) */
    uint64_t tcpi_sndbuf_limited;   /* Sndbuf limited time (usec) */

    uint32_t tcpi_delivered;        /* Packets delivered */
    uint32_t tcpi_delivered_ce;     /* Packets delivered with CE */

    uint64_t tcpi_bytes_sent;       /* Bytes sent */
    uint64_t tcpi_bytes_retrans;    /* Bytes retransmitted */
    uint32_t tcpi_dsack_dups;       /* Duplicate DSACKs */
    uint32_t tcpi_reord_seen;       /* Reorderings seen */
};

/* TCP congestion avoidance states */
#define TCP_CA_Open         0
#define TCP_CA_Disorder     1
#define TCP_CA_CWR          2
#define TCP_CA_Recovery     3
#define TCP_CA_Loss         4

/* TCP MD5 signature structure */
#define TCP_MD5SIG_MAXKEYLEN 80

struct tcp_md5sig {
    struct sockaddr_storage tcpm_addr;      /* Address */
    uint8_t  tcpm_flags;                    /* Flags */
    uint8_t  tcpm_prefixlen;                /* Address prefix length */
    uint16_t tcpm_keylen;                   /* Key length */
    uint32_t __tcpm_pad;                    /* Reserved */
    uint8_t  tcpm_key[TCP_MD5SIG_MAXKEYLEN]; /* Key */
};

/* TCP repair queues */
#define TCP_NO_QUEUE        0
#define TCP_RECV_QUEUE      1
#define TCP_SEND_QUEUE      2
#define TCP_QUEUES_NR       3

#ifdef __cplusplus
}
#endif

#endif /* _NETINET_TCP_H */
