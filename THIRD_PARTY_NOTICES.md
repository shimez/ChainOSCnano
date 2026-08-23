# Third-Party Notices

ChainOSCnanoはMIT Licenseで公開していますが、ビルド時に利用する第三者製ソフトウェアには、それぞれのライセンスが適用されます。

## ファームウェアに組み込まれる主な依存関係

### Arduino-ESP32

- Project: Espressif Arduino Core for ESP32
- License: GNU Lesser General Public License v2.1
- Source: https://github.com/espressif/arduino-esp32

ライセンス本文は[`licenses/LGPL-2.1.txt`](licenses/LGPL-2.1.txt)を参照してください。

### M5Chain

- Project: M5Stack M5Chain
- License: MIT License
- Source: https://github.com/m5stack/M5Chain

### Adafruit NeoPixel

- Project: Adafruit NeoPixel
- License: GNU Lesser General Public License v3
- Source: https://github.com/adafruit/Adafruit_NeoPixel

ライセンス本文は[`licenses/LGPL-3.0.txt`](licenses/LGPL-3.0.txt)を参照してください。

### ArduinoOSC

- Project: ArduinoOSC
- License: MIT License
- Source: https://github.com/hideakitai/ArduinoOSC

ArduinoOSCが利用するArxContainer、ArxSmartPtr、ArxTypeTraits、DebugLogもMIT Licenseです。各依存関係とバージョンは`platformio.ini`およびPlatformIOの依存関係解決結果を参照してください。

### ArduinoJson

- Project: ArduinoJson
- License: MIT License
- Source: https://github.com/bblanchon/ArduinoJson

全体設定およびデバイスプリセットのJSON解析に使用しています。

## ビルドツール

PlatformIO、pioarduino platform-espressif32、esptoolおよびコンパイラー等はビルドや書き込みに使用します。これらのツール自体は通常、配布するファームウェアへそのまま組み込まれるものではありません。

## 再現可能なビルド

依存関係の指定は`platformio.ini`に記載しています。

```powershell
pio run -e m5nanoc6
```

各依存関係の最新かつ正確なライセンス条件は、それぞれの配布元も確認してください。
