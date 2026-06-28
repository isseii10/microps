# API仕様書（逆生成）

C言語関数レベルの公開 API を層ごとに整理する。

---

## ソケット API 層（sock.h）

BSD ソケット互換の薄いラッパー。内部で `tcp_cmd_*` / `udp_cmd_*` に委譲する。

### sock_open
```c
int sock_open(int domain, int type, int protocol);
```
| 引数 | 説明 |
|---|---|
| `domain` | `AF_INET`(2) のみサポート |
| `type` | `SOCK_STREAM`(1)=TCP, `SOCK_DGRAM`(2)=UDP |
| `protocol` | 未使用（0 固定） |

**戻り値**: ソケット記述子（>=0）、失敗時 -1

---

### sock_close
```c
int sock_close(int desc);
```

---

### sock_bind
```c
int sock_bind(int desc, const struct sockaddr *addr, int addrlen);
```
`addr` は `struct sockaddr_in` にキャストして使用。

---

### sock_listen
```c
int sock_listen(int desc, int backlog);
```

---

### sock_accept
```c
int sock_accept(int desc, struct sockaddr *addr, int *addrlen);
```
**ブロッキング**。接続が来るまで `sched_task_sleep` で待機。

---

### sock_connect
```c
int sock_connect(int desc, const struct sockaddr *addr, int addrlen);
```
**ブロッキング**。3-way handshake 完了まで待機。

---

### sock_send / sock_recv
```c
ssize_t sock_send(int desc, const void *buf, size_t n);
ssize_t sock_recv(int desc, void *buf, size_t n);
```

---

### sock_sendto / sock_recvfrom
```c
ssize_t sock_sendto(int desc, const void *buf, size_t n,
                    const struct sockaddr *addr, int addrlen);
ssize_t sock_recvfrom(int desc, void *buf, size_t n,
                      struct sockaddr *addr, int *addrlen);
```
UDP 用。`addr` で送受信先エンドポイントを指定/取得。

---

## TCP コマンド層（tcp.h）

`sock_*` から直接呼び出される低レベル TCP インターフェース。

### tcp_cmd_socket
```c
int tcp_cmd_socket(void);
```
PCB を確保して記述子を返す。

### tcp_cmd_close
```c
int tcp_cmd_close(int desc);
```
FIN を送信して接続を閉じる。状態に応じて FIN_WAIT / LAST_ACK へ遷移。

### tcp_cmd_connect
```c
int tcp_cmd_connect(int desc, ip_endp_t remote);
```
アクティブオープン。SYN 送信 → ESTABLISHED まで **ブロッキング**。

### tcp_cmd_bind
```c
int tcp_cmd_bind(int desc, ip_endp_t local);
```

### tcp_cmd_listen
```c
int tcp_cmd_listen(int desc, int backlog);
```
LISTEN 状態へ遷移。

### tcp_cmd_accept
```c
int tcp_cmd_accept(int desc, ip_endp_t *remote);
```
backlog キューから ESTABLISHED 済み PCB を取得。キューが空なら **ブロッキング**。

### tcp_cmd_send
```c
ssize_t tcp_cmd_send(int desc, uint8_t *data, size_t len);
```
MSS 単位に分割して再送キューに登録後、`tcp_output()` を呼び出す。

### tcp_cmd_receive
```c
ssize_t tcp_cmd_receive(int desc, uint8_t *buf, size_t size);
```
受信バッファ（`pcb->buf`）にデータが届くまで **ブロッキング**。

### tcp_cmd_open（内部互換）
```c
int tcp_cmd_open(ip_endp_t local, ip_endp_t remote, int active);
```
`active=1`: アクティブオープン、`active=0`: パッシブオープン（LISTEN）。

---

## UDP コマンド層（udp.h）

### udp_cmd_open
```c
int udp_cmd_open(void);
```

### udp_cmd_close
```c
int udp_cmd_close(int desc);
```

### udp_cmd_bind
```c
int udp_cmd_bind(int desc, ip_endp_t local);
```

### udp_cmd_sendto
```c
ssize_t udp_cmd_sendto(int desc, uint8_t *data, size_t len, ip_endp_t remote);
```

### udp_cmd_recvfrom
```c
ssize_t udp_cmd_recvfrom(int desc, uint8_t *buf, size_t size, ip_endp_t *remote);
```
受信キューにデータが届くまで **ブロッキング**。

---

## IP層（ip.h）

### ip_output
```c
ssize_t ip_output(uint8_t protocol, const uint8_t *data, size_t len,
                  ip_addr_t src, ip_addr_t dst);
```
ルーティングテーブルを引いて適切な `ip_iface` から送信。

### ip_iface_alloc / ip_iface_register
```c
struct ip_iface *ip_iface_alloc(const char *addr, const char *netmask);
int ip_iface_register(struct net_device *dev, struct ip_iface *iface);
```

### ip_route_set_default_gateway
```c
int ip_route_set_default_gateway(struct ip_iface *iface, const char *gateway);
```

### ip_protocol_register
```c
int ip_protocol_register(uint8_t protocol, ip_protocol_handler_t handler);
```
上位プロトコル（TCP=6, UDP=17, ICMP=1）の受信ハンドラを登録。

---

## ネットワークデバイス層（net.h）

### net_device_alloc / net_device_register
```c
struct net_device *net_device_alloc(void);
int net_device_register(struct net_device *dev);
```

### net_device_output
```c
int net_device_output(struct net_device *dev, uint16_t type,
                      const uint8_t *data, size_t len, const void *dst);
```

### net_protocol_register
```c
int net_protocol_register(uint16_t type, net_protocol_handler_t handler);
```
Ethertype に対応するハンドラを登録（IP=0x0800, ARP=0x0806）。

### net_init / net_run / net_shutdown
```c
int net_init(void);
int net_run(void);
int net_shutdown(void);
```
`net_init()` → デバイス・インタフェース登録 → `net_run()` の順で使用。

---

## ARP（arp.h）

### arp_resolve
```c
int arp_resolve(struct net_iface *iface, ip_addr_t pa, uint8_t *ha);
```

| 戻り値 | 意味 |
|---|---|
| `ARP_RESOLVE_FOUND` (1) | キャッシュ命中。`ha` に MAC アドレスが入る |
| `ARP_RESOLVE_INCOMPLETE` (0) | ARP Request 送信済み。再試行が必要 |
| `ARP_RESOLVE_ERROR` (-1) | エラー |

---

## ICMP（icmp.h）

### icmp_output
```c
int icmp_output(uint8_t type, uint8_t code, uint32_t val,
                const uint8_t *data, size_t len,
                ip_addr_t src, ip_addr_t dst);
```

---

## エラー規約

| 状況 | 戻り値 | errno |
|---|---|---|
| 成功 | 0 または正の値 | 変更なし |
| 一般エラー | -1 | 変更なし |
| シグナルによる割り込み | -1 | `EINTR` |
| ブロッキング呼び出し中断 | -1 | `EINTR` |
