# NEXUS-BOOT

```
  ▄▄▄▄▄▄▄      NEXUS-BOOT v1.0
 █ ▄▄▄▄▄ █     Over-The-Cable Injector
 █ █   █ █     
 █ █▄▄▄█ █     Custom Bootloader for M5StickS3
 █▄▄▄▄▄▄▄█     
```

A custom BootLoader and app launcher for the **M5StickS3** (ESP32-S3). Store multiple firmware images on the device, pick one from a menu, launch it — and hold a button to return to the Bootloader at any time. Works with any `.bin` file, no modification required.

---

## Features

- **Boot menu** — browse and launch stored `.bin` firmware images from a clean UI
- **Over-the-cable upload** — push apps wirelessly over USB via `nexus_cli.py`
- **Return to BootLoader from any app** — hold BtnA during reset, no app modification needed
- **Persistent app storage** — apps live in LittleFS, survive reboots
- **One-command installer** — flash everything onto a fresh device in seconds

---

## Requirements

- M5StickS3
- Python 3.8+
- USB cable

---

## Quick Install

Download the latest release, unzip it, and run:

```bash
python install.py
```

The installer auto-detects your device, installs dependencies if needed, and flashes everything. After it finishes, your stick boots into NEXUS-BOOT.

---

## Usage

### Upload an app

```bash
python nexus_cli.py
```

Select `1`, enter the path to your `.bin` file. The app is transferred over USB and saved to internal storage.

### Launch an app

In the BIOS menu on the stick:
- **BtnB** (side button) — scroll through apps
- **BtnA** (front button) — launch selected app

### Return to BIOS from any running app

Hold **BtnA** (front button, G11), then press **Reset** (side power button). Keep holding BtnA for ~1 second. The device reboots back into NEXUS-BOOT.

This works with **any** app — no modification to the app is required.

---

## Project Structure

```
Nexus_Bootloader/
├── bootloader/          ESP-IDF project — custom second-stage bootloader
│   ├── src/main.c       (empty stub app, bootloader logic is in components)
│   ├── bootloader_components/
│   │   └── ...          GPIO factory-reset hook
│   ├── partitions.csv
│   └── sdkconfig.defaults
├── bootmenu/            Arduino/PlatformIO project — BIOS firmware
│   ├── src/main.cpp
│   ├── platformio.ini
│   └── partitions.csv
├── example.ino.bin      Sample app for testing
└── release 1.0/         Pre-built release (flash with install.py)
    ├── install.py
    ├── bootloader.bin
    ├── nexus_boot.bin
    ├── partitions.bin
    └── nexus_cli.py
```

---

## How It Works

```
Flash layout (8MB):

0x00000  bootloader.bin   Custom bootloader (GPIO factory reset)
0x08000  partitions.bin   Partition table
0x0e000  otadata          OTA boot selection
0x10000  nexus_boot.bin   NEXUS-BOOT firmware  ← factory partition
0x21000  [ota_0]          Loaded apps go here
0x41000  [LittleFS]       App binary storage (~4MB)
```

The custom bootloader checks **G11 (BtnA)** at every boot. If held, it erases `otadata` — the bootloader then falls back to the `factory` partition where NEXUS-BOOT lives. This is a native ESP-IDF feature (`CONFIG_BOOTLOADER_FACTORY_RESET`) so it works regardless of what app is running.

When you launch an app from the menu, NEXUS-BOOT writes it to `ota_0` and sets it as the boot partition. On reset without BtnA held, the app boots normally.

---

## Building from Source

### Bootmenu (BIOS firmware)

Requires [PlatformIO](https://platformio.org/).

```bash
cd bootmenu
pio run
# output: .pio/build/m5stack-sticks3/firmware.bin
```

### Bootloader

Requires PlatformIO with ESP-IDF framework.

```bash
cd bootloader
pio run
# output: .pio/build/bootbuild/bootloader.bin
```

After building, copy both to `release 1.0/` and run `install.py`.

---

## Contributing

PRs and issues are welcome. If you port this to another M5Stack device, open a PR!

---

## Acknowledgements

Built with the help of [Claude](https://claude.ai) by Anthropic —
co-developed the entire partition layout, custom bootloader, and tooling over a very long debugging session.

> *"мы не лошары"* — the developer, somewhere around hour 6

---

## License

MIT
