---
layout: default
title: ChainOSCnano Quick Start
permalink: /en/quick-start/
---

# ChainOSCnano Quick Start

[日本語版](../../quick-start/)

## What you need

- M5NanoC6 and supported M5Stack Chain devices
- Wiring or an adapter for GND, 5V, GPIO2, and GPIO1
- A USB Type-C data cable
- A 2.4 GHz Wi-Fi network
- A computer running an OSC receiver
- Desktop Chrome or Edge

## 1. Install firmware

Open the [Web Installer](../../installer/), connect the M5NanoC6 by USB, select `Install ChainOSCnano`, and follow the instructions.

## 2. Configure Wi-Fi

1. Connect to `ChainOSCnano-Setup` with password `12345678`.
2. If the captive portal does not open, visit `http://192.168.4.1/`.
3. Save the credentials for a 2.4 GHz Wi-Fi network.

The built-in LED is red in AP Mode, blinks blue while connecting, and turns cyan when connected.

## 3. Open the Web UI

Visit `http://chainoscnano.local/`. If Windows cannot resolve it, run `Resolve-DnsName chainoscnano.local` in PowerShell and open the returned IPv4 address.

The Web UI has no authentication. Use ChainOSCnano only on a trusted local network.

## 4. Send OSC

Set the receiver IPv4 address and UDP port, configure a connected Chain device, select `Save All Settings`, and operate the device. See the [English User Guide](../user-guide/) for details.
