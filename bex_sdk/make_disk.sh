#!/bin/bash
# Requirements for Arch Linux:
# sudo pacman -S mtools dosfstools

IMG_NAME="bex_disk.img"
BEX_FILE="hello.bex"

if [ ! -f "$BEX_FILE" ]; then
    echo "Error: $BEX_FILE not found! Please run build.sh first."
    exit 1
fi

echo "Creating a 10MB empty disk image..."
dd if=/dev/zero of=$IMG_NAME bs=1M count=10

echo "Formatting image as FAT32..."
mkfs.fat -F 32 $IMG_NAME

echo "Copying $BEX_FILE into the disk image..."
# Use mcopy from mtools to copy the file into the FAT32 image without needing root privileges
mcopy -i $IMG_NAME $BEX_FILE ::/

echo "Done! You can now attach $IMG_NAME to your emulator as a secondary hard drive."
echo ""
echo "Example for QEMU:"
echo "qemu-system-i386 -cdrom ../bananaos.img -drive file=$IMG_NAME,format=raw,index=1"
echo ""
echo "Then inside BananaOS Terminal, you can run:"
echo "/dev/disk1/hello.bex"
