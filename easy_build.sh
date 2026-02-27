#!/bin/bash

# BananaOS Easy Build Script
# -----------------------------------------
# This script installs dependencies, clones,
# and builds BananaOS automatically.

echo "Starting BananaOS Build"

# 1. Update and Install Dependencies
echo "Installing system dependencies (requires sudo)..."
sudo apt update && sudo apt install -y \
    build-essential \
    nasm \
    gcc-multilib \
    binutils \
    grub-common \
    grub-pc-bin \
    xorriso \
    mtools \
    qemu-system
    git \

# 2. Navigate to Desktop
echo "Moving to Desktop..."
cd ~/Desktop || exit

# 3. Clone the Repository
if [ -d "BananaOS" ]; then
    echo "⚠️  BananaOS folder already exists. Pulling latest changes..."
    cd BananaOS && git pull
else
    echo "Cloning BananaOS from GitHub..."
    git clone https://github.com/Banaxi-Tech/BananaOS
    cd BananaOS || exit
fi

# 4. Build the OS
echo "Compiling BananaOS..."
make

# 5. Rename the image to ISO
if [ -f "bananaos.img" ]; then
    echo "✅ Build successful!"
    mv bananaos.img bananaos.iso
    echo "Result: bananaos.iso in the BananaOS Folder on Your Desktop."
    echo "-----------------------------------------"
    echo "To run it install qemu or run in a hypervisor"
    echo "To run in QEMU (128MB): qemu-system-i386 -cdrom bananaos.iso -m 128M"
else
    echo "❌ Build failed. Please check the terminal output for errors."
fi
