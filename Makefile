CC = gcc
AS = nasm
LD = ld

# ADDED -fno-stack-protector and -fno-pie to CFLAGS
CFLAGS = -m32 -ffreestanding -fno-stack-protector -fno-pie -O2 -Wall -Wextra -c -mno-sse
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

OBJS = boot.o kernel.o mouse.o calc.o notepad.o settings.o disk.o fat16.o fat32.o explorer.o dialog.o terminal.o pci.o ahci.o net.o browser.o

all: bananaos.img

boot.o: boot.s
	$(AS) $(ASFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

# Fixed: The rule for bananaos.bin was missing the command line from your previous log
bananaos.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

bananaos.img: bananaos.bin
	mkdir -p isodir/boot/grub
	cp bananaos.bin isodir/boot/bananaos.bin
	# Ensure bg.bmp exists in your folder or this line will fail
	cp bg.bmp isodir/boot/bg.bmp
	echo 'set timeout=10' > isodir/boot/grub/grub.cfg
	echo 'set default=0' >> isodir/boot/grub/grub.cfg
	echo 'menuentry "BananaOS" {' >> isodir/boot/grub/grub.cfg
	echo '	multiboot /boot/bananaos.bin' >> isodir/boot/grub/grub.cfg
	echo '	module /boot/bg.bmp' >> isodir/boot/grub/grub.cfg
	echo '	boot' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg
	grub-mkrescue -o bananaos.img isodir

clean:
	rm -rf *.o bananaos.bin isodir bananaos.img
