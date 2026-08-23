# ChainOSCnano

M5NanoC6とM5Stack Chainデバイスを組み合わせ、コンパクトなOSCコントローラーとして利用することを目指す、個人開発の非公式プロジェクトです。

> [!IMPORTANT]
> このプロジェクトはM5Stack社の公式製品・公式ファームウェアではありません。

## 現在のバージョン

### v0.3.0 — Wi-Fi provisioning and captive portal

Chain Keyの入力監視とLED制御に加え、Wi-Fi接続、AP Mode、キャプティブポータル、mDNS、ブラウザーによるWi-Fi設定を実装したバージョンです。OSC送信とChainデバイス設定UIはまだ実装されていません。

v0.1.0の実機検証では次の項目を確認済みです。

- GPIO19をHIGHにしてChainデバイスへ給電できる
- GPIO1／GPIO2のUART（115200 bps）でChainデバイスを検出できる
- ChainデバイスのID、種類、UIDを取得できる
- 4台のChain Keyを同時に認識できる
- Chainデバイスの抜き差しと、1台から4台までの段階的な再接続を検出できる
- 診断中の空きヒープが約425 KBで安定している
- 内蔵RGB LEDを初期化できる

v0.2.0では、実機で次の動作を確認済みです。

- 4台のChain Keyについて、押した時／離した時をUID付きで取得できる
- 複数のChain Keyを同時に操作しても、ID・UID・入力状態が正しく対応する
- 認識した対応Chainデバイスを青、押しているChain Keyをオレンジで点灯できる
- 抜き差しや接続順変更後も、UID・入力・LEDの対応を維持できる
- 不安定な接続中の偽入力を破棄し、通信エラー時に直前の状態を維持する
- 切断後にUARTを自動復旧し、本体を再起動せず再接続できる
- 入力監視とUART再初期化を繰り返しても、空きヒープが約425 KBで安定する

v0.3.0では次のネットワーク機能を追加しています。

- Wi-Fi設定がない場合は`ChainOSCnano-Setup`というAPを起動する
- APのパスワードは`12345678`
- キャプティブポータルまたは`http://192.168.4.1/`からWi-Fiを設定する
- 保存済みWi-Fiへの起動時接続が失敗した場合はAP Modeへ移行する
- 接続後は`http://chainoscnano.local/`またはIPアドレスで状態を確認する
- 接続後にWi-Fiが切断された場合は、AP Modeへ戻らず自動再接続する
- ブラウザーからWi-Fi設定を削除してセットアップ状態へ戻せる

## 初回Wi-Fi設定

1. M5NanoC6を起動します。
2. PCまたはスマートフォンから`ChainOSCnano-Setup`へ接続します。
3. パスワード`12345678`を入力します。
4. キャプティブポータルが開かない場合は、ブラウザーで`http://192.168.4.1/`を開きます。
5. 2.4 GHz帯のWi-FiのSSIDとパスワードを保存します。
6. 再起動後、`http://chainoscnano.local/`を開きます。

M5NanoC6は2.4 GHz帯のWi-Fiを使用します。設定ページには認証機能がないため、信頼できるローカルネットワークで使用してください。

## 本体RGB LED

| 色 | 状態 |
|---|---|
| 青点滅 | Wi-Fi接続中／再接続中 |
| 水色 | Wi-Fi接続済み |
| 赤 | AP Mode |

## 対象ハードウェア

- M5NanoC6（ESP32-C6FH4、Flash 4 MB、PSRAMなし）
- M5Stack Chainデバイス
- GND／5V／GPIO2／GPIO1を接続する配線または変換基板

## ピン構成

| 用途 | GPIO | 備考 |
|---|---:|---|
| Chain RX | GPIO1 | Chain側TXへ接続 |
| Chain TX | GPIO2 | Chain側RXへ接続 |
| Chain電源制御 | GPIO19 | HIGHで有効 |
| 内蔵RGB LED | GPIO20 | 状態表示用 |

設定値は[`src/config.h`](src/config.h)にまとめています。

## シリアル診断

起動時に、チップ情報、Flash容量、ヒープ、PSRAM、Chain電源、UART設定を出力します。動作中は約5秒ごとに空きヒープを出力し、Chainデバイスの構成が変化した場合は一覧を再表示します。

```text
[ChainOSCnano][CHAIN] state=CONNECTED devices=1
[ChainOSCnano][CHAIN] index=0 id=1 type=3(Key) uid=...
[ChainOSCnano][RUN] uptime=5000 ms free_heap=...
```

## Arduino IDEでビルドする

1. Arduino IDEで`ChainOSCnano.ino`を開きます。
2. ESP32 Arduino Core 3.xを導入します。
3. M5ChainとAdafruit NeoPixelをライブラリマネージャーから導入します。
4. ESP32-C6に対応するボードを選択します。
5. コンパイルしてM5NanoC6へ書き込みます。

Arduino IDEは`ChainOSCnano.ino`、PlatformIOは`src/main.cpp`をエントリーポイントとして使用し、どちらも`src/app.cpp`の共通実装を呼び出します。

## PlatformIOでビルドする

Visual Studio CodeのPlatformIOでリポジトリのルートディレクトリを開き、次を実行します。

```powershell
pio run -e m5nanoc6
```

書き込みは次のコマンドで行えます。

```powershell
pio run -e m5nanoc6 -t upload
```

ESP32-C6のArduinoフレームワークを利用するため、`platformio.ini`ではpioarduino版のEspressif 32プラットフォームを使用しています。Flash容量はM5NanoC6に合わせて4 MBに設定しています。

## テスト

確認手順と実機結果は[`docs/TESTING.md`](docs/TESTING.md)を参照してください。

## 今後の予定

- OSC送信
- Chainデバイス設定用Web UI
- 設定保存とJSON／プリセット互換
- 対応Chainデバイスの拡大

実装時は、M5ChainOSCおよびChainOSCminiとの設定・プリセット互換性を重視します。

## ライセンス

ChainOSCnano固有のソースコードはMIT Licenseで公開します。依存ライブラリには別のライセンスが適用される場合があります。詳細は[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)および[`licenses/`](licenses/)を参照してください。

Copyright (c) 2026 shimez and contributors
