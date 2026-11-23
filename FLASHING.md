# Flashing DigiFrame

Prebuilt binaries live in `build/` after a compile.

## Build (command line)

```
arduino-cli compile --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,CDCOnBoot=cdc --output-dir build firmware/DigiFrame
```

`PartitionScheme=custom` uses the sketch's `partitions.csv` (4 MB OTA app
slots + ~7.9 MB `ffat`). In Arduino IDE pick Partition Scheme **Custom** with
the other board settings from the header of `DigiFrame.ino`.

> Repartition caveat: this 4 MB-app layout moves the `ffat` data partition
> (app1 now at `0x410000`, ffat at `0x810000`). The first flash that carries
> the new partition table (the `_0x0` or `merged` image, address `0x0`) wipes
> LittleFS once — the default GIF pack re-seeds and config resets to defaults.
> App-only flashes at `0x10000` don't touch it.

## Flash — pick ONE of these

### 0) OTA — over WiFi, no cable (easiest once the frame is running)

Open the dashboard (http://digiframe.local) → **Firmware (OTA)** →
pick `build/DigiFrame.ino.bin` → Update firmware. The panel shows
"UPDATING" with progress and the frame reboots into the new firmware.
Stored GIFs/config are untouched.

Only the plain app image works here — the `_0x0` / `merged` images are
rejected on purpose ("not an app image").

> Serial caveat after an OTA: the frame may now boot from the second
> app slot (`app1`), so a later **serial** app-only flash at `0x10000`
> can appear to do nothing. Either flash the `_0x0` image (it resets
> the boot selection) or first run
> `esptool --chip esp32s3 --port COM5 erase-region 0xe000 0x2000`.

The serial options below — replace `COM5` with your port (check Device
Manager). Hold BOOT while plugging in if the port doesn't appear.

### A) Full image, keeps stored GIFs/config (recommended)

`DigiFrame_flash_at_0x0.bin` contains bootloader + partition table +
app and stops before the data partition, so LittleFS (GIFs, events,
config) survives:

```
esptool --chip esp32s3 --port COM5 write-flash 0x0 build/DigiFrame_flash_at_0x0.bin
```

Also works with a browser flasher (e.g. https://espressif.github.io/esptool-js/)
— address `0x0`.

### B) App only (fastest, after the first flash)

```
esptool --chip esp32s3 --port COM5 write-flash 0x10000 build/DigiFrame.ino.bin
```

> **Panel dark after changing the wiring on the dashboard?** Power-cycle the
> frame: an unconfirmed wiring is only ever tried for one boot, so the next
> boot is back on the last wiring you kept (it also reverts on its own after
> 10 minutes). A *kept* wiring that is wrong can be changed back from the
> dashboard, which works without the panel; failing that, a factory reset
> (below) returns to the `config.h` pins.

### C) Factory reset — wipes EVERYTHING including stored GIFs

`DigiFrame.ino.merged.bin` is the padded 16 MB image; flashing it
erases the LittleFS partition too:

```
esptool --chip esp32s3 --port COM5 write-flash 0x0 build/DigiFrame.ino.merged.bin
```

> Note: install `PubSubClient` (Home Assistant MQTT) from Library Manager
> before building. The app is ~1.5 MB and targets a **4 MB** app partition
> (`PartitionScheme=custom`), so there's plenty of headroom.
>
> `CDCOnBoot=cdc` is required if you want to read `Serial` output over the
> board's native USB port — without it Arduino's `Serial` goes to the UART0
> pins and you'll see only ROM and panic messages.

## First boot / new WiFi

If the frame can't reach the stored WiFi it starts a hotspot and shows a
QR code on the panel.

1. Point your phone's camera at the panel QR — it encodes a `WIFI:` payload,
   so it offers to join the `DigiFrame` hotspot directly (pass `digiframe123`
   if you'd rather join manually from WiFi settings).
2. The captive page pops up (or open http://192.168.4.1 — once a phone has
   joined, the panel QR switches to this URL too).
3. Enter your WiFi → Save & connect. When online, the dashboard is at
   http://digiframe.local.

Either way, if the old network reappears the frame rejoins it automatically.
For control when you're away from home, use the Telegram bot.
