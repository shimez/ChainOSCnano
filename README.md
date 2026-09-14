# ChainOSCnano

このプロジェクトのソフトウェア、Webサイト、ドキュメントは、OpenAI Codexとの協働により制作されています。

This project's software, website, and documentation are created in collaboration with OpenAI Codex.

M5NanoC6とM5Stack Chainデバイスを組み合わせ、コンパクトなOSCコントローラーとして利用することを目指す、個人開発の非公式プロジェクトです。

> [!IMPORTANT]
> このプロジェクトはM5Stack社の公式製品・公式ファームウェアではありません。

## 現在のバージョン

### v1.2.6 — Encoder v2に対応

ChainOSCminiをベースにEncoder v2へ対応し、新規Encoderの初期設定をv2形式へ変更しました。

- Encoder v2の回転量／回転方向モード、保存、実行時処理に対応
- Legacy設定からv2候補を確認して移行できるWeb UIを追加
- Encoder Device Preset v1／v2のImport／Exportに対応
- NVS／LittleFSに設定がない新規Encoderはv2設定で開始
- Angle／Joystick／ToFのMinimum Change表記を整理
- Angleの分解能変更直後の不要なOSC送信を防止
- JoystickのMinimum Change判定をX／Y各軸で独立して管理

過去の変更内容は[`CHANGELOG.md`](CHANGELOG.md)を参照してください。

## ドキュメントとファームウェア

- [ChainOSCnanoポータル](https://shimez.github.io/ChainOSCnano/)
- [クイックスタート](https://shimez.github.io/ChainOSCnano/quick-start/)
- [日本語ユーザーガイド](https://shimez.github.io/ChainOSCnano/user-guide/)
- [Web Installer](https://shimez.github.io/ChainOSCnano/installer/)
- [GitHub Releases](https://github.com/shimez/ChainOSCnano/releases)

ブラウザーで`http://chainoscnano.local/`または本体のIPアドレスを開き、各Chain Keyを設定して「すべての設定を保存」を押します。

## Device Preset対応

デバイス単位のプリセットにはChainOSCシリーズ共通の`ChainOSC-device-preset`形式を使用します。

- Key v1：M5ChainOSC、ChainOSCmini、ChainOSCnano、ChainOSCPad、ChainOSC for Windowsと共有できます。
- Encoder v1／v2：M5ChainOSC、ChainOSCmini、ChainOSCnano、ChainOSCPadと共有できます。v1はLegacy設定として読み込み、v2として保存した設定はv2でエクスポートします。
- Angle v1、ToF v1、Joystick v1：M5ChainOSC、ChainOSCmini、ChainOSCnano間で共有できます。

共有可否はDevice TypeとschemaVersion、各製品のImporter／Exporter対応に基づきます。詳細な共通仕様は[ChainOSC共通リポジトリ](https://github.com/shimez/ChainOSC)を参照してください。

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

## ライセンス

ChainOSCnano固有のソースコードはMIT Licenseで公開します。依存ライブラリには別のライセンスが適用される場合があります。詳細は[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)および[`licenses/`](licenses/)を参照してください。

Copyright (c) 2026 shimez and contributors
