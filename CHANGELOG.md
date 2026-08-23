# Changelog

ChainOSCnanoの主な変更をこのファイルに記録します。

形式はKeep a Changelogを参考にし、バージョン番号はSemantic Versioningに従います。

## [Unreleased]

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

[Unreleased]: https://github.com/shimez/ChainOSCnano/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/shimez/ChainOSCnano/releases/tag/v0.1.0
