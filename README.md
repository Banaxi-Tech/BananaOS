# BananaOS

<p align="center">
  <img src="https://raw.githubusercontent.com/Banaxi-Tech/BananaOS/refs/heads/main/In%20Vmware.png" alt="BananaOS Desktop" width="700">
</p>

<p align="center">
  <strong>A lightweight, graphical operating system written from scratch in C and Assembly</strong>
</p>

<p align="center">
  <a href="#features">Features</a> •
  <a href="#getting-started">Getting Started</a> •
  <a href="#building">Building</a> •
  <a href="#running">Running</a> •
  <a href="#contributing">Contributing</a>
</p>

---

> **Warning**
> BananaOS is in active development and is considered unstable. It is **NOT recommended** to run this on real hardware. Doing so may lead to data loss or system corruption. Please use emulators (QEMU, VirtualBox, VMware) for testing. The developers are not responsible for any damages.

## Overview

BananaOS is a hobby operating system designed for learning and exploring low-level system programming. It features a custom kernel, a graphical user interface with a window manager, an application dock, and several integrated applications. BananaOS is Multiboot compliant and uses VESA framebuffer for graphics.

### Platform Support

| Platform | Status | Notes |
|:---------|:-------|:------|
| i386 (BIOS) | **Supported** | Primary development target |
| x86_64 (BIOS) | In Progress | Coming soon |
| x86_64 (EFI) | Planned | Future release |

### System Requirements

| | Minimum | Recommended |
|:--|:--------|:------------|
| **CPU** | Intel 486 | Pentium II or better |
| **RAM** | 4 MB | 128 MB |
| **VRAM** | 1 MB | 8 MB |
| **Boot** | BIOS | BIOS |

> **Note:** Since the latest update the setup iso can also boot on 4MB RAM

## Features
*   **Multiboot Compliant Kernel**: Loaded by GRUB, providing essential system services.
*   ** Newly Added Network Support**
*   **VESA Framebuffer Graphics**:
*   **Frosted Glass**: Optional Effect for Powerful hardware for transparent titlebars.
    *   High-resolution graphical output (default 1024x768, configurable via Multiboot).
    *   Double buffering for smooth animations and UI rendering.
    *   Basic graphics primitives: pixel, rectangle, rounded rectangle drawing with alpha blending.
    *   BMP wallpaper support (loaded as a Multiboot module).
*   **Graphical User Interface (GUI)**:
    *   **Window Manager**: Features movable, resizable (maximize/minimize), and closeable Windows.
    *   **Application Dock**: A central launcher for integrated applications, with hover animations.
    *   **Theming**: Supports light/dark modes, and customizable rounded or square styles for Windows and the dock.
*   **Input Handling**:
    *   PS/2 Mouse support for cursor control and UI interaction.
    *   PS/2 Keyboard support with scancode to ASCII conversion and Shift key handling.
*   **Integrated Applications**:
    *   **Terminal**: A basic command-line interface.
    *   **Calculator**: A simple arithmetic calculator.
    *   **Notepad**: A basic text editor.
    *   **File Explorer**: Navigates supported file systems (IDE/SATA drives).
    *   **Settings**: Allows customization of theme (light/dark, rounded/square UI elements).
*   **Drivers**:
    *   AHCI (Advanced Host Controller Interface) driver for SATA storage devices.
    *   PCI (Peripheral Component Interconnect) bus enumeration.
*   **Memory Management**: Basic physical memory detection and backbuffer allocation strategy based on available RAM. Includes checks for minimum memory requirements.
* **486 Retro-Compatibility**: Includes an ISR to emulate CMOV instructions, allowing the OS to boot on original Intel 486 hardware.


### Kernel & Core
- **Multiboot Compliant** — Loaded by GRUB bootloader
- **486 Compatibility** — CMOV instruction emulation for vintage hardware
- **Memory Management** — Physical memory detection and allocation

### Graphics
- **VESA Framebuffer** — High-resolution output (default 1920×1080, configurable)
- **Double Buffering** — Smooth, flicker-free rendering
- **Alpha Blending** — Transparent UI elements
- **BMP Wallpapers** — Custom background support

### User Interface
- **Window Manager** — Draggable, resizable, minimizable Windows
- **Application Dock** — Animated launcher bar
- **Theming** — Light/dark modes, rounded/square styles
- **Top Bar** — System menu with clock

### Drivers
- **Storage** — IDE (PIO mode) and SATA (AHCI) support
- **Filesystem** — FAT16 and FAT32 read support
- **Input** — PS/2 keyboard and mouse
- **Network** — Intel E1000 Ethernet driver
- **PCI** — Bus enumeration and device detection

