# Brother MFC-6800 SANE Scanner Driver (ARM64)

A patched SANE backend driver for the Brother MFC-6800 multi-function printer/scanner, compiled for ARM64 (aarch64) systems such as the Raspberry Pi 5 and Raspberry Pi Zero 2W.

## Background

The Brother MFC-6800 is a late-1990s USB 1.1 multi-function device. Brother released open-source scanner driver code (`brscan-src-0.2.4`) but only for x86 systems. This project patches and compiles that driver for ARM64 Linux systems, bringing a 20+ year old scanner back to life on modern single-board computers.

### What Was Fixed

The original Brother driver had several issues with modern Linux and USB stacks:

1. **`-Bsymbolic` linker flag** — Ensures the patched `sanei_usb` functions are used instead of the system SANE library's versions. Without this, the driver silently uses unpatched USB code at runtime and scans fail with corrupt output.

2. **USB write chunking** — EP3 OUT (bulk out) has a 16-byte max packet size. Writes are broken into 16-byte chunks to prevent USB protocol errors.

3. **Cancel-on-open recovery** — Sends a cancel command and drains stale data when opening the device, allowing recovery from previously interrupted scans.

4. **Wait-for-ready polling** — Polls the scanner status before reading scan data, checking that `data[4] == 0x00` indicates the scanner is ready.

5. **ADF end-of-document detection** — Properly detects when the automatic document feeder is empty and terminates the scan.

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
| Raspberry Pi 5 | 4-8 GB | ~30 seconds | Fast, recommended for heavy scanning use |
| Pi Zero 2W | 512 MB | ~3 minutes | Works great, just slower to compile |

## Quick Install (Experienced Users)

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

The install script handles everything: blacklists `usblp`, installs dependencies, compiles the driver, configures SANE, and tests scanner detection.

After the script finishes, optionally install the web scanning interface:

```bash
sudo apt install -y curl
curl -s https://raw.githubusercontent.com/sbs20/scanservjs/master/bootstrap.sh | sudo bash -s -- -v latest
```

---

## Complete Installation Guide (Step by Step for Beginners)

This guide assumes you are starting from a **fresh DietPi installation** on a Raspberry Pi 5 or Pi Zero 2W. Every command is explained. If you've never used a Linux terminal before, just follow along — each step tells you exactly what to type.

### What You Need Before Starting

- A **Raspberry Pi 5** or **Raspberry Pi Zero 2W** with DietPi OS installed and SSH enabled
- A **Brother MFC-6800** printer/scanner with a USB cable
- A **computer** (Mac, Windows, or Linux) on the same network as the Pi
- The `brscan-mfc6800-arm64.tar.gz` file downloaded to your computer
- For Pi Zero 2W: a **micro-USB to USB-A OTG adapter** (the Pi Zero 2W only has micro-USB ports)

### Step 1: Find Your Pi's IP Address

If you don't know your Pi's IP address, check your router's admin page for connected devices, or connect a monitor and keyboard to the Pi and run:

```bash
hostname -I
```

This will show something like `192.168.x.x` — this is your Pi's IP address. Write it down; you'll need it throughout this guide.

### Step 2: Transfer the Driver Package to the Pi

**Do NOT plug in the scanner yet.**

Open a terminal on your computer:
- **Mac:** Open the Terminal app (found in Applications → Utilities)
- **Windows:** Open PowerShell (right-click the Start button → Windows PowerShell)
- **Linux:** Open your terminal emulator

Then type:

**Mac/Linux:**
```bash
scp ~/Downloads/brscan-mfc6800-arm64.tar.gz root@<pi-ip>:/root/
```

**Windows (PowerShell):**
```powershell
scp $env:USERPROFILE\Downloads\brscan-mfc6800-arm64.tar.gz root@<pi-ip>:/root/
```

Replace `<pi-ip>` with your Pi's IP address (e.g., `192.168.1.100`).

When prompted:
1. Type `yes` and press Enter to accept the SSH key (first time only)
2. Enter the root password you set during DietPi's first boot

The file transfer takes about 1-2 seconds.

> **Trouble with scp?** If you get "sftp-server: No such file or directory", SSH into the Pi (Step 3) and run: `apt install -y openssh-sftp-server`, then retry the scp command from your computer.

### Step 3: Connect to Your Pi via SSH

From the same terminal on your computer:

**Mac/Linux:**
```bash
ssh root@<pi-ip>
```

**Windows (PowerShell):**
```powershell
ssh root@<pi-ip>
```

Enter the root password when prompted. You should see a prompt like:

```
root@DietPi:~#
```

**Everything from this point on is typed at this prompt (on the Pi), not on your computer.**

### Step 4: Blacklist the usblp Kernel Module

```bash
echo "blacklist usblp" > /etc/modprobe.d/no-usblp.conf
```

