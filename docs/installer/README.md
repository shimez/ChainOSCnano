# ChainOSCnano Web Installer

ChainOSCnanoのファームウェアをM5NanoC6へブラウザーから書き込むためのWeb Installerです。

現在の公開版は`1.2.6`です。

- Version 1.2.6: Encoder v2とLegacyからの移行、Encoder Device Preset v1／v2、新規Encoderのv2初期設定に対応
- Version 1.2.4: 接続中のChainデバイスカードに現在の接続順を示す位置ラベルを追加
- Version 1.2.3: Web UIの入力検証を強化し、通常画面とAP Modeへfaviconを追加
- Version 1.2.1: AP Modeのキャプティブポータルから全設定を削除する機能を追加
- Version 1.2.2: Encoder／Joystick／Angle／ToF／Push Sequenceの入力検証と警告表示を強化
- Version 1.2.0: Web UIからLittleFSとNVSの全設定を削除して再起動する機能を追加

## 公開URL

```text
https://shimez.github.io/ChainOSCnano/installer/
```

デスクトップ版ChromeまたはEdgeを使用します。

## 自動配信

GitHub ActionsがPlatformIOでmergedバイナリを生成してGitHub Releaseへ添付します。Releaseを公開すると、Pages Workflowが同じバイナリをPages成果物へ組み込みます。

```text
installer/firmware/ChainOSCnano-1.2.6-M5NanoC6-merged.bin
```

`manifest.json`はこのファイルをESP32-C6のoffset `0x0`へ書き込みます。

## ローカル確認

Releaseからmergedバイナリをダウンロードして`docs/installer/firmware/`へ配置し、`docs`で次を実行します。

```powershell
py -m http.server 8000 --bind 127.0.0.1
```

ChromeまたはEdgeで`http://localhost:8000/installer/`を開きます。
