#!/usr/bin/env python3
"""Generate ChainOSCnano whole-settings files for storage limit testing."""

import json
from pathlib import Path


OUTPUT_DIR = Path(__file__).resolve().parents[1] / "test-data" / "storage-limit"


def identity(index: int) -> str:
    return f"chain:F0{index:022X}"


def message(index: int, pressed: bool) -> dict:
    return {
        "address": f"/chainoscnano/stress/{index}/button",
        "value": "1" if pressed else "0",
        "type": 1,
    }


def sequence(index: int) -> dict:
    return {
        "address": f"/chainoscnano/stress/{index}/sequence",
        "type": 0,
        "start": 0.0,
        "end": 10.0,
        "step": 1.0,
    }


def device(index: int) -> dict:
    device_type = ((index - 1) % 5) + 1
    common = {
        "identity": identity(index),
        "deviceType": device_type,
        "displayName": f"Storage Test {index:02d}",
        "builtIn": False,
    }

    if device_type == 1:
        common["deviceTypeName"] = "Encoder"
        common["encoder"] = {
            "rotationAddress": f"/chainoscnano/stress/{index}/encoder",
            "sendIncrement": True,
            "absoluteInputMin": 0.0,
            "absoluteInputMax": 100.0,
            "incrementScale": 1.0,
            "range": {"outMin": 0.0, "outMax": 1.0, "type": 0},
            "clickMode": 0,
            "press": [message(index, True)],
            "release": [message(index, False)],
            "sequence": sequence(index),
        }
    elif device_type == 2:
        common["deviceTypeName"] = "Angle"
        common["angle"] = {
            "address": f"/chainoscnano/stress/{index}/angle",
            "use12bit": True,
            "deadband": 2,
            "range": {"outMin": 0.0, "outMax": 1.0, "type": 0},
        }
    elif device_type == 3:
        common["deviceTypeName"] = "Key"
        common["key"] = {
            "mode": 0,
            "press": [message(index, True)],
            "release": [message(index, False)],
            "sequence": sequence(index),
        }
    elif device_type == 4:
        common["deviceTypeName"] = "Joystick"
        common["joystick"] = {
            "xAddress": f"/chainoscnano/stress/{index}/x",
            "yAddress": f"/chainoscnano/stress/{index}/y",
            "deadband": 8,
            "invertX": False,
            "invertY": False,
            "range": {"outMin": -1.0, "outMax": 1.0, "type": 0},
            "clickMode": 0,
            "press": [message(index, True)],
            "release": [message(index, False)],
            "sequence": sequence(index),
        }
    else:
        common["deviceTypeName"] = "ToF"
        common["tof"] = {
            "address": f"/chainoscnano/stress/{index}/tof",
            "deadband": 10,
            "maxDistanceMm": 1000,
            "nearValueHigh": True,
            "range": {"outMin": 0.0, "outMax": 1.0, "type": 0},
        }
    return common


def settings(count: int) -> dict:
    return {
        "format": "ChainOSCnano-settings",
        "schemaVersion": 1,
        "firmwareVersion": "0.6.0",
        "wifiCredentialsIncluded": False,
        "global": {
            "oscHost": "192.168.0.12",
            "oscPort": 9000,
            "uiLanguage": "ja",
        },
        "devices": [device(index) for index in range(1, count + 1)],
    }


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    for count in (10, 20, 30, 40, 41):
        path = OUTPUT_DIR / f"ChainOSCnano-storage-test-{count:02d}.json"
        path.write_text(
            json.dumps(settings(count), ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        print(f"{path.name}: {path.stat().st_size} bytes")


if __name__ == "__main__":
    main()
