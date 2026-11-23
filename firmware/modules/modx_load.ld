/*
 * Link script of a code-load module image (app/modx.h modx_load_desc_t,
 * docs/QSPI_CONTENT.md 12.8): the Python module.
 *
 * Unlike an XIP image (modx_image.ld) this one is not position-independent:
 * it is linked at address 0 like a normal program, with static data, and the
 * core copies it into its SDRAM code area and relocates it when it binds. The
 * link keeps its relocations (-q); tools/modreloc.py turns the absolute ones
 * into the list the loader applies and checks that no other kind needs it.
 *
 *   0                 descriptor (modx_load_desc_t), code, constants
 *   ...               initialised data (.data)
 *   __modx_end        end of the bytes copied (desc.image_size)
 *   .bss              zeroed by the loader (desc.bss_size)
 */
ENTRY(modx_descriptor)

SECTIONS
{
  . = 0;
  .text : ALIGN(4)
  {
    KEEP(*(.modx_desc))
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(8);
  }
  .data : ALIGN(8)
  {
    *(.data .data.* .data.rel.ro .data.rel.ro.* .sdata .sdata.*)
    *(.got .got.* .got.plt)
    . = ALIGN(8);
    __modx_end = .;
  }
  .bss (NOLOAD) : ALIGN(8)
  {
    __modx_bss_start = .;
    *(.bss .bss.* COMMON .sbss .sbss.*)
    . = ALIGN(8);
    __modx_bss_end = .;
  }
  .init_array : { *(.init_array .init_array.* .preinit_array .fini_array .fini_array.*) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.ARM.attributes) }
}

ASSERT(modx_descriptor == 0, "code module: the descriptor must be at offset 0")
ASSERT(SIZEOF(.init_array) == 0, "code-load module: constructors are not run")