> **Why is this needed?** The MFC-6800 has both a printer and scanner interface on the same USB connection. Without this step, the Linux kernel automatically loads a printer driver (`usblp`) that locks the USB device, causing "Device busy" errors when you try to scan. Don't worry — printing through CUPS still works fine without this module.

### Step 5: Install Required Software

DietPi is a very minimal operating system and doesn't include compilers or development libraries out of the box. Install everything the driver needs:

```bash
apt update
apt install -y build-essential libusb-dev sane-utils
```

What these packages do:
- `build-essential` — The C compiler (gcc) and tools needed to compile the driver from source code
- `libusb-dev` — USB communication library that the scanner driver uses to talk to the scanner
- `sane-utils` — The SANE scanner framework and the `scanimage` command-line scanning tool

This takes 2-3 minutes on a Pi 5, or 5-10 minutes on a Pi Zero 2W. Wait until you see the command prompt again.

### Step 6: Extract the Driver Package

```bash
cd /root
tar xzf brscan-mfc6800-arm64.tar.gz
```

This creates a folder `/root/brscan-mfc6800-arm64/` containing all the source code and build files. No output means it worked.

### Step 7: Compile the Driver

```bash
cd /root/brscan-mfc6800-arm64/build
make
```

You should see output ending with:

```
Build complete: libsane-brother.so.1.0.8
Chunked USB I/O: writes 16 bytes, reads 64 bytes
Interrupt endpoint polling enabled
```

This takes about 30 seconds on a Pi 5 or 2-3 minutes on a Pi Zero 2W.

If you see errors instead, go back and make sure Step 5 completed successfully. You can re-run it safely.

### Step 8: Install the Compiled Driver

```bash
cp libsane-brother.so.1.0.8 /usr/lib/aarch64-linux-gnu/sane/
cd /usr/lib/aarch64-linux-gnu/sane/
ln -sf libsane-brother.so.1.0.8 libsane-brother.so.1
ln -sf libsane-brother.so.1 libsane-brother.so
```

What this does:
- Copies the compiled driver to the directory where SANE looks for scanner drivers
- Creates two symbolic links (`libsane-brother.so.1` and `libsane-brother.so`) that SANE uses to find and load the driver

### Step 9: Configure SANE to Use the Driver

```bash
echo "usb 0x04f9 0x0111" > /etc/sane.d/brother.conf
grep -q "^brother" /etc/sane.d/dll.conf || echo "brother" >> /etc/sane.d/dll.conf
```

What this does:
- Creates `brother.conf` telling SANE to look for a USB device with Brother's vendor ID (`04f9`) and the MFC-6800's product ID (`0111`)
- Adds `brother` to SANE's master driver list (`dll.conf`) so it loads our driver when scanning for devices

### Step 10: Connect and Power On the Scanner

Now it's safe to connect the scanner:

1. **Plug the USB cable** from the MFC-6800 into any USB-A port on the Pi
   - On Pi Zero 2W: use a micro-USB to USB-A OTG adapter
2. **Power on** the MFC-6800
3. Wait 5 seconds

Verify the Pi sees the scanner on USB:

```bash
lsusb | grep Brother
```

You should see something like:

```
Bus 003 Device 002: ID 04f9:0111 Brother Industries, Ltd MFC-6800
```

The bus and device numbers will vary — that's normal. If you don't see any output, check that the USB cable is firmly connected and the scanner is powered on.

### Step 11: Test Scanner Detection

```bash
scanimage -L
```

Expected output (bus/device numbers will vary):

```
device `brother:libusb:003:002' is a Brother MFC multi-function peripheral
```

This may take up to 10 seconds as SANE probes all backends.

> **Note:** The numbers `003:002` will be different on your system and change each time the scanner is reconnected. This is normal USB behavior.

### Step 12: Test Scanning

Use the device string from Step 11 (replace `003:002` with your numbers):

```bash
scanimage -d "brother:libusb:003:002" --format=tiff -o /tmp/test.tiff
echo "Exit code: $?"
```

What to expect:
- The scanner motor should physically move (you'll hear it)
- The scan takes a few seconds
- `Exit code: 0` means success
- The scanned image is saved to `/tmp/test.tiff`

**Congratulations! Your Brother MFC-6800 scanner is alive and working on your Pi!**

### Step 13 (Optional): Install ScanServJS Web Interface

ScanServJS gives you a browser-based scanning interface you can access from any device on your network — your phone, tablet, or laptop:

```bash
apt install -y curl
curl -s https://raw.githubusercontent.com/sbs20/scanservjs/master/bootstrap.sh | sudo bash -s -- -v latest
```

This takes 5-10 minutes on a Pi 5, or 15-20 minutes on a Pi Zero 2W. Once complete, open a browser on any device connected to the same network and go to:

```
http://<pi-ip>:8080
```

You should see the ScanServJS interface with your Brother scanner listed. Click "Scan" to scan directly from the browser.

### Step 14 (Optional): Verify After Reboot

Reboot the Pi to make sure everything starts correctly on boot:

```bash
reboot
```

After the Pi comes back up (give it 1-2 minutes), SSH back in and test:

```bash
scanimage -L
```

The scanner should be detected. Note that the device numbers (e.g., `003:002`) will likely be different after reboot — this is normal.

---

## Troubleshooting

### "Device busy" error

The `usblp` kernel module is claiming the USB interface.

```bash
# Check if the module is loaded
lsmod | grep usblp

