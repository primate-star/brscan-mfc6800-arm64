# Brother MFC-6800 SANE Scanner Driver (ARM64)

A patched SANE backend driver for the Brother MFC-6800 multi-function printer/scanner,
compiled for ARM64 (aarch64) systems such as the Raspberry Pi 5 and Raspberry Pi Zero 2W.

## Background

The Brother MFC-6800 is a early-2000's USB 1.1 multi-function device. Brother released
open-source scanner driver code (`brscan-src-0.2.4`) but only for x86 systems. This
project patches and compiles that driver for ARM64 Linux, bringing a 25+ year old scanner
back to life on modern single-board computers.

## Scanning Compatibility

| Mode | Flatbed | ADF Single Page | ADF Multi-Page |
|------|---------|-----------------|----------------|
| **Color** | 100–600 DPI ✓ | 100–300 DPI ✓ | 100–300 DPI ✓ |
| **Grayscale** | 100–600 DPI ✓ | 100–300 DPI ✓ | 100–300 DPI ✓ |
| **Lineart (B&W)** | 100–600 DPI ✓ | 100–300 DPI ✓ | 100–300 DPI ✓ |

## What Was Fixed

**v3.1 (April 2026) — Color scan edge band eliminated**

Color scans at 300 DPI and 600 DPI showed a visible dark band on one edge of the produced image — on the right edge for flatbed scans, on the left edge for ADF scans (ADF flips orientation). Lower resolutions (100/200) showed the same artifact but it blurred to near-invisible.

Root cause: the MFC-6800's color sensor has an unsensed/uncalibrated region on the right side of its scan area. The driver was asking for the full 215.9mm physical width, which extended past the calibrated zone, producing dark or zero-valued pixels at the edge.

Fix: clamp `x_br` in color mode to a per-resolution width derived from Brother's official x86 driver (`brscan-0.2.4`). Verified by capturing reference scans on a Vaio Ubuntu x86 system using `usbmon`, then probing the sensor empirically by shifting a fixed-width scan window left/right — confirming the dead zone lives on the right side of the physical sensor.

Per-resolution maximum widths (matching Brother exactly): 200 DPI → 1632 px; 300 DPI → 2448 px; 600 DPI → 4912 px.

User-specified scan regions via `-l` are still honored, so custom scan-area requests via `scanimage` work as expected.

**v3 (April 2026) — ADF lineart 300 DPI multi-page reliability**

Multi-page ADF scans at 300 DPI lineart would stop at a random page (typically 1–6) with
a 60-second timeout, falsely reporting "Document feeder out of documents". The scanner
emits a "next page ready" sentinel (`0x81`) as a 1-byte USB read that sometimes landed
past a complete record boundary, where the existing end-of-page detection couldn't see
it. Fixed by adding a narrow orphan-sentinel check that inspects the buffer tail after a
brief idle period. Verified against Brother's official x86 driver via usbmon USB protocol
capture.

**v2 — 600 DPI and ADF fixes**

- **600 DPI grayscale shearing** — Changed grayscale X-coordinate rounding to 16-byte
  alignment to match the scanner's padded scan lines, eliminating diagonal distortion.
- **Lineart right-side cutoff** — Fixed B&W X-coordinate rounding (was discarding up to
  ~1 inch on the right edge of every lineart scan).
- **600 DPI color/grayscale timing** — Increased retries and timeouts to accommodate
  USB 1.1 bandwidth at high resolutions.
- **Drain buffer record sync** — Added record boundary detection after scanner init sleep
  to ensure scan data alignment.
- **ADF multi-page support** — Implemented Brother's short next-page command for pages 2+,
  replacing full scan re-initialization and eliminating inter-page timeouts.

**v1 — Core ARM64 compatibility**

- **`-Bsymbolic` linker flag** — Ensures patched `sanei_usb` functions are used at
  runtime instead of the system SANE library's versions.
- **USB write chunking** — Breaks writes into 16-byte chunks matching EP3 OUT max packet
  size.
- **Cancel-on-open recovery** — Sends cancel and drains stale data on device open,
  allowing recovery from interrupted scans.
- **Wait-for-ready polling** — Polls scanner status before reading scan data.
- **ADF end-of-document detection** — Correctly detects feeder empty and terminates scan.

## Supported Hardware

