# BananaOS

> [!CAUTION]
> BananaOS is currently in active development and is considered unstable. It is **NOT recommended** to boot or run this operating system on real hardware. Doing so may lead to data loss, system corruption, or other unexpected issues. The developers are not responsible for any damages that may occur from attempting to run BananaOS on physical devices. Please use emulators (like QEMU) for testing and development purposes only.

## Overview
BananaOS is one of the first Vibecoded Operating System Projects, designed for learning and exploration of low-level system programming. It features a custom kernel, a graphical user interface with a window manager, a dock, and several integrated applications. BananaOS is Multiboot compliant and utilizes VESA framebuffer for graphics output.

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
sudo apt install -y qemu-system
```

### Building BananaOS
1.  Clone the repository:
    ```bash
    git clone https://github.com/Banaxi-Tech/BananaOS
    cd BananaOS
    ```
2.  Build the OS:
    ```bash
    make
    ```
### This Command Compiles BananaOS This `.img` file is essentially an ISO image and can be renamed to `bananaos.iso` if preferred.

### Running BananaOS in QEMU
After a successful build, you can run BananaOS using QEMU:
```bash
qemu-system-i386 -cdrom bananaos.img -m 128M
```
### Running BananaOS in QEMU With a 486
After the successful build run
```bash
qemu-system-i386 -cpu 486 -cdrom bananaos.img -m 11.2M
```

## Technologies Used
*   **GCC**: C Compiler
*   **NASM**: Assembler
*   **GNU GRUB**: Bootloader and ISO creation
*   **QEMU**: System Emulator


## Contributing
Contributions are welcome! Please feel free to open issues or submit pull requests.



## Contact
For questions or feedback, please open an issue on GitHub.
