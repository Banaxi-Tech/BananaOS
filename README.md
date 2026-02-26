# BananaOS

**⚠️ WARNING: Highly Unstable Development OS ⚠️**

BananaOS is currently in active development and is considered highly unstable. It is **NOT recommended** to boot or run this operating system on real hardware. Doing so may lead to data loss, system corruption, or other unexpected issues. The developers are not responsible for any damages that may occur from attempting to run BananaOS on physical devices. Please use emulators (like QEMU) for testing and development purposes only.

## Overview
BananaOS is a 32-bit hobby operating system project, designed for learning and exploration of low-level system programming. It features a custom kernel, a graphical user interface with a window manager, a dock, and several integrated applications. BananaOS is Multiboot compliant and utilizes VESA framebuffer for graphics output.

## Architecture Support

BananaOS is being developed with support for different platforms in mind.

| Platform           | Status        | Notes                                |
| :----------------- | :------------ | :----------------------------------- |
| i386 (BIOS Boot)   | Supported     | Primary development target.          |
| x86_64 (EFI Boot)  | Coming Soon   | Planned for future releases.         |

## System Requirements

To run BananaOS, your system or virtual machine should meet the following specifications:

| Category              | Minimum Requirement         | Recommended Minimum               |
| :-------------------- | :-------------------------- | :-------------------------------- |
| **Processor**         | Intel 486                   | Intel Pentium 2 or equivalent     |
| **RAM**               | 11.2 MB                     | 128 MB                            |
| **Graphics**          | VESA-compatible Framebuffer | VESA-compatible Framebuffer       |
| **Boot Method**       | BIOS                        | BIOS                              |

## Features
*   **Multiboot Compliant Kernel**: Loaded by GRUB, providing essential system services.
*   **VESA Framebuffer Graphics**:
    *   High-resolution graphical output (default 1024x768, configurable via Multiboot).
    *   Double buffering for smooth animations and UI rendering.
    *   Basic graphics primitives: pixel, rectangle, rounded rectangle drawing with alpha blending.
    *   BMP wallpaper support (loaded as a Multiboot module).
*   **Graphical User Interface (GUI)**:
    *   **Window Manager**: Features movable, resizable (maximize/minimize), and closeable windows.
    *   **Application Dock**: A central launcher for integrated applications, with hover animations.
    *   **Theming**: Supports light/dark modes, and customizable rounded or square styles for windows and the dock.
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
*   **CPU Feature Emulation**: Includes an Interrupt Service Routine (ISR) to emulate CMOV (Conditional Move) instructions for older CPUs or specific emulator configurations, ensuring broader compatibility.

## Getting Started

### Prerequisites
To build and run BananaOS, you will need a Debian-based Linux system (such as Ubuntu, Debian, or WSL2).

First, install the necessary build tools:
```bash
sudo apt update && sudo apt install -y build-essential nasm gcc-multilib binutils grub-common grub-pc-bin xorriso
```
For running BananaOS, you will also need QEMU:
```bash
sudo apt install -y qemu-system-i386
```

### Building BananaOS
1.  Clone the repository:
    ```bash
    git clone [YOUR_REPOSITORY_URL]
    cd VibeOS
    ```
2.  Build the OS:
    ```bash
    make
    ```
    This command compiles all C and assembly source files, links them into `vibeos.bin`, and then uses `grub-mkrescue` to create a bootable ISO image `vibeos.img`. This `.img` file is essentially an ISO image and can be renamed to `vibeos.iso` if preferred. The `bg.bmp` file is included in the ISO as the default wallpaper module.

### Running BananaOS in QEMU
After a successful build, you can run BananaOS using QEMU:
```bash
qemu-system-i386 -fda vibeos.img -m 128
```
*(Note: `-m 128` allocates 128MB of RAM to the VM. Adjust as needed. For testing AHCI/SATA, you might need to attach a disk image, e.g., `-drive file=disk.img,format=raw,if=pflash`)*

## Technologies Used
*   **GCC**: C Compiler
*   **NASM**: Assembler
*   **GNU GRUB**: Bootloader and ISO creation (`grub-mkrescue`)
*   **QEMU**: System Emulator

## Directory Structure
*   `boot.s`: Assembly bootloader code.
*   `kernel.c`: Main kernel entry point and core functionalities.
*   `*.c`, `*.h`: C source and header files for various modules (e.g., `mouse.c`, `calc.c`, `ahci.c`, `pci.c`, `fat16.c`, `fat32.c`, `explorer.c`, `dialog.c`, `terminal.c`, `settings.c`).
*   `linker.ld`: Linker script for positioning code and data.
*   `Makefile`: Build automation script.
*   `isodir/`: Temporary directory created during the build process to stage files for the ISO image. Contains `boot/grub/grub.cfg` and `boot/vibeos.bin`, `boot/bg.bmp`.
*   `vibeos.bin`: The compiled kernel executable.
*   `vibeos.img`: The final bootable ISO image.
*   `create_disk.sh`, `create_fat32_disk.sh`: Utility scripts for creating disk images for use with the OS.
*   `bg.bmp`: Default wallpaper image.

## Screenshots
*(Consider adding screenshots of BananaOS running here to showcase its GUI!)*

## Contributing
Contributions are welcome! Please feel free to open issues or submit pull requests.

## License
*(Please specify your license here, e.g., MIT, GPL, etc. If unsure, the MIT License is a common and permissive choice.)*

## Contact
For questions or feedback, please open an issue on GitHub.
