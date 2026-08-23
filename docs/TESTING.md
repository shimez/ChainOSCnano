# ChainOSCnano テストガイド

この文書は、ChainOSCnano v0.1.0のハードウェア検証手順と確認結果を記録します。

## ビルドと書き込み

### Arduino IDE

- `ChainOSCnano.ino`を開く
- ESP32-C6対応ボードを選択する
- コンパイルが成功する
- M5NanoC6へ書き込める

### PlatformIO

```powershell
pio run -e m5nanoc6
pio run -e m5nanoc6 -t upload
```

PlatformIOでのビルドと書き込みは、使用するPC環境で別途確認してください。

## 起動診断

115200 bpsでシリアルモニターを開き、次の内容を確認します。

- `ChainOSCnano v0.1.0`が表示される
- チップがESP32-C6として表示される
- Flash容量が4 MBとして表示される
- PSRAMが0 bytesとして表示される
- Chain電源がGPIO19、active HIGHとして表示される
- Chain UARTがRX1／TX2、115200 bpsとして表示される
- `READY`が表示される

## Chainデバイスの検出

1. Chainデバイスを接続して起動します。
2. `state=CONNECTED`と接続台数が表示されることを確認します。
3. 各デバイスのID、種類、UIDが表示されることを確認します。
4. デバイスを追加または取り外し、`state=CHANGED`または`state=DISCONNECTED`が表示されることを確認します。
5. 再接続後にデバイス一覧が復元されることを確認します。

## v0.1.0で確認済みの結果

- GPIO19をHIGHにすることでChainデバイスへ給電できた
- GPIO1／GPIO2のUARTでChainデバイスを列挙できた
- Chain Keyを4台同時に認識できた
- すべて取り外した際に`DISCONNECTED devices=0`を検出できた
- 1台から4台まで順番に再接続した際、接続台数とUIDが更新された
- 長時間動作中も空きヒープは約425 KBで安定していた

## v0.1.0の対象外

- Chainデバイスの入力取得
- OSC送信
- Wi-Fi接続とAP Mode
- Web UI
- 設定保存、JSONバックアップ、デバイスプリセット

これらは今後のバージョンで段階的に検証します。
