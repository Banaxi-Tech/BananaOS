CC = gcc
AS = nasm
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -c
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

OBJS = boot.o kernel.o mouse.o calc.o notepad.o settings.o disk.o fat16.o fat32.o explorer.o dialog.o terminal.o pci.o ahci.o

all: bananaos.img

boot.o: boot.s
	$(AS) $(ASFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

bananaos.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

bananaos.img: bananaos.bin
	mkdir -p isodir/boot/grub
	cp bananaos.bin isodir/boot/bananaos.bin
	cp bg.bmp isodir/boot/bg.bmp
	echo 'set timeout=0' > isodir/boot/grub/grub.cfg
	echo 'set default=0' >> isodir/boot/grub/grub.cfg
	echo 'menuentry "BananaOS" {' >> isodir/boot/grub/grub.cfg
	echo '	multiboot /boot/vibeos.bin' >> isodir/boot/grub/grub.cfg
	echo '	module /boot/bg.bmp' >> isodir/boot/grub/grub.cfg
	echo '	boot' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg
	grub-mkrescue -o bananaos.img isodir

clean:
	rm -rf *.o bananaos.bin isodir bananaos.img
