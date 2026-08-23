# Changelog

ChainOSCnanoの主な変更をこのファイルに記録します。

形式はKeep a Changelogを参考にし、バージョン番号はSemantic Versioningに従います。

## [Unreleased]

## [0.5.0] - 2026-08-24

### Added

- UID単位のChain Key設定モデルとデバイス別NVS保存
- デバイス名とKey ModeのWeb設定
- Press／Release合計8件までのOSCメッセージ設定
- Float／Int／Stringの型別OSC送信
- メッセージの追加、削除、並べ替え
- OSCメッセージ0件時の送信抑止
- Start／End／Step／Typeと周回を備えたSequenceモード
- Sequenceモードの押下を示すChain Keyの緑色LED表示
- 接続中デバイスと保存済み未接続デバイスの分離表示
- 保存済み未接続デバイス設定の削除
- 一括保存前のAddress、値、件数、Sequenceの検証

## [0.4.0] - 2026-08-24

### Added

- OSC送信先IPv4アドレスとUDPポートのWeb設定
- OSC送信先のPreferences保存と再起動後の復元
- Chain Keyを押した時の`Int 1`、離した時の`Int 0`送信
- Chain Key UIDを含む接続順に依存しない固定OSC Address
- Wi-Fi切断中のOSC送信抑止と再接続後の自動再開
- OSC送信・抑止内容のシリアル診断ログ
- ArduinoOSCのPlatformIO依存関係と第三者ライセンス表記

## [0.3.0] - 2026-08-24

### Added

- Preferencesへ保存したWi-Fi認証情報の読み込みと保存
- 保存済みWi-Fiへの接続と、起動時接続タイムアウト処理
- `ChainOSCnano-Setup`によるパスワード付きAP Mode
- DNSリダイレクトとOS別検出ルートを備えたキャプティブポータル
- 日本語／英語に対応したWi-Fi設定ページ
- SSID、パスワード、64桁PSKの入力検証
- `chainoscnano.local`によるmDNSアクセス
- Wi-Fi切断後の自動再接続
- Wi-Fi設定の削除とセットアップモードへの復帰
- 本体RGB LEDによる接続中・接続済み・AP Modeの状態表示
- Wi-Fi接続中・再接続中を識別する本体RGB LEDの青色点滅

## [0.2.0] - 2026-08-24

### Added

- 複数のChain Keyの押した時／離した時の監視
- UIDを保持したChain Key入力状態の管理
- 認識したChainデバイスの青色LED表示
- 押しているChain Keyのオレンジ色LED表示
- 入力初期化時の状態とLED色の診断ログ
- M5Chainのアクティブレポート排出による動的メモリ解放
- 一時的な通信エラーで入力状態を変更しないエラー処理
- 入力確定前のUID再照合と、不安定な接続中に発生する偽イベントの破棄
- LED更新結果と診断ログの整合
- 切断時に全Keyのアクティブレポートを破棄する処理
- 連続スキャン失敗時にChain UARTを再初期化する自動復旧処理

### Fixed

- Chainデバイスの構成変更中に旧IDから偽の入力イベントが発生する問題
- Chainデバイスを抜き差しした際、まれにUARTが再接続できない問題

## [0.1.0] - 2026-08-24

### Added

- M5NanoC6向けの初期ハードウェア検証ファームウェア
- Arduino IDE／PlatformIO両対応のプロジェクト構成
- 起動情報、リセット理由、メモリ、定期的な空きヒープの診断ログ
- GPIO19によるChainデバイスの電源制御
- GPIO1／GPIO2を使用したChain UART通信とデバイス列挙
- Chainデバイスの接続、切断、構成変更の検出
- ChainデバイスのID、種類、UIDのログ出力
- M5NanoC6内蔵RGB LEDの初期化
- v0.1.0向けREADME、テスト手順、第三者ライセンス表記

[Unreleased]: https://github.com/shimez/ChainOSCnano/compare/v0.5.0...HEAD
[0.5.0]: https://github.com/shimez/ChainOSCnano/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/shimez/ChainOSCnano/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/shimez/ChainOSCnano/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/shimez/ChainOSCnano/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/shimez/ChainOSCnano/releases/tag/v0.1.0
