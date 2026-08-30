# ChainOSCnano

このプロジェクトのソフトウェア、Webサイト、ドキュメントは、OpenAI Codexとの協働により制作されています。

This project's software, website, and documentation are created in collaboration with OpenAI Codex.

M5NanoC6とM5Stack Chainデバイスを組み合わせ、コンパクトなOSCコントローラーとして利用することを目指す、個人開発の非公式プロジェクトです。

> [!IMPORTANT]
> このプロジェクトはM5Stack社の公式製品・公式ファームウェアではありません。

## 現在のバージョン

### v1.0.0 — Device Preset Import Error Registry v1

Device Preset Import Error Registry v1へ完全対応し、JSON構文、必須項目、JSON型、OSC設定、Sequence、デバイス固有値・範囲、保存失敗のエラーコードと日英メッセージをChainOSCシリーズで統一しました。不正なプリセットは既存設定を変更せず拒否します。

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

v0.4.0では次のOSC機能を追加しています。

- OSC送信先のIPv4アドレスとUDPポートをブラウザーから設定できる
- OSC送信先を保存し、再起動後も復元できる
- Chain Keyを押した時に`Int 1`、離した時に`Int 0`を送信する
- UIDを含む固定OSC Addressを使用し、抜き差しや接続順変更の影響を受けない
- Wi-Fi切断中はOSC送信を抑止し、再接続後に自動的に送信を再開する

初期OSC送信先は`192.168.1.100:9000`です。各Chain Keyの初期OSC Addressは次の形式です。

```text
/chainoscnano/key/<24桁UID>
```

例：

```text
/chainoscnano/key/78000C001651343430383836
```

v0.5.0では次のKey設定機能を追加しています。

- 接続中のChain KeyをUID単位のカードとして表示
- デバイス名の設定
- 「押した時／離した時」と「シーケンス」のモード切り替え
- Press／Release合計8件までのOSCメッセージ
- OSC Address、Float／Int／String、値の設定
- メッセージの追加、削除、並べ替え
- 0件の場合は該当イベントでOSCを送信しない
- SequenceのAddress、開始値、終了値、増減量、型、周回動作
- SequenceモードのKeyを押した時はChain KeyのLEDを緑色で表示
- UID単位のLittleFS保存と再起動後の復元
- ChainデバイスはUID全体、本体ボタンは`NanoButton.json`を設定ファイル名として使用
- 一時ファイルの検証後に置換する安全な保存処理と、現行compact NVS設定からの自動移行
- 設定ファイルサイズおよびLittleFSの総容量・使用量・空き容量をシリアルログへ出力
- 保存済みデバイス設定は全種類合計で最大40台
- 接続中デバイスと保存済み未接続デバイスの分離表示
- 未接続デバイス設定の削除

v0.6.0では次の機能を追加しています。

- Chain Encoder、Chain Angle、Chain ToF、Chain Joystickへの対応
- 各デバイスの設定、OSC送信、UID単位の保存と復元
- ChainOSCnano全体設定のバージョン付きJSONエクスポート／インポート
- デバイス単位のJSONプリセットのエクスポート／インポート
- `ChainOSC-device-preset`形式によるM5ChainOSC／ChainOSCminiとのプリセット互換
- Key／Encoderクリック／Joystickクリックの複数メッセージとSequence設定
- デバイスカードの「…」メニューと10秒間のオレンジLED識別
- 不正なJSON、異なるデバイス種類、入力値、容量の検証

v0.7.0では次の機能を追加しています。

- M5NanoC6本体ボタンをWeb UIの内蔵Keyとして表示
- 本体ボタンのPress／Release、複数OSCメッセージ、Sequence設定
- 本体ボタン設定の保存・復元とJSONプリセット入出力
- 本体ボタン押下中とIdentify Device実行中のオレンジLED表示
- ChainOSCnanoポータル、Web Installer、faviconのアンバー系デザイン

v0.8.0では次の保存機能を追加・変更しています。

- Key、Encoder、Angle、ToF、Joystick設定をLittleFSへ保存
- ChainデバイスはUID全体、本体ボタンは`NanoButton.json`をファイル名として使用
- 一時ファイルの検証後に既存設定を置換し、書き込み失敗時も既存設定を維持
- 現行compact NVS設定からLittleFSへの自動移行
- 設定ファイルサイズとLittleFSの総容量・使用量・空き容量をシリアルログへ出力

v1.0.0では次の機能を変更しています。

- Device Preset Import Error Registry v1の全22エラーコードに対応

## ドキュメントとファームウェア

- [ChainOSCnanoポータル](https://shimez.github.io/ChainOSCnano/)
- [クイックスタート](https://shimez.github.io/ChainOSCnano/quick-start/)
- [日本語ユーザーガイド](https://shimez.github.io/ChainOSCnano/user-guide/)
- [Web Installer](https://shimez.github.io/ChainOSCnano/installer/)
- [GitHub Releases](https://github.com/shimez/ChainOSCnano/releases)

ブラウザーで`http://chainoscnano.local/`または本体のIPアドレスを開き、各Chain Keyを設定して「すべての設定を保存」を押します。

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
3. M5Chain、Adafruit NeoPixel、ArduinoOSC、ArduinoJsonをライブラリマネージャーから導入します。
4. ESP32-C6に対応するボードを選択します。
5. `Tools`→`Partition Scheme`で、**`Huge APP (3MB No OTA / 1MB SPIFFS)`**を選択します。
6. コンパイルしてM5NanoC6へ書き込みます。

`Huge APP (3MB No OTA / 1MB SPIFFS)`は必須です。デバイス設定の保存にLittleFSを使用するため、ファイルシステム領域のないPartition Schemeでは設定を保存・復元できません。Arduino IDE上では領域名が`SPIFFS`と表示されますが、ファームウェアからはLittleFSとして使用します。

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

ESP32-C6のArduinoフレームワークを利用するため、`platformio.ini`ではpioarduino版のEspressif 32プラットフォームを使用しています。Flash容量はM5NanoC6に合わせて4 MBとし、PlatformIOでは`partitions.csv`の3 MBアプリ領域を使用します。

## リリースの自動ビルド

`vX.Y.Z`タグをpushするとGitHub ActionsがPlatformIOビルド、mergedバイナリ、SHA-256チェックサム、Draft Releaseを作成します。Draft Releaseを公開すると、同じバイナリを組み込んだGitHub PagesとWeb Installerが自動配信されます。

## テスト

確認手順と実機結果は[`docs/TESTING.md`](docs/TESTING.md)を参照してください。

## プリセット互換

デバイス単位のプリセットにはChainOSCシリーズ共通の`ChainOSC-device-preset`形式を使用します。同じ種類のデバイスであれば、M5ChainOSC、ChainOSCmini、ChainOSCnanoの間でプリセットを共有できます。

## ライセンス

ChainOSCnano固有のソースコードはMIT Licenseで公開します。依存ライブラリには別のライセンスが適用される場合があります。詳細は[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)および[`licenses/`](licenses/)を参照してください。

Copyright (c) 2026 shimez and contributors
