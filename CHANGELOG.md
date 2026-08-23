# Changelog

ChainOSCnanoの主な変更をこのファイルに記録します。

形式はKeep a Changelogを参考にし、バージョン番号はSemantic Versioningに従います。

## [Unreleased]

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

[Unreleased]: https://github.com/shimez/ChainOSCnano/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/shimez/ChainOSCnano/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/shimez/ChainOSCnano/releases/tag/v0.1.0
