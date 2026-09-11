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
- VRChatを実行するPC
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

## 3. VRChatでOSCを有効にする

VRChatを起動し、リングメニュー → オプション → OSC → 有効に設定します。

## 4. VRChatを実行しているPCのIPv4アドレスを確認する

WindowsでPowerShellまたはコマンドプロンプトを開き、`ipconfig`を実行します。ChainOSCnanoと同じネットワークに接続しているWi-FiまたはEthernetアダプターの`IPv4 Address`を確認してください。VPNや仮想アダプターではなく、実際に接続中のアダプターを選びます。

## 5. 設定画面を開く

ブラウザーで`http://chainoscnano.local/`を開きます。Windowsで開けない場合はPowerShellで次を実行し、表示されたIPv4アドレスをブラウザーで開きます。

```powershell
Resolve-DnsName chainoscnano.local
```

設定画面には認証機能がありません。信頼できるローカルネットワークで使用してください。

## 6. OSC送信先を設定する

1. 「OSC送信先」の「ホスト名またはIPv4アドレス」に、VRChatを実行しているPCのIPv4アドレスを入力します。
2. 「UDPポート」に`9000`を入力します。

## 7. KeyにVoice操作を設定する

接続中のChain Keyの設定で、まず「押した時」に次の値を手入力します。

- OSCアドレス：`/input/Voice`
- 型：`Int`
- 値：`1`

「離した時」に切り替えて、次の値を手入力します。

- OSCアドレス：`/input/Voice`
- 型：`Int`
- 値：`0`

## 8. 保存して動作を確認する

1. 「すべての設定を保存」を押します。
2. VRChatが起動していてOSCが有効な状態で、設定したChain Keyを操作します。
3. VRChatのVoice入力状態が切り替わることを確認します。Voiceが切り替われば、ChainOSCnanoからOSCメッセージを送信できています。

VRChat以外のOSC対応アプリケーションでも、送信先、OSC Address、型、値をそのアプリケーションに合わせて設定すれば利用できます。よく使う設定の再利用・共有にはDevice Presetを利用できます。詳しくは[日本語ユーザーガイド](../user-guide/)を参照してください。
