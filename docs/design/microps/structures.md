# データ構造定義（逆生成）

C構造体定義・定数・状態遷移を層ごとに整理する。

---

## ユーティリティ（util.h / platform/linux/sched.h）

### queue_entry — 侵入型リストノード
```c
struct queue_entry {
    struct queue_entry *next;
    /* data bytes exists after this structure. */
};
```

### queue — 侵入型 FIFO キュー
```c
struct queue {
    struct queue_entry *head;
    struct queue_entry *tail;
    size_t num;
};
```

### sched_task — 条件変数ベーススリープ/ウェイクアップ
```c
struct sched_task {
    struct sched_task *next;
    pthread_cond_t cond;
    int interrupted;
    int wc; /* wait count */
};
```

---

## ネットワークデバイス層（net.h）

### net_device
```c
struct net_device {
    struct net_device *next;
    struct net_iface *ifaces;
    unsigned int index;
    char name[IFNAMSIZ];        /* "net0", "net1", ... */
    uint16_t type;              /* NET_DEVICE_TYPE_* */
    uint16_t mtu;
    uint16_t flags;             /* NET_DEVICE_FLAG_* */
    uint16_t hlen;              /* ヘッダ長 */
    uint16_t alen;              /* アドレス長 */
    uint8_t addr[NET_DEVICE_ADDR_LEN];
    uint8_t broadcast[NET_DEVICE_ADDR_LEN];
    struct net_device_ops *ops;
    void *priv;                 /* ドライバ固有データ */
};
```

**定数**
```c
#define NET_DEVICE_TYPE_DUMMY     0x0000
#define NET_DEVICE_TYPE_LOOPBACK  0x0001
#define NET_DEVICE_TYPE_ETHERNET  0x0002

#define NET_DEVICE_FLAG_UP        0x0001
#define NET_DEVICE_FLAG_LOOPBACK  0x0010
#define NET_DEVICE_FLAG_BROADCAST 0x0020
#define NET_DEVICE_FLAG_P2P       0x0040
#define NET_DEVICE_FLAG_NEED_ARP  0x0100
```

### net_device_ops — 仮想関数テーブル
```c
struct net_device_ops {
    int (*open)(struct net_device *dev);
    int (*close)(struct net_device *dev);
    int (*output)(struct net_device *dev, uint16_t type,
                  const uint8_t *data, size_t len, const void *dst);
};
```

### net_iface — デバイスに紐付くインタフェース（基底）
```c
struct net_iface {
    struct net_iface *next;
    struct net_device *dev; /* back pointer */
    int family;             /* NET_IFACE_FAMILY_IP = 1 */
};
```

### net_protocol — プロトコル登録エントリ（内部）
```c
/* net.c 内部 */
struct net_protocol {
    struct net_protocol *next;
    uint16_t type;          /* Ethertype */
    lock_t lock;
    struct queue queue;     /* 受信キュー（softirq処理前） */
    net_protocol_handler_t handler;
};
```

---

## Ethernet層（ether.h）

### ether_hdr
```c
struct ether_hdr {
    uint8_t dst[ETHER_ADDR_LEN];  /* 6 bytes */
    uint8_t src[ETHER_ADDR_LEN];  /* 6 bytes */
    uint16_t type;                 /* Ethertype (ネットワークバイトオーダー) */
};
```

**定数**
```c
#define ETHER_ADDR_LEN         6
#define ETHER_HDR_SIZE         14
#define ETHER_FRAME_SIZE_MIN   60    /* FCS なし */
#define ETHER_FRAME_SIZE_MAX   1514  /* FCS なし */
#define ETHER_TYPE_IP          0x0800
#define ETHER_TYPE_ARP         0x0806
#define ETHER_TYPE_IPV6        0x86dd
```

---

## IP層（ip.h）

### ip_hdr
```c
struct ip_hdr {
    uint8_t vhl;       /* version(4bit) + header length(4bit) */
    uint8_t tos;
    uint16_t total;    /* 総長（ヘッダ+ペイロード） */
    uint16_t id;
    uint16_t offset;   /* フラグ(3bit) + フラグメントオフセット(13bit) */
    uint8_t ttl;
    uint8_t protocol;  /* IP_PROTOCOL_* */
    uint16_t sum;
    ip_addr_t src;
    ip_addr_t dst;
};
```

### ip_iface — IP インタフェース（net_iface を先頭フィールドで継承）
```c
struct ip_iface {
    struct net_iface iface; /* 必ず先頭に配置。NET_IFACE() マクロでキャスト可能 */
    struct ip_iface *next;
    ip_addr_t unicast;
    ip_addr_t netmask;
    ip_addr_t broadcast;
};
```

