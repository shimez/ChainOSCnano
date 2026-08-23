---
layout: default
title: ChainOSCnano User Guide
permalink: /en/user-guide/
---

# ChainOSCnano User Guide

[日本語版](../../user-guide/)

This guide covers ChainOSCnano v0.7.0 setup and its Web UI.

> [!IMPORTANT]
> ChainOSCnano is an unofficial, independently developed project. The Web UI has no authentication, so use it only on a trusted local network.

## Wi-Fi and the Web UI

For initial setup, connect to `ChainOSCnano-Setup` with password `12345678`, open `http://192.168.4.1/`, and select a 2.4 GHz Wi-Fi network. After connection, open `http://chainoscnano.local/` or the device IPv4 address.

The built-in RGB LED is red in AP Mode, blinks blue while connecting or reconnecting, and turns cyan when connected.

## Common settings

- **Language** switches Japanese and English.
- **System** shows the product, version, IPv4 address, and mDNS URL.
- **Wi-Fi** can erase saved Wi-Fi credentials.
- **OSC Target** accepts the receiver IPv4 address and UDP port.
- **Save All Settings** stores common and connected-device settings together.

An OSC Address must start with `/`. Float and Int require valid numeric values; String accepts text.

## Key and click modes

Key, Encoder Click, and Joystick Click support two modes:

- **Press / Release**: configure up to eight messages in total across both events. Each message has an Address, type, and value. An empty event sends nothing.
- **Sequence**: each press advances from Start toward End by Step and wraps to Start after leaving the range.

## Encoder

Configure the rotation Address, Absolute or Increment mode, absolute input range, increment scale, output range, and output type. Encoder Click uses the Key modes above.

## Angle

Configure the Address, deadband, output range, and type. A larger deadband suppresses messages caused by small angle changes.

## ToF

Configure deadband in millimeters, the practical maximum distance, near-to-far direction, output range, and numeric type. Out-of-range measurements stop OSC transmission.

## Joystick

Configure X/Y Addresses, axis inversion, deadband, output range, and type. Joystick Click uses the Key modes above.

## Device menu and presets

The `...` menu exports or imports a UID-free `ChainOSC-device-preset` JSON file and can identify a device with an orange LED for ten seconds. Presets are compatible with the same device type in M5ChainOSC and ChainOSCmini.

## Backup and restore

Export or import all settings as versioned JSON. Wi-Fi credentials are excluded. Back up settings before reinstalling or erasing firmware.

## VRChat

In the VRChat radial menu, select **Options → OSC → Enabled**. Keep the PC and ChainOSCnano on the same trusted local network.
