# データフロー図（逆生成）

## パケット受信フロー

```
[NIC / TAP デバイス]
        │  (ether_tap: read syscall or loopback)
        ▼
[ether_input()]              ←── ether.c
  フレーム検証・パース
        │  net_input(type, data, len, dev)
        ▼
[net_input()]                ←── net.c
  プロトコルキューに enqueue
  intr_raise(INTR_IRQ_SOFT)  ──→ SIGUSR1 シグナルで割り込みスレッドへ
        │
        ▼  (割り込みスレッドが sigwait で受信)
[net_softirq_handler()]      ←── net.c
  プロトコルキューから dequeue
  proto->handler() を呼び出し
        │
   ┌────┴────┐
   ▼         ▼
[ip_input()]  [arp_input()]   ←── ip.c / arp.c
  IP ヘッダ検証・ルーティング
        │
   ┌────┴────────┐
   ▼             ▼
[tcp_input()]  [udp_input()]  ←── tcp.c / udp.c
  チェックサム検証
  PCB を探索して tcp_segment_arrives() / udp_pcb 受信キューへ
        │
        ▼
  sched_task_wakeup() でアプリスレッドを起こす
        │
        ▼
[tcp_cmd_receive() / udp_cmd_recvfrom()]  ← アプリケーションスレッド
```

### シーケンス図（受信）

```mermaid
sequenceDiagram
    participant D as ether_tap (driver)
    participant N as net.c (softirq)
    participant I as ip_input
    participant T as tcp_input
    participant P as tcp_pcb (sched_task)
    participant A as Application

    D->>N: net_input(IP, frame, dev)
    N->>N: enqueue → intr_raise(SIGUSR1)
    Note over N: 割り込みスレッドが sigwait で受信
    N->>I: ip_input(data, len, dev)
    I->>T: tcp_input(iphdr, data, len, iface)
    T->>P: tcp_segment_arrives() → buf へコピー
    T->>A: sched_task_wakeup(&pcb->task)
    A->>A: tcp_cmd_receive() で buf から読み出し
```

---

## パケット送信フロー

```
[Application]
        │  tcp_cmd_send() / udp_cmd_sendto()
        ▼
[tcp_output() / udp_output()]      ←── tcp.c / udp.c
  擬似ヘッダチェックサム計算
  ip_output(protocol, data, len, src, dst) を呼び出し
        │
        ▼
[ip_output()]                       ←── ip.c
  ルーティングテーブル検索 ip_route_lookup(dst)
  IP ヘッダ構築・チェックサム計算
  arp_resolve() で宛先 MAC アドレス解決
  net_device_output() を呼び出し
        │
        ▼
[net_device_output()]               ←── net.c
  dev->ops->output(dev, type, data, len, hwaddr)
        │
        ▼
[ether_tap / loopback driver]       ←── platform/linux/driver/ether_tap.c
  Ethernet フレームを writev / write で送出
```

### シーケンス図（送信）

```mermaid
sequenceDiagram
    participant A as Application
    participant T as tcp_output
    participant IP as ip_output
    participant ARP as arp_resolve
    participant N as net_device_output
    participant D as ether_tap

    A->>T: tcp_cmd_send(desc, data, len)
    T->>IP: ip_output(TCP, data, len, src, dst)
    IP->>ARP: arp_resolve(iface, target, hwaddr)
    ARP-->>IP: hwaddr (MAC アドレス)
    IP->>N: net_device_output(dev, IP, frame, hwaddr)
    N->>D: dev->ops->output(...)
    D->>D: writev() → NIC / TAP
```

---

## TCP 接続確立フロー（3-way handshake）

```mermaid
sequenceDiagram
    participant CA as Client App
    participant CS as Client TCP
    participant SS as Server TCP
    participant SA as Server App

    CA->>CS: tcp_cmd_open(active=1)
    CS->>SS: SYN (SYN_SENT)
    SS->>SA: sched_task_wakeup (SYN_RECEIVED → backlog へ)
    SS->>CS: SYN+ACK
    CS->>SS: ACK (ESTABLISHED)
    CS-->>CA: desc (接続完了)
    SA->>SA: tcp_cmd_accept() → backlog から dequeue
    SA-->>SA: ESTABLISHED
```

---

## TCP 接続切断フロー（4-way handshake）

```mermaid
sequenceDiagram
    participant A as Initiator App
    participant AI as Initiator TCP
    participant PI as Peer TCP
    participant P as Peer App

    A->>AI: tcp_cmd_close(desc)
    AI->>PI: FIN (FIN_WAIT1)
    PI->>P: sched_task_wakeup (CLOSE_WAIT)
    PI->>AI: ACK (FIN_WAIT2)
    P->>PI: tcp_cmd_close()
    PI->>AI: FIN (LAST_ACK)
    AI->>PI: ACK (TIME_WAIT → 30秒後 CLOSED)
    PI-->>PI: CLOSED
```

---

## 割り込み・スレッドモデル

```
Main Thread (Application)          Interrupt Thread
      │                                   │
      │  net_run()                         │  intr_run() → pthread_create
      │  ──────────────────────────────>  │  sigwait(&sigmask, &sig)
      │                                   │
      │  tcp_cmd_send()                   │
      │  → ip_output()                    │
      │  → intr_raise(SIGUSR1) ──────>   │  sig == SIGUSR1
      │                                   │  → net_softirq_handler()
      │                                   │    → ip_input()
      │                                   │    → tcp_input()
      │                                   │    → sched_task_wakeup()
      │  <── (条件変数シグナル) ──────    │
      │  tcp_cmd_receive() 復帰           │
      │                                   │  sig == SIGALRM (100ms)
      │                                   │  → tcp_timer() 再送チェック
```

---

## ARP 解決フロー

```mermaid
flowchart TD
    A[ip_output_device] --> B{キャッシュに HW アドレスあり?}
    B -->|Yes| C[net_device_output]
    B -->|No| D[arp_resolve: ARP_RESOLVE_INCOMPLETE]
    D --> E[ARP Request ブロードキャスト]
    E --> F[相手から ARP Reply]
    F --> G[ARP キャッシュ更新]
    G --> H[送信待ちパケットを再送]
```
