# ChainOSCnano Web Installer

ChainOSCnanoのファームウェアをM5NanoC6へブラウザーから書き込むためのWeb Installerです。

現在の公開版は`0.6.0`です。

## 公開URL

```text
https://shimez.github.io/ChainOSCnano/installer/
```

デスクトップ版ChromeまたはEdgeを使用します。

## 自動配信

GitHub ActionsがPlatformIOでmergedバイナリを生成してGitHub Releaseへ添付します。Releaseを公開すると、Pages Workflowが同じバイナリをPages成果物へ組み込みます。

```text
installer/firmware/ChainOSCnano-0.6.0-M5NanoC6-merged.bin
```

`manifest.json`はこのファイルをESP32-C6のoffset `0x0`へ書き込みます。

## ローカル確認

Releaseからmergedバイナリをダウンロードして`docs/installer/firmware/`へ配置し、`docs`で次を実行します。

```powershell
py -m http.server 8000 --bind 127.0.0.1
```

ChromeまたはEdgeで`http://localhost:8000/installer/`を開きます。
