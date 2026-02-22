#!/bin/bash
# Brother MFC-6800 SANE Scanner Driver - ARM64 Installer
# For Raspberry Pi 5 / DietPi (aarch64)

set -e

echo "=== Brother MFC-6800 Scanner Driver Installer ==="

# Check architecture
ARCH=$(uname -m)
if [ "$ARCH" != "aarch64" ]; then
    echo "ERROR: This driver is for ARM64 (aarch64) only. Detected: $ARCH"
    exit 1
fi

# Check root
if [ "$EUID" -ne 0 ]; then
    echo "ERROR: Please run as root (sudo ./install.sh)"
    exit 1
fi

# Step 1: Blacklist usblp
echo "[1/6] Blacklisting usblp kernel module..."
echo "blacklist usblp" > /etc/modprobe.d/no-usblp.conf
rmmod usblp 2>/dev/null || true

# Step 2: Install dependencies
echo "[2/6] Installing dependencies..."
apt-get update -qq
apt-get install -y -qq libusb-dev sane-utils build-essential > /dev/null 2>&1

# Step 3: Build
echo "[3/6] Compiling driver..."
cd "$(dirname "$0")/build"
make clean > /dev/null 2>&1 || true
make 2>&1 | tail -3

# Step 4: Install
echo "[4/6] Installing driver..."
SANE_DIR="/usr/lib/aarch64-linux-gnu/sane"
mkdir -p "$SANE_DIR"
cp libsane-brother.so.1.0.8 "$SANE_DIR/"
cd "$SANE_DIR"
ln -sf libsane-brother.so.1.0.8 libsane-brother.so.1
ln -sf libsane-brother.so.1 libsane-brother.so

# Step 5: Configure SANE
echo "[5/6] Configuring SANE..."
echo "usb 0x04f9 0x0111" > /etc/sane.d/brother.conf
grep -q "^brother" /etc/sane.d/dll.conf 2>/dev/null || echo "brother" >> /etc/sane.d/dll.conf

# Step 6: Test
echo "[6/6] Testing scanner detection..."
echo ""
RESULT=$(scanimage -L 2>&1)
if echo "$RESULT" | grep -q "Brother"; then
    echo "SUCCESS! Scanner detected:"
    echo "$RESULT" | grep Brother
    echo ""
    DEVICE=$(echo "$RESULT" | grep -oP "brother:libusb:\d+:\d+")
    echo "To test scanning:"
    echo "  scanimage -d \"$DEVICE\" --format=tiff -o /tmp/test.tiff"
    echo ""
    echo "To install ScanServJS web interface:"
    echo "  apt install -y curl"
    echo "  curl -s https://raw.githubusercontent.com/sbs20/scanservjs/master/bootstrap.sh | sudo bash -s -- -v latest"
    echo "  Then open http://$(hostname -I | awk '{print $1}'):8080"
else
    echo "WARNING: Scanner not detected. Make sure it is powered on and connected via USB."
    echo "Try: lsusb | grep Brother"
    echo "You may need to reboot for the usblp blacklist to take effect."
fi