### ip_endp_t — IP エンドポイント（アドレス + ポート）
```c
typedef uint32_t ip_addr_t;

typedef struct {
    ip_addr_t addr;
    uint16_t port;
} ip_endp_t;
```

### ip_route — ルーティングエントリ（内部）
```c
/* ip.c 内部 */
struct ip_route {
    struct ip_route *next;
    ip_addr_t network;
    ip_addr_t netmask;
    ip_addr_t nexthop;
    struct ip_iface *iface;
};
```

**定数**
```c
#define IP_HDR_SIZE_MIN   20
#define IP_HDR_SIZE_MAX   60
#define IP_ADDR_LEN       4
#define IP_PROTOCOL_ICMP  1
#define IP_PROTOCOL_TCP   6
#define IP_PROTOCOL_UDP   17
#define IP_ENDP_DYNAMIC_PORT_MIN  49152
#define IP_ENDP_DYNAMIC_PORT_MAX  65535
```

---

## TCP層（tcp.c / tcp.h）

### tcp_hdr
```c
struct tcp_hdr {
    uint16_t src;   /* 送信元ポート */
    uint16_t dst;   /* 宛先ポート */
    uint32_t seq;   /* シーケンス番号 */
    uint32_t ack;   /* 確認応答番号 */
    uint8_t off;    /* データオフセット(4bit) + 予約(4bit) */
    uint8_t flg;    /* コントロールビット (FIN/SYN/RST/PSH/ACK/URG) */
    uint16_t wnd;   /* ウィンドウサイズ */
    uint16_t sum;   /* チェックサム */
    uint16_t up;    /* 緊急ポインタ */
};
```

**フラグ定数**
```c
#define TCP_FLG_FIN 0x01
#define TCP_FLG_SYN 0x02
#define TCP_FLG_RST 0x04
#define TCP_FLG_PSH 0x08
#define TCP_FLG_ACK 0x10
#define TCP_FLG_URG 0x20
```

### snd_vars — 送信変数（RFC 793 Section 3.2）
```c
struct snd_vars {
    uint32_t nxt; /* SND.NXT: 次に送る SEQ */
    uint32_t una; /* SND.UNA: 未確認の最初の SEQ */
    uint16_t wnd; /* SND.WND: 受信側ウィンドウ */
    uint16_t up;  /* SND.UP: 緊急ポインタ */
    uint32_t wl1; /* ウィンドウ更新判定用 SEQ */
    uint32_t wl2; /* ウィンドウ更新判定用 ACK */
};
```

### rcv_vars — 受信変数
```c
struct rcv_vars {
    uint32_t nxt; /* RCV.NXT: 期待する次の SEQ */
    uint16_t wnd; /* RCV.WND: 受信可能ウィンドウ */
    uint16_t up;  /* RCV.UP: 緊急ポインタ */
};
```

### tcp_pcb — プロトコルコントロールブロック
```c
struct tcp_pcb {
    struct queue_entry entry; /* backlog キューへの侵入ノード */
    int state;                /* TCP_STATE_* */
    int mode;                 /* TCP_PCB_MODE_SOCKET=1 */
    ip_endp_t local;
    ip_endp_t remote;
    struct snd_vars snd;
    uint32_t iss;             /* 初期送信シーケンス番号 */
    struct rcv_vars rcv;
    uint32_t irs;             /* 初期受信シーケンス番号 */
    uint16_t mss;             /* 最大セグメントサイズ */
    uint8_t buf[65535];       /* 受信バッファ */
    struct sched_task task;   /* スリープ/ウェイクアップ */
    struct queue queue;       /* 再送キュー */
    struct timeval tw_timer;  /* TIME_WAIT タイマー */
    struct tcp_pcb *parent;   /* listen PCB へのポインタ */
    struct queue backlog;     /* accept 待ち PCB キュー */
    int backlog_max;
};
```

**TCP状態定数**
```c
#define TCP_STATE_NONE         0
#define TCP_STATE_CLOSED       1
#define TCP_STATE_LISTEN       2
#define TCP_STATE_SYN_SENT     3
#define TCP_STATE_SYN_RECEIVED 4
#define TCP_STATE_ESTABLISHED  5
#define TCP_STATE_FIN_WAIT1    6
#define TCP_STATE_FIN_WAIT2    7
#define TCP_STATE_CLOSE_WAIT   8
#define TCP_STATE_CLOSING      9
#define TCP_STATE_LAST_ACK    10
#define TCP_STATE_TIME_WAIT   11
```