# Remove it temporarily
sudo rmmod usblp

# Make sure it's blacklisted permanently
cat /etc/modprobe.d/no-usblp.conf
# Should show: blacklist usblp

# If the file doesn't exist or is wrong, recreate it and reboot
echo "blacklist usblp" | sudo tee /etc/modprobe.d/no-usblp.conf
sudo reboot
```

### Scanner not detected by `scanimage -L`

```bash
# 1. Check USB connection
lsusb | grep Brother
# Should show: ID 04f9:0111 Brother Industries, Ltd MFC-6800
# If nothing shows: check cable, try different USB port, power cycle the scanner

# 2. Check SANE config files
cat /etc/sane.d/brother.conf
# Should show: usb 0x04f9 0x0111

grep brother /etc/sane.d/dll.conf
# Should show: brother

# 3. Check driver loads correctly
SANE_DEBUG_DLL=5 scanimage -L 2>&1 | grep brother
# Should show lines about loading libsane-brother
```

### "dlopen failed: file too short"

The driver file got corrupted during copy. Rebuild and reinstall:

```bash
cd /root/brscan-mfc6800-arm64/build
make clean
make
sudo cp libsane-brother.so.1.0.8 /usr/lib/aarch64-linux-gnu/sane/
```

### USB device string changes after reboot

This is completely normal. The `brother:libusb:XXX:YYY` address changes whenever the scanner is reconnected or the Pi reboots. The numbers simply increment.

- **For `scanimage` commands:** Always run `scanimage -L` first to get the current address
- **For ScanServJS:** It handles this automatically through device discovery — no action needed

### Scan produces a blank or corrupt image

1. Power cycle the scanner (turn off, wait 5 seconds, turn on)
2. Wait 5 seconds after power on
3. Try scanning again

If the problem persists, try a different USB port or a shorter USB cable. USB 1.1 devices can be sensitive to cable quality.

### "make" fails with errors

Make sure all dependencies from Step 5 are installed:

```bash
sudo apt install -y build-essential libusb-dev sane-utils
```

Then retry:

```bash
cd /root/brscan-mfc6800-arm64/build
make clean
make
```

### scp fails with "sftp-server: No such file or directory"

DietPi's minimal install may not include the SFTP server. Install it on the Pi:

```bash
apt install -y openssh-sftp-server
```

Then retry the scp command from your computer.

### ScanServJS shows "no devices" after reboot

The scanner detection may need a moment. Try:

```bash
sudo systemctl restart scanservjs
```

If it still doesn't work, verify the scanner is detected on the command line first:

```bash
scanimage -L
```

If `scanimage -L` works but ScanServJS doesn't see the scanner, restart the ScanServJS service again. The USB device address may have changed since the service started.

---

## What's in This Package

| File/Folder | Description |
|-------------|-------------|
| `backend/brother.c` | Main SANE backend — scan commands, device options, data handling |
| `backend/brother.h` | Header with device structs and constants |
| `sanei/sanei_usb.c` | Patched USB I/O — chunked writes, direct reads |
| `build/Makefile` | Build configuration with `-Bsymbolic` linker flag |
| `build/sanei_config.c` | SANE configuration parser |
| `build/sanei_constrain_value.c` | SANE option value validation |
| `build/sanei_init_debug.c` | SANE debug logging initialization |
| `include/sane/` | SANE API headers (22 files) |
| `install.sh` | Automated install script (does Steps 4-12 for you) |
| `LICENSE` | GNU General Public License v2 |
| `README.md` | This file |

## Original Source

Based on `brscan-src-0.2.4` from Brother's open-source driver release and the `sane-backends-1.3.1` SANEI library.

## License

This project is licensed under the GNU General Public License v2 (GPL-2.0), the same license as the original SANE backends and Brother driver source. See [LICENSE](LICENSE) for the full text.

## Contributing

If you have a similar Brother multi-function device from this era (MFC-6800, MFC-9800, DCP-1000, etc.), these patches may work for your model too — the Brother SANE backend supported multiple devices. Pull requests and issue reports welcome.

## Acknowledgments

- [Brother Industries](https://www.brother.com/) for releasing the original scanner driver source code as open source
- [SANE Project](http://www.sane-project.org/) for the Scanner Access Now Easy framework
- [ScanServJS](https://github.com/sbs20/scanservjs) for the excellent web scanning interface
- The original `brscan-src-0.2.4` and `sane-backends` authors
