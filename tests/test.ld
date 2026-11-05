ENTRY(test_main)
MEMORY { FLASH (rx) : ORIGIN = 0x10000, LENGTH = 512K
         RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K }
SECTIONS {
 .text : { *(.text*) *(.rodata*) } > FLASH
 .ARM.exidx : { *(.ARM.exidx*) } > FLASH
 .data : { *(.data*) } > RAM AT> FLASH
 .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
}