**TCP状態遷移図**
```
CLOSED ──(passive open)──> LISTEN
CLOSED ──(active open/SYN)──> SYN_SENT
LISTEN ──(SYN received)──> SYN_RECEIVED
SYN_SENT ──(SYN+ACK/ACK)──> ESTABLISHED
SYN_RECEIVED ──(ACK)──> ESTABLISHED ──> backlog へ追加
ESTABLISHED ──(FIN sent)──> FIN_WAIT1
ESTABLISHED ──(FIN recv)──> CLOSE_WAIT ──(FIN sent)──> LAST_ACK ──> CLOSED
FIN_WAIT1 ──(ACK)──> FIN_WAIT2 ──(FIN recv/ACK)──> TIME_WAIT ──(30s)──> CLOSED
```

**タイマー定数**
```c
#define TCP_DEFAULT_RTO       200000  /* 再送タイムアウト初期値: 200ms */
#define TCP_RETRANS_DEADLINE  12      /* 再送期限: 12秒 */
#define TCP_TIMEWAIT_SEC      30      /* TIME_WAIT 時間（2MSL 代替）*/
#define TCP_PCB_SIZE          16      /* PCB プール最大数 */
```

### tcp_queue_entry — 再送キューエントリ
```c
struct tcp_queue_entry {
    struct queue_entry entry;     /* 侵入ノード */
    struct timeval first;         /* 初回送信時刻 */
    struct timeval last;          /* 直近送信時刻 */
    unsigned int rto;             /* 再送タイムアウト（マイクロ秒） */
    uint32_t seq;
    uint8_t flg;
    size_t len;
    /* 以降にデータバイト列が続く */
};
```

### seg_info — セグメント情報（内部解析用）
```c
struct seg_info {
    uint32_t seq;
    uint32_t ack;
    uint16_t len;  /* SYN/FIN も 1 としてカウント */
    uint16_t wnd;
    uint16_t up;
};
```

---

## UDP層（udp.c / udp.h）

### udp_hdr
```c
struct udp_hdr {
    uint16_t src;  /* 送信元ポート */
    uint16_t dst;  /* 宛先ポート */
    uint16_t len;  /* ヘッダ+データ長 */
    uint16_t sum;  /* チェックサム（擬似ヘッダ込み） */
};
```

### udp_pcb — UDP プロトコルコントロールブロック
```c
struct udp_pcb {
    int state;              /* UDP_PCB_STATE_* */
    ip_endp_t local;
    struct queue queue;     /* 受信キュー */
    struct sched_task task;
};
```

### udp_queue_entry — 受信キューエントリ
```c
struct udp_queue_entry {
    struct queue_entry entry;  /* 侵入ノード */
    ip_endp_t remote;
    uint16_t len;
    /* 以降にデータバイト列が続く */
};
```

---

## ソケット API層（sock.c / sock.h）

### sock — ソケット記述子管理
```c
struct sock {
    int used;    /* 使用中フラグ */
    int family;  /* AF_INET */
    int type;    /* SOCK_STREAM / SOCK_DGRAM */
    int desc;    /* tcp_pcb / udp_pcb の記述子 */
};
```

### sockaddr_in — IPv4 ソケットアドレス
```c
struct sockaddr_in {
    unsigned short sin_family;  /* AF_INET */
    uint16_t sin_port;          /* ネットワークバイトオーダー */
    struct in_addr sin_addr;
};

struct in_addr {
    uint32_t s_addr;            /* ネットワークバイトオーダー */
};
```

---

## 疑似ヘッダ（TCP / UDP チェックサム用）

```c
/* tcp.c, udp.c 内部共通 */
struct pseudo_hdr {
    uint32_t src;
    uint32_t dst;
    uint8_t zero;
    uint8_t protocol;
    uint16_t len;
};
```

---

## プラットフォーム層（platform/linux/）

### irq_entry — 割り込みハンドラ登録エントリ
```c
/* intr.c 内部 */
struct irq_entry {
    struct irq_entry *next;
    unsigned int irq;      /* シグナル番号 */
    intr_isr_t isr;        /* ハンドラ関数ポインタ */
    int flags;             /* INTR_IRQ_SHARED=0x0001 */
    void *arg;
};
```

**シグナルマッピング**
```c
#define INTR_IRQ_SOFT   SIGUSR1   /* softirq（受信パケット処理） */
#define INTR_IRQ_USER   SIGUSR2   /* ユーザー定義 */
#define INTR_IRQ_TIMER  SIGALRM   /* タイマー割り込み */
```
