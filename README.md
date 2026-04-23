# Brother MFC-6800 SANE Scanner Driver (ARM64)

A patched SANE backend driver for the Brother MFC-6800 multi-function printer/scanner, compiled for ARM64 (aarch64) systems such as the Raspberry Pi 5 and Raspberry Pi Zero 2W.

## Background

The Brother MFC-6800 is a late-1990s USB 1.1 multi-function device. Brother released open-source scanner driver code (`brscan-src-0.2.4`) but only for x86 systems. This project patches and compiles that driver for ARM64 Linux systems, bringing a 25+ year old scanner back to life on modern single-board computers.

## Features

### Scanning Modes & Resolutions

| Mode | Flatbed | ADF Single Page | ADF Multi-Page |
|------|---------|-----------------|----------------|
| **Color** | 100–600 DPI ✓ | 100–300 DPI ✓ | 100–300 DPI ✓ |
| **Grayscale** | 100–600 DPI ✓ | 100–300 DPI ✓ | 100–300 DPI ✓ |
| **Lineart (B&W)** | 100–600 DPI ✓ | 100–300 DPI ✓ | 100–200 DPI ✓ |

### What Was Fixed

**v3 (April 2026) — ADF lineart 300 DPI multi-page reliability**

Previously, multi-page ADF scans at 300 DPI lineart would stop at a random page (typically 1-6) with a 60-second timeout, incorrectly reporting "Document feeder out of documents". The scanner emits a "next page ready" sentinel (0x81) as a 1-byte USB read that sometimes landed past a complete record boundary, where the existing end-of-page detection couldn't see it. Fixed by adding a narrow orphan-sentinel check that inspects the buffer tail after a brief idle period. Verified against Brother's original x86 Linux driver via usbmon protocol capture.


**Original fixes (v1):**

1. **`-Bsymbolic` linker flag** — Ensures the patched `sanei_usb` functions are used instead of the system SANE library's versions. Without this, the driver silently uses unpatched USB code at runtime and scans fail with corrupt output.

2. **USB write chunking** — EP3 OUT (bulk out) has a 16-byte max packet size. Writes are broken into 16-byte chunks to prevent USB protocol errors.

3. **Cancel-on-open recovery** — Sends a cancel command and drains stale data when opening the device, allowing recovery from previously interrupted scans.

4. **Wait-for-ready polling** — Polls the scanner status before reading scan data, checking that `data[4] == 0x00` indicates the scanner is ready.

5. **ADF end-of-document detection** — Properly detects when the automatic document feeder is empty and terminates the scan.

**600 DPI and ADF fixes (v2):**

6. **600 DPI grayscale shearing fix** — The scanner uses 16-byte aligned scan lines at 600 DPI, producing 8 bytes of padding per row. Changed grayscale X-coordinate rounding from 8-byte (`~7`) to 16-byte (`~15`) alignment, eliminating the diagonal image distortion.

7. **Lineart right-side cutoff fix** — Changed B&W X-coordinate rounding from 256-pixel (`~255`) to 8-pixel (`~7`) alignment. The original rounding was discarding up to 250 pixels (~1 inch) on the right edge of every lineart scan.

8. **600 DPI color/grayscale timing** — Increased I-command retries (10→20), retry interval (200ms→500ms), scanner initialization wait (3→6 seconds), and data timeout (5→60 seconds) to accommodate USB 1.1 bandwidth constraints at high resolutions.

9. **Drain buffer record synchronization** — After the scanner initialization sleep, the USB buffer may contain mid-record data. Added record boundary detection with double-header verification for grayscale modes, ensuring scan data alignment.

10. **ADF multi-page support** — Implemented Brother's short next-page command (`\x1BX\n\x80`) for ADF pages 2+, replacing the full scan re-initialization. Eliminates inter-page timeouts that caused the scanner to stop feeding.

## Supported Hardware

| Component | Details |
|-----------|---------|
| **Scanner** | Brother MFC-6800 (USB ID `04f9:0111`) |
| **Tested Platforms** | Raspberry Pi 5, Raspberry Pi Zero 2W |
| **Compatible With** | Any aarch64/ARM64 Linux system (Pi 4, Pi 3B+ 64-bit, etc.) |
| **Tested OS** | DietPi (Debian Bookworm) |
| **USB** | USB 1.1 connection (use any USB-A port on the Pi) |