| Component | Details |
|-----------|---------|
| **Scanner** | Brother MFC-6800 (USB ID `04f9:0111`) |
| **Tested Platforms** | Raspberry Pi 5, Raspberry Pi Zero 2W |
| **Compatible With** | Any aarch64/ARM64 Linux system (Pi 4, Pi 3B+ 64-bit, etc.) |
| **Tested OS** | DietPi (Debian Bookworm) |
| **USB** | USB 1.1 (use any USB-A port on the Pi) |

| Platform | RAM | Compile Time |
|----------|-----|-------------|
| Raspberry Pi 5 | 4–8 GB | ~30 seconds |
| Pi Zero 2W | 512 MB | ~3 minutes |

## Quick Install

```bash
git clone https://github.com/primate-star/brscan-mfc6800-arm64.git
cd brscan-mfc6800-arm64
sudo ./install.sh
```

Or from a `.tar.gz`:

```bash
tar xzf brscan-mfc6800-arm64.tar.gz
cd brscan-mfc6800-arm64
sudo ./install.sh
```

The install script will:
1. Blacklist the `usblp` kernel module (required for SANE access)
2. Install build dependencies (`libusb-dev`, `sane-utils`, `build-essential`)
3. Compile the driver from source
4. Install the shared library to `/usr/lib/aarch64-linux-gnu/sane/`
5. Configure SANE (`brother.conf` and `dll.conf`)
6. Test scanner detection

## Web Interface (ScanServJS)

```bash
sudo apt install -y curl
curl -s https://raw.githubusercontent.com/sbs20/scanservjs/master/bootstrap.sh | sudo bash -s -- -v latest
```

Then open `http://<pi-ip-address>:8080` in your browser.

## Manual Scanning

```bash
# List detected scanners
scanimage -L

# Flatbed scan (grayscale, 300 DPI)
scanimage -d "brother:libusb:XXX:YYY" --mode Grayscale --resolution 300 --format=tiff -o scan.tiff

# Flatbed scan (color, 600 DPI)
scanimage -d "brother:libusb:XXX:YYY" --mode Color --resolution 600 --format=tiff -o scan.tiff

# ADF multi-page scan (grayscale, 300 DPI)
scanimage -d "brother:libusb:XXX:YYY" --source ADF --mode Grayscale --resolution 300 --batch --format=tiff

# ADF multi-page scan (lineart, 300 DPI)
scanimage -d "brother:libusb:XXX:YYY" --source ADF --mode 'Black & White' --resolution 300 --batch --format=tiff
```

## Known Limitations

**Scanner firmware prioritizes ADF over flatbed.** If paper is loaded in the ADF when a
flatbed scan is requested, the MFC-6800 will feed from the ADF regardless. Remove paper
from the ADF to use the flatbed.

**Flatbed fallthrough after ADF batch (`scanimage` only).** If `--batch-count=N` is used
and the feeder empties before N pages, the driver may attempt a flatbed scan for the
remaining pages. Not an issue with ScanServJS. Workaround: match `--batch-count` to the
number of pages loaded.

**600 DPI color scanning is slow.** Approximately 2–3 minutes per page over USB 1.1.
Hardware bandwidth limitation, not a driver issue.

**Scanner must be powered on before USB is connected.**

## Troubleshooting

**"Device busy" error:**
```bash
sudo rmmod usblp
cat /etc/modprobe.d/no-usblp.conf   # should show: blacklist usblp
```

**Scanner not detected:**
```bash
lsusb | grep Brother               # should show: ID 04f9:0111 Brother Industries, Ltd
cat /etc/sane.d/brother.conf       # should show: usb 0x04f9 0x0111
grep brother /etc/sane.d/dll.conf  # should show: brother
```

**Scanner gets stuck (no response):**
Power cycle the scanner (off, wait 5 seconds, on), then re-run `scanimage -L` — the USB
device address changes on each reconnect.

**ADF shows "cover open" error:**
Check the ADF paper guide slider — set it to the multi-page position for batch scanning.

## Uninstall

```bash
sudo rm -f /usr/lib/aarch64-linux-gnu/sane/libsane-brother.so*
sudo rm -f /etc/sane.d/brother.conf
sudo sed -i '/^brother$/d' /etc/sane.d/dll.conf
```
