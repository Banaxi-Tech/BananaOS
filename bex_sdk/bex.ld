ENTRY(_start)

SECTIONS
{
    /* As dictated by loader.c, BananaOS loads .bex apps at 0x2000000 */
    . = 0x2000000;

    .text : ALIGN(4K)
    {
        start.o(.text)
        *(.text)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata)
    }

    .data : ALIGN(4K)
    {
        *(.data)
    }

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }
}