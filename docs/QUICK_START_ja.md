---
layout: default
title: ChainOSCnano クイックスタート
permalink: /quick-start/
---

# ChainOSCnano クイックスタート

[English version](../en/quick-start/)

## 用意するもの

- M5NanoC6
- M5Stack Chainデバイス
- GND／5V／GPIO2／GPIO1を接続する配線または変換基板
- データ通信対応USB Type-Cケーブル
- 2.4 GHz帯Wi-Fi
- OSC受信アプリを実行するPC
- デスクトップ版ChromeまたはEdge

## 1. ファームウェアを書き込む

1. [Web Installer](../installer/)をChromeまたはEdgeで開きます。
2. M5NanoC6をUSB接続し、`Install ChainOSCnano`を押します。
3. M5NanoC6のシリアルポートを選び、画面の案内に従います。

## 2. Wi-Fiを設定する

1. SSID`ChainOSCnano-Setup`へ接続します。
2. パスワード`12345678`を入力します。
3. キャプティブポータルが開かない場合は`http://192.168.4.1/`を開きます。
4. 2.4 GHz帯Wi-FiのSSIDとパスワードを保存します。

本体RGB LEDは、赤がAP Mode、青点滅が接続中、水色が接続済みです。

## 3. 設定画面を開く

ブラウザーで`http://chainoscnano.local/`を開きます。Windowsで開けない場合はPowerShellで次を実行し、表示されたIPv4アドレスをブラウザーで開きます。

```powershell
Resolve-DnsName chainoscnano.local
```

設定画面には認証機能がありません。信頼できるローカルネットワークで使用してください。

## 4. OSCを送信する

1. 「OSC送信先」に受信PCのIPv4アドレスとUDPポートを設定します。
2. 接続中のChainデバイスでOSC Address、型、値を設定します。
3. 「すべての設定を保存」を押します。
4. デバイスを操作し、受信アプリでOSCメッセージを確認します。

詳しい項目は[日本語ユーザーガイド](../user-guide/)を参照してください。
