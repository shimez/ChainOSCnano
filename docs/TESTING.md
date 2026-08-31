# ChainOSCnano テストガイド

この文書は、ChainOSCnanoのハードウェア検証手順と確認結果を記録します。

## ビルドと書き込み

### Arduino IDE

- `ChainOSCnano.ino`を開く
- ESP32-C6対応ボードを選択する
- `Tools`→`Partition Scheme`で`Huge APP (3MB No OTA / 1MB SPIFFS)`を選択する
- コンパイルが成功する
- M5NanoC6へ書き込める

このPartition Schemeは必須です。1 MBの`SPIFFS`領域をファームウェアからLittleFSとして使用し、デバイス設定を保存します。

### PlatformIO

```powershell
pio run -e m5nanoc6
pio run -e m5nanoc6 -t upload
```

PlatformIOでのビルドと書き込みは、使用するPC環境で別途確認してください。

## 起動診断

115200 bpsでシリアルモニターを開き、次の内容を確認します。

- `ChainOSCnano v1.1.0`が表示される
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

## v0.5.0のテスト手順

### Press／Releaseと保存

1. 複数のChain KeyがUIDごとのカードとして表示されることを確認します。
2. デバイス名、OSC Address、型、値を変更して「すべての設定を保存」を押します。
3. Keyを操作し、設定順に正しいAddress、型、値が届くことを確認します。
4. Press／Releaseの合計が8件までは追加でき、9件目を追加できないことを確認します。
5. メッセージを並べ替え、送信順も変化することを確認します。
6. メッセージを削除して0件にし、そのイベントではOSCが送信されないことを確認します。
7. 再起動後にデバイス名、モード、全メッセージが復元されることを確認します。

### Sequence

1. Key Modeを「シーケンス」へ変更します。
2. Address、Start、End、Step、Typeを設定して保存します。
3. Keyを押すたびに値がStep分変化し、Endを超えた後はStartへ戻ることを確認します。
4. 押している間、同じChain KeyのLEDが緑色になることを確認します。
5. Keyを離した時は送信も値の更新も行われないことを確認します。
6. Wi-Fi切断中にKeyを押しても値が進まず、復旧後は切断前の次の値から再開することを確認します。

### システム設定のLittleFS保存とNVS移行

1. 旧ファームウェアでWi-Fi、OSC送信先、Web UI言語をNVSへ保存します。
2. ファイルシステムを消去せずに本バージョンへ更新します。
3. 起動ログに`[ChainOSCnano][SYSTEM] migration source=nvs target=littlefs result=ok`が出ることを確認します。
4. `/system/settings.json`の保存ログでファイルサイズ、LittleFS総容量、使用量、空き容量を確認します。
5. 再起動後もWi-Fi接続、OSC送信先、Web UI言語が復元されることを確認します。
6. Wi-Fi設定を削除し、OSC送信先とWeb UI言語が維持されることを確認します。
7. Web Installerまたはmerged firmwareでファイルシステムを消去せず更新し、同じ3設定が維持されることを確認します。
7. 再起動後にSequence設定が復元され、値はStartから始まることを確認します。

### UIDと未接続デバイス

1. 設定保存後にChain Keyを抜き差しし、同じUIDの設定が復元されることを確認します。
2. 複数Keyの接続順を変えても、UIDと設定の対応が維持されることを確認します。
3. 取り外したKeyが「保存済みデバイス設定」に未接続として表示されることを確認します。
4. 未接続デバイス設定を削除し、再接続後に初期値へ戻ることを確認します。
5. 複数のChain Keyを接続した状態で一括保存できることを確認します。

### 入力検証

- `/`で始まらないAddressを保存できない
- 上限を超えるAddress、Value、デバイス名を保存できない
- Float／Intへ不正な値を指定すると保存できない
- Sequenceの数値やAddressが不正な場合に保存できない
- 保存エラー時に不正な設定がNVSへ書き込まれない

## v0.6.0のテスト手順

### 対応Chainデバイス

1. Key、Encoder、Angle、ToF、Joystickを接続し、物理的な接続順でカードが表示されることを確認します。
2. 各カードで設定を変更して保存し、操作に対応するOSC Address、型、値が届くことを確認します。
3. EncoderとJoystickのクリックで、Press／ReleaseとSequenceが動作することを確認します。
4. デバイスを抜き差ししてもUID単位の設定が復元されることを確認します。
5. 「…」から識別を実行し、選択したデバイスだけが10秒間オレンジ色になることを確認します。

### 全体設定JSON

1. 全体設定をエクスポートします。
2. 複数デバイスの設定を変更して保存します。
3. エクスポートしたJSONをインポートし、全デバイスとOSC送信先が復元されることを確認します。
4. 再起動後もインポートした設定が維持されることを確認します。
5. 破損JSON、異なる全体設定形式、32 KiBを超えるファイルが適切に拒否されることを確認します。

### デバイスプリセット互換

1. 各デバイスカードの「…」からプリセットをエクスポートします。
2. JSONの`format`が`ChainOSC-device-preset`で、UIDを含まないことを確認します。
3. 同じ種類の別デバイスへインポートし、設定が画面へ直ちに反映されることを確認します。
4. M5ChainOSC／ChainOSCminiで出力した同種プリセットをChainOSCnanoへインポートします。
5. ChainOSCnanoで出力したプリセットをM5ChainOSC／ChainOSCminiへインポートします。
6. 異なるデバイス種類、全体設定JSON、破損JSONが設定を変更せず拒否されることを確認します。

## v0.7.0のテスト手順

### M5NanoC6本体ボタン

1. Web UIの接続中デバイス先頭に`M5NanoC6`のKeyカードが表示されることを確認します。
2. 本体ボタンを押した時／離した時に、設定したOSCメッセージが順番どおり送信されることを確認します。
3. Press／Release合計8件まで追加でき、9件目を追加できないことを確認します。
4. 0件にしたイベントではOSCメッセージが送信されないことを確認します。
5. Sequenceの開始値、終了値、増減量、型、周回動作を確認します。
6. 設定保存後に再起動し、デバイス名、モード、OSCメッセージ、Sequence設定が復元されることを確認します。
7. 本体ボタンを押している間、本体LEDがオレンジ色になることを確認します。
8. 「Identify Device」を実行すると、本体LEDが10秒間オレンジ色になることを確認します。
9. 本体ボタンのプリセットをエクスポート／インポートできることを確認します。
10. 全体設定JSONのエクスポート／インポート後も、本体ボタン設定が復元されることを確認します。

## v0.8.0で確認済みの結果

- Key、Encoder、Angle、ToF、Joystickおよび本体ボタンの設定をLittleFSへ保存できる
- 再起動後に各設定が復元され、設定内容を維持したままOSCを送信できる
- 大容量のKey設定を保存でき、書き込み後のJSONとヘッダー検証に成功する
- 設定保存時と起動時に、設定ファイルサイズおよびLittleFSの総容量・使用量・空き容量をシリアルログで確認できる
- 一時ファイルの検証後に既存ファイルが置換され、書き込み失敗時に既存設定が保護される

## v1.1.0で確認済みの結果

- Device Preset Import Error Registry v1のinvalidテストJSONをすべて実機で確認
- 各ファイルが想定したError Codeと日本語メッセージで拒否される
- JSON構文、必須項目、JSON型、OSC設定、Sequence、デバイス固有値・範囲の不正を検出できる
- インポート失敗後も画面上および保存済みのデバイス設定が変更されない
- 再起動後もインポート前の設定が維持される
- 正常なプリセットのインポート、保存およびOSC送信に問題がない
