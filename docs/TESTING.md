# ChainOSCnano テストガイド

この文書は、ChainOSCnanoのハードウェア検証手順と確認結果を記録します。

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

- `ChainOSCnano v0.4.0`が表示される
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

## v0.2.0のテスト手順

1. 複数のChain Keyを接続して起動します。
2. 各Keyについて`ready`、UID、初期状態が表示されることを確認します。
3. 離しているKeyのLEDが青色であることを確認します。
4. Keyを押すと`PRESSED`が表示され、同じKeyのLEDがオレンジ色になることを確認します。
5. Keyを離すと`RELEASED`が表示され、LEDが青色へ戻ることを確認します。
6. 複数Keyを交互または同時に操作し、IDとUIDが正しく対応することを確認します。
7. Keyの抜き差しや接続順変更後も、操作したKeyとログ・LEDが一致することを確認します。
8. 通信タイムアウトが発生しても、偽の`PRESSED`／`RELEASED`が出力されないことを確認します。
9. 長時間操作しても空きヒープが継続的に減少しないことを確認します。
10. 取り外したデバイスを再接続し、M5NanoC6を再起動しなくても再認識されることを確認します。

抜き差し中にUID照合やLED更新が失敗した場合は、次のようにイベントが破棄されます。この直後にデバイス構成が再スキャンされ、偽の`PRESSED`／`RELEASED`は出力されないことを確認してください。

```text
[ChainOSCnano][CHAIN_KEY] identity_check_failed id=... event_discarded=true
[ChainOSCnano][CHAIN_KEY] event_discarded id=... reason=led_update_failed
```

切断状態またはスキャン失敗が続いた場合は、UARTを自動的に再初期化します。再接続したデバイスが、その後のスキャンで認識されることを確認してください。

```text
[ChainOSCnano][CHAIN] bus_reinitialized reason=disconnected RX=1 TX=2
```

想定されるログは次の形式です。

```text
[ChainOSCnano][CHAIN_KEY] ready id=1 uid=... initial=RELEASED led=BLUE
[ChainOSCnano][CHAIN_KEY] id=1 uid=... state=PRESSED led=ORANGE
[ChainOSCnano][CHAIN_KEY] id=1 uid=... state=RELEASED led=BLUE
```

## v0.2.0で確認済みの結果

- Arduino IDEでコンパイルし、M5NanoC6へ書き込めた
- 4台のChain Keyを同時に監視できた
- 各Keyの`PRESSED`／`RELEASED`とUIDが正しく対応した
- 複数Keyの同時押しと、押した順番とは異なる順番での解放を正しく検出した
- 離しているKeyは青色、押しているKeyはオレンジ色で点灯した
- 全取り外し、段階的な再接続、接続順変更後も正しいUIDで再認識した
- 不安定な接続中に検出したUID不一致イベントを破棄できた
- 切断状態が続いた場合にUARTが自動再初期化され、本体を再起動せず復旧した
- 動作中の空きヒープは約425 KBで安定し、継続的な減少は見られなかった

## v0.3.0のテスト手順

### 初回設定とキャプティブポータル

1. Wi-Fi設定が保存されていない状態で起動します。
2. 本体RGB LEDが赤色になり、`ChainOSCnano-Setup`が表示されることを確認します。
3. パスワード`12345678`で接続します。
4. キャプティブポータルが自動表示されることを確認します。
5. 自動表示されない場合、`http://192.168.4.1/`で同じページが開くことを確認します。
6. 空欄SSIDや不正な長さのパスワードが拒否されることを確認します。
7. 2.4 GHz帯のSSIDとパスワードを保存し、自動再起動することを確認します。

### 通常接続

1. 接続処理中は本体RGB LEDが青色で点滅し、接続後は水色になることを確認します。
2. シリアルログにIPアドレス、`chainoscnano.local`、RSSIが表示されることを確認します。
3. `http://chainoscnano.local/`またはIPアドレスで状態ページを開きます。
4. Wi-Fi接続中もChain Keyの入力と各ChainデバイスのLEDが正常に動作することを確認します。

### 切断と復旧

1. 保存済みアクセスポイントを停止した状態で起動し、15秒後にAP Modeへ移行することを確認します。
2. 通常接続後にアクセスポイントを停止し、`state=RECONNECTING`になることを確認します。
3. 再接続中は本体RGB LEDが青色で点滅することを確認します。
4. この場合はAP Modeへ移行しないことを確認します。
5. アクセスポイントを復旧し、本体を再起動せず`CONNECTED`へ戻り、LEDが水色点灯になることを確認します。

### 設定削除

1. 通常接続中の状態ページで「Wi-Fi設定を削除」を実行します。
2. 自動再起動後、`ChainOSCnano-Setup`が再び表示されることを確認します。

## v0.4.0のテスト手順

### OSC送信先の設定と復元

1. Wi-Fi接続後、`http://chainoscnano.local/`またはIPアドレスで状態ページを開きます。
2. OSC送信先へ受信PCのIPv4アドレスとUDPポートを入力して保存します。
3. 正しくないIPv4アドレス、0、65536などの不正なポートが拒否されることを確認します。
4. M5NanoC6を再起動し、保存したIPv4アドレスとUDPポートがページへ復元されることを確認します。

### Chain KeyからのOSC送信

1. OSC受信側を設定したUDPポートで待ち受けます。
2. Chain Keyを押し、`/chainoscnano/key/<24桁UID>`へ`Int 1`が届くことを確認します。
3. 同じKeyを離し、同じAddressへ`Int 0`が届くことを確認します。
4. 複数のChain Keyで、それぞれ異なるUIDを含むAddressが使用されることを確認します。
5. Chain Keyを抜き差ししたり接続順を変更したりしても、同じUIDのKeyは同じAddressを使用することを確認します。
6. シリアルログに送信先、Address、型、値が表示されることを確認します。

```text
[ChainOSCnano][OSC] sent target=192.168.1.100:9000 address=/chainoscnano/key/... type=Int value=1
[ChainOSCnano][OSC] sent target=192.168.1.100:9000 address=/chainoscnano/key/... type=Int value=0
```

### Wi-Fi切断中の動作

1. Wi-Fi接続中にアクセスポイントを停止します。
2. Chain Keyを操作してもOSC受信側へ届かないことを確認します。
3. シリアルログに`skipped reason=wifi_disconnected`が表示されることを確認します。
4. アクセスポイントを復旧し、本体を再起動せずOSC送信が再開することを確認します。

## 現時点で未実装

- Chainデバイス設定用Web UI
- 設定保存、JSONバックアップ、デバイスプリセット

これらは今後のバージョンで段階的に検証します。