### Applications
| App | Description |
|:----|:------------|
| **Terminal** | Command-line interface with disk/network commands |
| **Calculator** | Basic arithmetic operations |
| **Notepad** | Simple text editor |
| **File Explorer** | Browse FAT16/FAT32 drives |
| **Settings** | Theme and UI customization |
| **Browser** | Basic HTTP client (experimental) |

## Project Structure

```
BananaOS/
├── apps/                   # Application code
│   ├── apps.h              # Shared app definitions
│   ├── browser.c           # Web browser
│   ├── calc.c              # Calculator
│   ├── dialog.c            # Dialog windows
│   ├── explorer.c          # File explorer
│   ├── loader.c            # BEX executable loader
│   ├── notepad.c           # Text editor
│   ├── settings.c          # Settings app
│   └── terminal.c          # Terminal emulator
├── drivers/                # Hardware drivers
│   ├── ahci.c/h            # SATA/AHCI driver
│   ├── disk.c/h            # Disk abstraction
│   ├── fat16.c/h           # FAT16 filesystem
│   ├── fat32.c/h           # FAT32 filesystem
│   ├── mouse.c             # PS/2 mouse driver
│   ├── net.c/h             # E1000 network driver
│   └── pci.c/h             # PCI bus driver
├── setup/                  # Installer
│   └── setup.c             # Setup wizard
├── kernel.c                # Main kernel
├── boot.s                  # Boot assembly
├── font.h                  # 8x8 bitmap font
├── linker.ld               # Linker script
├── Makefile                # Build system
└── bg.bmp                  # Default wallpaper
```


## Getting Started

### Prerequisites
You need a Linux system with development tools. Debian/Ubuntu is recommended.

### Quick Build (One Command)

```bash
curl -sSL https://raw.githubusercontent.com/Banaxi-Tech/BananaOS/refs/heads/main/easy_build.sh | bash
```

This downloads, builds, and creates both `bananaos.img` (portable OS) and `setup.iso` (installer).

## Building

### Install Dependencies

**Debian/Ubuntu:**
```bash
sudo apt update
sudo apt install -y build-essential nasm gcc-multilib binutils grub-common grub-pc-bin xorriso mtools qemu-system
```


### Build Commands

```bash
# Clone the repository
git clone https://github.com/Banaxi-Tech/BananaOS
cd BananaOS

# Build the portable OS image
make buildPortable

# Build the installer ISO (includes portable image)
make buildSetup
```

### Build Outputs

| File | Description |
|:-----|:------------|
| `bananaos.img` | Portable bootable image (can be renamed to .iso) |
| `setup.iso` | Installer that writes the OS to a target disk |

## Running

### QEMU (Recommended)

**Basic:**
```bash
qemu-system-i386 -cdrom bananaos.img -m 128M
```

**With networking:**
```bash
qemu-system-i386 -cdrom bananaos.img -m 128M -netdev user,id=n -device e1000,netdev=n
```

**486 mode (4MB RAM):**
```bash
qemu-system-i386 -cpu 486 -cdrom bananaos.img -m 4M
```

### Running the Installer

```bash
# Create a virtual disk
qemu-img create -f raw disk.img 128M

# Boot the installer
qemu-system-i386 -cdrom setup.iso -m 128M -drive file=disk.img,format=raw

# After installation, boot from the disk
qemu-system-i386 -drive file=disk.img,format=raw -m 128M
```

### VirtualBox / VMware

1. Create a new VM (Type: Other, Version: Other/Unknown)
2. Allocate at least 128 MB RAM
3. Attach `bananaos.img` or `setup.iso` as a CD/DVD
4. Boot the VM

## Terminal Commands

| Command | Description |
|:--------|:------------|
| `lsdisk` | List available disks |
| `ls /dev/disk0` | List files on a disk |
| `mount /dev/disk0 /mnt` | Mount a disk |
| `cat /mnt/file.txt` | Display file contents |
| `clear` | Clear terminal |
| `netinfo` | Show network information |
| `ping <ip>` | Ping an IP address |

## Technologies

- **C** — Kernel and applications
- **NASM** — x86 assembly
- **GCC** — Cross-compilation
- **GNU GRUB** — Bootloader
- **QEMU** — Testing and emulation

## Contributing

Contributions are welcome! Feel free to:
- Report bugs by opening an issue
- Submit pull requests with improvements
- Suggest new features

## License

See [LICENSE](LICENSE) for details.

## Contact

For questions or feedback, please [open an issue](https://github.com/Banaxi-Tech/BananaOS/issues) on GitHub.
