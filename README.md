<!--
Copyright (c) Meta Platforms, Inc. and affiliates.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# Muse gadget firmware for the Waveshare ESP32-S3-Touch-LCD-1.85

A [Muse gadget](https://github.com/facebookincubator/muse-gadget-sdk) port for
the **plain** [Waveshare
ESP32-S3-Touch-LCD-1.85](https://www.waveshare.com/esp32-s3-touch-lcd-1.85.htm)
— the 55 × 55 mm square-PCB board with a round 360 × 360 screen, **not** the
round 1.85C and not the 1.85B. Board target: **`lcd185`**.

Upstream has no 1.85 board, and the four community ports that exist all target
the 1.85C. This adds the missing sibling, the same way upstream carries both
the AMOLED 1.75 and 1.75C behind one driver.

## Lineage

| Layer | Source |
| --- | --- |
| Base SDK | [facebookincubator/muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk) (Apache-2.0) |
| 1.85C board driver, battery and sleep work | [jrbecart/muse-forty2a](https://github.com/jrbecart/muse-forty2a) (Apache-2.0) |
| This port | the `lcd185` target below |

## What the port changes

The 1.85 and the 1.85C "V1" are the same silicon — ESP32-S3R8, 16 MB
W25Q128JVSI flash, 8 MB octal in-package PSRAM, native USB-Serial/JTAG — and
the same ST77916 360 px QSPI panel, TCA9554 expander, CST816 touch, PCM5101
DAC and single I2S MEMS mic. Two things differ, so the 1.85C driver is shared
and both differences sit behind `CONFIG_MUSE_BOARD_WAVESHARE_S3_LCD_185`:

1. **Touch has its own I2C bus.** On the 1.85C the CST816 shares the
   expander's bus (GPIO11/GPIO10). On the 1.85 it answers on **GPIO1/GPIO3**,
   so the driver opens a second master bus on `I2C_NUM_1` and hands that to
   the touch panel IO. `TP_RST` stays on expander EXIO1, on the main bus.
2. **There is a real soft power latch.** The 1.85C has a mechanical slide
   switch; the 1.85 holds its battery rail up with **GPIO7**, raised first
   thing in `init()` (as Waveshare's own `PWR_Init` does, or the board dies the
   moment it is unplugged) and dropped in `power_off()` to cut power outright.
   The deep sleep still follows it, because dropping the latch does nothing
   while USB feeds the regulator.

The revision probe is also skipped: the 1.85 is only ever the PCM5101 + I2S
mic chain, never ES8311 + ES7210, so `s_v2` is pinned false.

### Pins

| Function | GPIO |
| --- | --- |
| LCD (ST77916, QSPI, 80 MHz) | CS 21, SCK 40, D0 46, D1 45, D2 42, D3 41, TE 18 |
| LCD backlight | 5 (LEDC PWM, active high) |
| Shared I2C — TCA9554 `0x20`, QMI8658 `0x6B`, PCF85063 `0x51` | SDA 11, SCL 10 |
| **Touch I2C — CST816 `0x15`** | **SDA 1, SCL 3**, INT 4, RST = EXIO1 |
| Speaker — PCM5101 DAC → APA2068 amp (I2S0) | BCLK 48, WS 38, DOUT 47, no MCLK |
| Mic — ICS-43434 (I2S1, right slot, 32-bit) | SCK 15, WS 2, SD 39 |
| Battery sense | 8 (ADC1_CH7, 1/3 divider) |
| **Power latch / power key** | **7 (hold high) / 6 (active low)** |
| Talk button | 0 (BOOT) |

Expander: EXIO1 = TP_RST, EXIO2 = LCD_RST, EXIO3 = SD_CS.

Sources: the [official 1.85
schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.85/ESP32-S3-Touch-LCD-1.85.pdf),
the [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.85)
and [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32/tree/main/main/boards/waveshare)'s
board config for the same hardware.

## Build

ESP-IDF **v6.0.1** exactly — the SDK does not support any other version, and
v6.0.2 / v6.1 fail with missing-header errors.

```sh
git clone -b v6.0.1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6.0.1
~/esp/esp-idf-v6.0.1/install.sh esp32s3
. ~/esp/esp-idf-v6.0.1/export.sh
```

Get an SDK token from
[gadgets.muse.ai/settings/sdk-tokens](https://gadgets.muse.ai/settings/sdk-tokens)
and put it where it will not be committed:

```sh
cd esp32
cp devices/sdkconfig.local.example devices/sdkconfig.local
$EDITOR devices/sdkconfig.local        # paste your mgst_... token
```

`devices/sdkconfig.local` is gitignored and loaded last by `board.sh`. Without
a token the firmware builds and boots but never pairs.

```sh
tools/muse/board.sh build lcd185
tools/muse/board.sh flash lcd185
idf.py -B build-muse-waveshare-s3-lcd-185 monitor     # Ctrl-] to quit
```

Coming from the Waveshare factory demo, erase once first — Muse's `nvs` and
`prod_data` partitions sit where other firmware keeps its data:

```sh
idf.py -p /dev/cu.usbmodem* erase-flash
```

A good boot logs `board V1: PCM5101 + I2S mic` and
`Waveshare ESP32-S3-Touch-LCD-1.85`, finds 8 MB of PSRAM, and does not
mention `touch unavailable`.

### Back up the factory image first

The vendor demo is only distributed as a 200 MB Google Drive zip, so the flash
dump is the practical way back:

```sh
mkdir -p backups && esptool -p /dev/cu.usbmodem* read-flash 0 0x1000000 \
    backups/factory-esp32s3-touch-lcd-1.85-16MB.bin
```

`backups/` is gitignored. Restore with `write-flash 0 <file>`.

## Pairing

A Muse account and the Muse phone app are required; the SDK itself needs no
subscription. In the app, **Settings → Devices**, turn on **Developer mode**,
then look for `MuseGadget-XXXXXX` (the last three octets of the Wi-Fi MAC).
Confirm with a short press of BOOT when the status asks for it. Hold BOOT for
5 s to unpair and forget Wi-Fi.

Replies arrive as **text**. Upstream ships no TTS, so the speaker stays quiet
until something fills `start_tts` in
`esp32/components/muse/muse_chat_session.cpp`.

## Known limits on this board

- **No AEC and one mic.** The 1.85 has no audio codec and no reference
  channel, with an open-air speaker ~20 mm from the mic, so barge-in is rough.
  Waveshare's own demo refuses to recognise speech during playback. The 1.85B
  and 1.85C V2 (ES8311 + ES7210) are the variants to pick for serious voice.
- **No TTS**, as above.
- **The power key (GPIO6) is not wired to an input event** yet; sleep and
  power off are in the touch settings. The latch itself works.
- **The QMI8658 IMU is unused.** The 1.85C has no IMU, so the shared driver
  never learned it; it is there on this board for the taking.

## Enclosure

A 3D-printable case and stand (STL, STEP and a Bambu Studio project) is in
[`enclosure/`](enclosure/).

## License

Apache-2.0, as upstream. See [LICENSE](LICENSE). The `esp32/avatar/` artwork
is **not** Apache-licensed — see its own terms.
