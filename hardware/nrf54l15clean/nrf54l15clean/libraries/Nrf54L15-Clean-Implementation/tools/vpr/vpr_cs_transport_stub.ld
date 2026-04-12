ENTRY(_start)

MEMORY
{
  RAM (rwx) : ORIGIN = 0x2003E600, LENGTH = 0x1800
}

__stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .text :
  {
    . = ALIGN(4);
    KEEP(*(.text.start))
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > RAM

  .data :
  {
    . = ALIGN(4);
    *(.data*)
    . = ALIGN(4);
  } > RAM

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
  } > RAM

  .stack (NOLOAD) :
  {
    . = ALIGN(16);
    . = . + 0x400;
    . = ALIGN(16);
  } > RAM

  /DISCARD/ :
  {
    *(.comment*)
    *(.note*)
    *(.eh_frame*)
    *(.riscv.attributes)
  }
}
