# microps アーキテクチャ設計（逆生成）

## 分析日時
2026-06-28

## システム概要

### 実装されたアーキテクチャ
- **パターン**: レイヤードアーキテクチャ（OSI参照モデルに準拠）
- **言語**: C（POSIX / Linux プラットフォーム）
- **構成**: ユーザー空間 TCP/IP スタック。カーネルのネットワークスタックを迂回し、TAPデバイス経由でEthernetフレームを直接送受信する

### 技術スタック

#### コア実装
- **言語**: C99
- **スレッドモデル**: POSIX Threads（pthread）
- **同期プリミティブ**: `pthread_mutex_t`（`lock_t`）、`pthread_cond_t`（`sched_task`）
- **割り込みモデル**: POSIX シグナル（SIGUSR1=SOFT IRQ、SIGUSR2=USER IRQ、SIGALRM=TIMER）

#### プラットフォーム抽象
- **対象 OS**: Linux
- **デバイスドライバ**: TAPデバイス（`ether_tap`）、ループバック（`loopback`）
- **タイマー**: `SIGALRM` ベースのインターバルタイマー
- **割り込みスレッド**: 専用 pthread が `sigwait()` でシグナルを待機

---

## レイヤー構成

```
┌─────────────────────────────────────┐
│         ソケット API層              │  sock.c / sock.h
│   (POSIX-like: open/close/bind/...) │
├─────────────────────────────────────┤
│    トランスポート層                 │
│  ┌──────────────┬──────────────┐    │
│  │  TCP         │  UDP         │    │  tcp.c/h, udp.c/h
│  │  (tcp_pcb)   │  (udp_pcb)   │    │
│  └──────────────┴──────────────┘    │
├─────────────────────────────────────┤
│    ネットワーク層                   │
│  ┌──────────────┬──────────────┐    │
│  │  IP          │  ICMP        │    │  ip.c/h, icmp.c/h
│  └──────────────┴──────────────┘    │
│  ┌──────────────────────────────┐   │
│  │  ARP                         │   │  arp.c/h
│  └──────────────────────────────┘   │
├─────────────────────────────────────┤
│    リンク層                         │  ether.c/h
│  ┌──────────────────────────────┐   │
│  │  Ethernet                    │   │
│  └──────────────────────────────┘   │
├─────────────────────────────────────┤
│    ネットワークデバイス抽象層       │  net.c/h
│  ┌──────────────┬──────────────┐    │
│  │  loopback    │  ether_tap   │    │  driver/loopback.c
│  └──────────────┴──────────────┘    │  platform/linux/driver/ether_tap.c
├─────────────────────────────────────┤
│    プラットフォーム層               │  platform/linux/
│  ┌──────────┬─────────┬────────┐    │  intr.c, sched.c, timer.c
│  │  intr    │  sched  │ timer  │    │
│  └──────────┴─────────┴────────┘    │
└─────────────────────────────────────┘
```

### ディレクトリ構造
```
microps/
├── net.c / net.h          # デバイス抽象・プロトコル登録・softirq
├── ether.c / ether.h      # Ethernet フレーム処理
├── arp.c / arp.h          # ARP キャッシュ・解決
├── ip.c / ip.h            # IPv4 ルーティング・送受信
├── icmp.c / icmp.h        # ICMP メッセージ処理
├── tcp.c / tcp.h          # TCP PCB・状態機械・再送
├── udp.c / udp.h          # UDP PCB・送受信キュー
├── sock.c / sock.h        # BSD-like ソケット API ラッパー
├── util.c / util.h        # キュー・チェックサム・ログ
├── driver/
│   └── loopback.c/h       # ループバックデバイス
└── platform/linux/
    ├── platform.c/h       # 初期化・lock_t 定義
    ├── intr.c/h           # シグナルベース割り込みスレッド
    ├── sched.c/h          # 条件変数ベーススケジューラ
    ├── timer.c/h          # SIGALRM タイマー
    └── driver/
        └── ether_tap.c/h  # Linux TAP デバイスドライバ
```

---

## デザインパターン

### 発見されたパターン

| パターン | 採用箇所 | 実装方法 |
|---|---|---|
| **レイヤードアーキテクチャ** | 全体 | 各層が上位層のハンドラを関数ポインタで登録 |
| **Observer（コールバック登録）** | `net_protocol_register`, `ip_protocol_register` | 受信時にハンドラが呼び出される |
| **侵入型リスト (Intrusive List)** | `queue_entry` | 構造体先頭にリンクノードを埋め込む |
| **C言語構造体継承** | `ip_iface` extends `net_iface` | 先頭フィールド埋め込み + キャストマクロ |
| **PCB (Protocol Control Block)** | `tcp_pcb`, `udp_pcb` | 接続状態・バッファ・タスクを一元管理 |
| **Softirq（遅延処理）** | `net_softirq_handler` | 受信パケットをキューに積み、SIGUSR1で処理スレッドを起こす |
| **Two-phase Init** | `net_init()` → `net_run()` | 登録フェーズと起動フェーズを分離 |

---

## 非機能要件の実装状況

### スレッドセーフ
- プロトコルキュー・PCBアクセスはすべて `lock_t`（pthread_mutex_t）で保護
- 割り込みスレッドとアプリケーションスレッドは条件変数（`sched_task`）で同期

### ログ出力
- `errorf / warnf / infof / debugf` マクロによる統一ログ
- `HEXDUMP` マクロ定義時はパケットダンプ出力あり

### メモリ管理
- `memory_alloc / memory_free`（platform 抽象）経由で動的確保
- PCBは固定サイズ配列（`pcbs[TCP_PCB_SIZE=16]`、`pcbs[UDP_PCB_SIZE]`）

### エラーハンドリング
- 各関数は `int`（0=成功, -1=失敗）または `ssize_t` を返す
- TCP コマンドは `errno = EINTR` を設定して割り込みを通知