### Platform Notes

| Platform | RAM | Compile Time | Notes |
|----------|-----|-------------|-------|
| Raspberry Pi 5 | 4–8 GB | ~30 seconds | Fast, recommended for heavy scanning use |
| Pi Zero 2W | 512 MB | ~3 minutes | Works great, just slower to compile |

## Quick Install

```bash
git clone https://github.com/primate-star/brscan-mfc6800-arm64.git
cd brscan-mfc6800-arm64
sudo ./install.sh
```

Or if you downloaded the `.tar.gz`:

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

For a browser-based scanning interface:

```bash
apt install -y curl
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

# ADF multi-page scan (lineart, 200 DPI)
scanimage -d "brother:libusb:XXX:YYY" --source ADF --mode 'Black & White' --resolution 200 --batch --format=tiff
```

## Known Limitations

**Scanner firmware prioritizes ADF over Flatbed.** If paper is loaded in the ADF when a flatbed scan is requested, the MFC-6800 firmware will feed from the ADF anyway. This is 1999-era firmware behavior, not a driver issue — remove paper from the ADF to scan the flatbed.

**Flatbed auto-scan after ADF batch (scanimage only).** When `scanimage --batch-count=N` is used with ADF and the feeder empties before reaching N pages, the driver may fall through to a flatbed scan on the next page request. Not an issue for scanservjs (which does not specify a fixed page count). Workaround: match `--batch-count` to the number of pages loaded.


- **ADF lineart multi-page at 300 DPI**: May stop after several pages due to USB 1.1 timing sensitivity. Lineart data is very small and transfers quickly, leaving insufficient time for inter-page command processing. Use 100–200 DPI for reliable multi-page lineart ADF scanning, or use Grayscale mode at 300 DPI.
- **600 DPI color scanning**: Slow over USB 1.1 (~2–3 minutes per page). This is a hardware bandwidth limitation, not a driver issue.
- **Scanner must be powered on before connecting USB**: The MFC-6800 requires power before the USB handshake occurs.

## Troubleshooting

**"Device busy" error:**
```bash
# The usblp module is loaded. Remove it:
sudo rmmod usblp
# Verify it's blacklisted:
cat /etc/modprobe.d/no-usblp.conf
# Should show: blacklist usblp
```

**Scanner not detected:**
```bash
# Check USB connection:
lsusb | grep Brother
# Should show: Bus XXX Device YYY: ID 04f9:0111 Brother Industries, Ltd

# Check SANE configuration:
cat /etc/sane.d/brother.conf
# Should show: usb 0x04f9 0x0111

grep brother /etc/sane.d/dll.conf
# Should show: brother
```

**Scanner gets stuck (no response):**
Power cycle the scanner (turn off, wait 5 seconds, turn on), then retry. The USB device address changes on each reconnect, so use `scanimage -L` to find the new address.

**ADF shows "cover open" error:**
Check the physical ADF paper guide slider — it has single-page and multi-page positions. Set it to multi-page for batch scanning.

## Uninstall

```bash
sudo rm -f /usr/lib/aarch64-linux-gnu/sane/libsane-brother.so*
sudo rm -f /etc/sane.d/brother.conf
sudo sed -i '/^brother$/d' /etc/sane.d/dll.conf
```

## Project Structure

```
brscan-mfc6800-arm64/
├── backend/
│   ├── brother.c          # Main SANE backend driver (patched)
│   └── brother.h          # Driver header with device structures
├── sanei/
│   ├── sanei_usb.c        # USB I/O layer (patched for chunked writes)
│   └── *.c                # Other SANE internal functions
├── include/sane/          # SANE API headers
├── build/
│   └── Makefile           # Build configuration
├── install.sh             # Automated installer
├── LICENSE                # GPL v2
└── README.md              # This file
```

## License

GPL v2 — See [LICENSE](LICENSE) for details.

Based on the SANE external backend by Frank Trefz and Brother Industries open-source scanner driver code.
