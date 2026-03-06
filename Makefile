CC = gcc
AS = nasm
LD = ld

CFLAGS = -m32 -ffreestanding -fno-stack-protector -fno-pie -O2 -Wall -Wextra -c -mno-sse
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

# Driver object files
DRIVER_OBJS = drivers/mouse.o drivers/disk.o drivers/fat16.o drivers/fat32.o drivers/pci.o drivers/ahci.o drivers/net.o

# App object files
APP_OBJS = apps/calc.o apps/notepad.o apps/settings.o apps/explorer.o apps/dialog.o apps/terminal.o apps/browser.o apps/loader.o

# Main OS object files
OBJS = boot.o kernel.o $(DRIVER_OBJS) $(APP_OBJS)

# Setup object files
SETUP_OBJS = boot.o setup/setup.o drivers/mouse.o drivers/disk.o drivers/pci.o drivers/ahci.o

all: bananaos.img

buildPortable: bananaos.img

boot.o: boot.s
	$(AS) $(ASFLAGS) $< -o $@

kernel.o: kernel.c
	$(CC) $(CFLAGS) $< -o $@

drivers/%.o: drivers/%.c
	$(CC) $(CFLAGS) $< -o $@

apps/%.o: apps/%.c
	$(CC) $(CFLAGS) $< -o $@

setup/setup.o: setup/setup.c
	$(CC) $(CFLAGS) $< -o $@

bananaos.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

bananaos.img: bananaos.bin
	mkdir -p isodir/boot/grub
	cp bananaos.bin isodir/boot/bananaos.bin
	cp bg.bmp isodir/boot/bg.bmp
	echo 'set timeout=10' > isodir/boot/grub/grub.cfg
	echo 'set default=0' >> isodir/boot/grub/grub.cfg
	echo 'menuentry "BananaOS" {' >> isodir/boot/grub/grub.cfg
	echo '	multiboot /boot/bananaos.bin' >> isodir/boot/grub/grub.cfg
	echo '	module /boot/bg.bmp' >> isodir/boot/grub/grub.cfg
	echo '	boot' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg
	grub-mkrescue -o bananaos.img isodir

setup.bin: $(SETUP_OBJS) linker.ld
	$(LD) $(LDFLAGS) $(SETUP_OBJS) -o $@

buildSetup: bananaos.img setup.bin
	mkdir -p setupdir/boot/grub
	cp setup.bin setupdir/boot/setup.bin
	cp bananaos.img setupdir/boot/bananaos.img
	echo 'set timeout=5' > setupdir/boot/grub/grub.cfg
	echo 'set default=0' >> setupdir/boot/grub/grub.cfg
	echo 'menuentry "BananaOS Setup" {' >> setupdir/boot/grub/grub.cfg
	echo '	multiboot /boot/setup.bin' >> setupdir/boot/grub/grub.cfg
	echo '	module /boot/bananaos.img' >> setupdir/boot/grub/grub.cfg
	echo '	boot' >> setupdir/boot/grub/grub.cfg
	echo '}' >> setupdir/boot/grub/grub.cfg
	grub-mkrescue -o setup.iso setupdir

clean:
	rm -rf *.o bananaos.bin isodir bananaos.img setup.bin setupdir setup.iso
	rm -rf drivers/*.o apps/*.o setup/*.o
