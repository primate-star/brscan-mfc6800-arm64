# Makefile for Brother MFC SANE backend - ARM64
# Compiled for Raspberry Pi 5 / DietPi
# Includes patched sanei_usb.c with USB 1.1 packet size fixes

CC = gcc
CFLAGS = -Wall -O2 -fPIC -g -I../include -I. -DHAVE_LIBUSB -DV_MAJOR=1 -DV_MINOR=0
LDFLAGS = -shared -Wl,-soname,libsane-brother.so.1 -Wl,-Bsymbolic
LIBS = -lusb

# Version
V_MAJOR = 1
V_MINOR = 0
V_REV = 8

TARGET = libsane-brother.so.1.0.8
SYMLINK1 = libsane-brother.so.1
SYMLINK2 = libsane-brother.so

# Use patched sources from parent directories
BACKEND_SRC = ../backend/brother.c
SANEI_USB_SRC = ../sanei/sanei_usb.c

# Additional sanei support files
SANEI_SUPPORT = sanei_config.c sanei_constrain_value.c sanei_init_debug.c

OBJECTS = brother.o sanei_usb.o sanei_config.o sanei_constrain_value.o sanei_init_debug.o

.PHONY: all clean install test backup

all: $(TARGET)

# Compile patched brother.c from backend directory
brother.o: $(BACKEND_SRC) ../backend/brother.h
	$(CC) $(CFLAGS) -c $(BACKEND_SRC) -o $@

# Compile patched sanei_usb.c from sanei directory
sanei_usb.o: $(SANEI_USB_SRC)
	$(CC) $(CFLAGS) -c $(SANEI_USB_SRC) -o $@

# Compile local sanei support files
sanei_config.o: sanei_config.c
	$(CC) $(CFLAGS) -c $< -o $@

sanei_constrain_value.o: sanei_constrain_value.c
	$(CC) $(CFLAGS) -c $< -o $@

sanei_init_debug.o: sanei_init_debug.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	ln -sf $(TARGET) $(SYMLINK1)
	ln -sf $(SYMLINK1) $(SYMLINK2)
	@echo "Build complete: $(TARGET)"
	@echo "Chunked USB I/O: writes 16 bytes, reads 64 bytes"
	@echo "Interrupt endpoint polling enabled"

clean:
	rm -f *.o $(TARGET) $(SYMLINK1) $(SYMLINK2)

install: $(TARGET)
	@echo "Installing $(TARGET) to /usr/lib/sane/..."
	install -d /usr/lib/sane/
	install -m 755 $(TARGET) /usr/lib/sane/
	ln -sf $(TARGET) /usr/lib/sane/$(SYMLINK1)
	ln -sf $(SYMLINK1) /usr/lib/sane/$(SYMLINK2)
	@if ! grep -q "^brother$$" /etc/sane.d/dll.conf 2>/dev/null; then \
		echo "brother" >> /etc/sane.d/dll.conf; \
		echo "Added 'brother' to /etc/sane.d/dll.conf"; \
	fi
	install -d /etc/sane.d/
	install -m 644 brother.conf /etc/sane.d/
	ldconfig
	@echo "Installation complete."

# Quick test to verify the scanner is detected
test:
	@echo "Testing scanner detection..."
	SANE_DEBUG_SANEI_USB=5 SANE_DEBUG_BROTHER=5 scanimage -L 2>&1 | head -50
	@echo ""
	@echo "To run a full scan test:"
	@echo "  scanimage -d 'brother:bus2;dev1' --test"

# Backup current working driver
backup:
	@mkdir -p ~/scanner-driver-backup/
	cp -v $(TARGET) $(SYMLINK1) $(SYMLINK2) ~/scanner-driver-backup/ 2>/dev/null || true
	cp -v ../backend/brother.c ~/scanner-driver-backup/
	cp -v ../sanei/sanei_usb.c ~/scanner-driver-backup/
	@echo "Backup saved to ~/scanner-driver-backup/"
