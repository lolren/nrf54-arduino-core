/*
 * Linker Script for nRF54L15 without VPR reservations.
 *
 * Memory Layout:
 * - RRAM:  1524 KB total (0x00000000 - 0x0017D000)
 *   - 4 KB reserved at the top for bond/preferences/EEPROM persistence
 *   - 1520 KB usable for code/rodata/data loads
 * - RAM:   256 KB total (0x20000000 - 0x20040000)
 *   - top 512 B reserved for the existing saved-context window
 *   - no CPUAPP<->VPR transport windows are reserved
 *   - 255.5 KB usable for CPUAPP runtime sections when VPR support is disabled
 *
 * Licensed under the Apache License 2.0
 */

/* Entry Point */
ENTRY(Reset_Handler)

/* Memory Regions */
MEMORY
{
    FLASH (rx)      : ORIGIN = 0x00000000, LENGTH = 0x17C000
    FLASH_BOND (r)  : ORIGIN = 0x0017C000, LENGTH = 0x1000
    RAM (rwx)       : ORIGIN = 0x20000000, LENGTH = 0x3FE00
}

/* Stack size. Heap is not given a fixed size; it fills all RAM between
 * the end of static sections (.bss, .noinit) and the bottom of the stack. */
__stack_size__ = 4K;

/* Section Definitions */
SECTIONS
{
    /*
     * The startup code goes into FLASH
     */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))     /* Startup code */
        . = ALIGN(4);
    } >FLASH

    /*
     * The program code and other data goes into FLASH
     */
    .text :
    {
        . = ALIGN(4);
        *(.text)                 /* .text sections (code) */
        *(.text*)                /* .text* sections (code) */
        *(.glue_7)               /* glue arm to thumb code */
        *(.glue_7t)              /* glue thumb to arm code */
        *(.eh_frame)             /* exception handling */

        KEEP (*(.init))
        KEEP (*(.fini))

        . = ALIGN(4);
        _etext = .;              /* End of code section */
    } >FLASH

    /*
     * C/C++ runtime constructor/destructor tables.
     * __libc_init_array() consumes these symbols at startup.
     */
    .preinit_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
        . = ALIGN(4);
    } >FLASH

    .init_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
        . = ALIGN(4);
    } >FLASH

    .fini_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
        . = ALIGN(4);
    } >FLASH

    /*
     * Read-only data (constants, strings, etc.)
     */
    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)               /* .rodata sections (constants, strings, etc.) */
        *(.rodata*)              /* .rodata* sections */
        . = ALIGN(4);
    } >FLASH

    /*
     * Reserved non-volatile storage area for bond persistence.
     * This region is excluded from normal code/data placement.
     */
    .bond_storage (NOLOAD) :
    {
        . = ALIGN(4);
        _sbond_storage = .;
        KEEP(*(.bond_storage))
        KEEP(*(.bond_storage*))
        . = ALIGN(4);
        _ebond_storage = .;
    } >FLASH_BOND

    /*
     * Reserved non-volatile storage area for user Preferences key/value data.
     * Shares the dedicated FLASH_BOND page with bond storage.
     */
    .prefs_storage (NOLOAD) :
    {
        . = ALIGN(4);
        _sprefs_storage = .;
        KEEP(*(.prefs_storage))
        KEEP(*(.prefs_storage*))
        . = ALIGN(4);
        _eprefs_storage = .;
    } >FLASH_BOND

    /*
     * Reserved non-volatile storage area for EEPROM emulation.
     * Shares the dedicated FLASH_BOND page with bond/prefs storage.
     */
    .eeprom_storage (NOLOAD) :
    {
        . = ALIGN(4);
        _seeprom_storage = .;
        KEEP(*(.eeprom_storage))
        KEEP(*(.eeprom_storage*))
        . = ALIGN(4);
        _eeeprom_storage = .;
    } >FLASH_BOND

    /*
     * ARM sections
     */
    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } >FLASH

    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >FLASH

    /*
     * Initialized data sections into RAM, load from FLASH
     */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;              /* Start of data section in RAM */
        *(.data)                 /* .data sections */
        *(.data*)                /* .data* sections */

        . = ALIGN(4);
        _edata = .;              /* End of data section in RAM */
    } >RAM AT>FLASH

    _sidata = LOADADDR(.data);   /* Start of data section in FLASH */

    /*
     * Uninitialized data section into RAM
     */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;               /* Start of bss section */
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)

        . = ALIGN(4);
        _ebss = .;               /* End of bss section */
        __bss_end__ = _ebss;
    } >RAM

    /*
     * Non-initialized RAM section retained across reset paths where RAM power
     * is maintained. Startup code does not touch this section.
     */
    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        _snoinit = .;
        *(.noinit)
        *(.noinit*)
        . = ALIGN(4);
        _enoinit = .;
    } >RAM

    /*
     * Heap: grows upward from end of static sections to the bottom of the stack.
     * Stack: the startup code sets SP to __stack_start__ (top of RAM); the
     * linker symbol __stack_end__ marks the lowest safe stack address.
     */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE ( __heap_start__ = . );
        PROVIDE ( end = . );
        PROVIDE ( _end = . );
        . = ORIGIN(RAM) + LENGTH(RAM) - __stack_size__;
        PROVIDE ( __heap_end__ = . );
        . = ALIGN(8);
    } >RAM
    /*
     * Symbols for startup code
     */
    __stack_start__ = ORIGIN(RAM) + LENGTH(RAM);
    __stack_end__ = __stack_start__ - __stack_size__;


    . = ALIGN(4);
}
