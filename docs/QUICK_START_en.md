---
layout: default
title: ChainOSCnano Quick Start
permalink: /en/quick-start/
---

# ChainOSCnano Quick Start

[日本語版](../../quick-start/)

## What you need

- M5NanoC6
- A USB Type-C data cable
- A 2.4 GHz Wi-Fi network
- A computer running VRChat
- Desktop Chrome or Edge

## 1. Install firmware

Open the [Web Installer](../../installer/), connect the M5NanoC6 by USB, select `Install ChainOSCnano`, and follow the instructions.

## 2. Configure Wi-Fi

1. Connect to `ChainOSCnano-Setup` with password `12345678`.
2. If the captive portal does not open, visit `http://192.168.4.1/`.
3. Save the credentials for a 2.4 GHz Wi-Fi network.

The built-in LED is red in AP Mode, blinks blue while connecting, and turns cyan when connected.

## 3. Enable OSC in VRChat

Start VRChat and select **Action Menu → Options → OSC → Enabled**.

## 4. Find the IPv4 address of the VRChat PC

In Windows PowerShell or Command Prompt, run `ipconfig`. Find the `IPv4 Address` of the Wi-Fi or Ethernet adapter connected to the same network as ChainOSCnano. Do not use a VPN or virtual adapter address.

## 5. Open the Web UI

Visit `http://chainoscnano.local/`. If Windows cannot resolve it, run `Resolve-DnsName chainoscnano.local` in PowerShell and open the returned IPv4 address.

The Web UI has no authentication. Use ChainOSCnano only on a trusted local network.

## 6. Configure the OSC destination

1. In **OSC Destination**, enter the IPv4 address of the PC running VRChat in **Hostname or IPv4 address**.
2. Enter `9000` in **UDP Port**.

## 7. Configure a Voice action on the M5NanoC6 built-in Key

In the **M5NanoC6** card, select **Press** and enter the following values. The default device name is **M5NanoC6 Button**.

- OSC Address: `/input/Voice`
- Type: `Int`
- Value: `1`

Switch to **Release** and enter the following values:

- OSC Address: `/input/Voice`
- Type: `Int`
- Value: `0`

## 8. Save and verify the action

1. Select **Save All Settings**.
2. With VRChat running and OSC enabled, operate the M5NanoC6 built-in Key.
3. Confirm that VRChat's Voice input state changes. This confirms that ChainOSCnano sent an OSC message.

ChainOSCnano is not limited to VRChat. For another OSC-compatible application, set the destination, OSC Address, type, and value for that application. Device Preset can be used to reuse and share frequently used settings; see the [English User Guide](../user-guide/) for details.
