/*
 * Link script of a code module image (app/modx.h, docs/QSPI_CONTENT.md 12).
 *
 * The image is linked at address 0 and runs wherever it is installed (the
 * QSPI window on the hardware): the code is compiled with -fPIE and hidden
 * visibility, so it reaches its own code and constants PC-relatively and
 * needs no GOT. The descriptor (.modx_desc) comes first; its function
 * offset is the only absolute address in the image (tools/modlink.py checks
 * the relocations the link keeps with -q). A module has no static RW data:
 * any .data, .bss or GOT entry stops the link.
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
    . = ALIGN(4);
    __modx_end = .;
  }
  .got : { *(.got .got.* .got.plt) }
  .data : { *(.data .data.* .data.rel.ro .data.rel.ro.* .sdata .sdata.*) }
  .bss : { *(.bss .bss.* COMMON .sbss .sbss.*) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.ARM.attributes) }
}

ASSERT(SIZEOF(.data) == 0, "code module: static RW data (.data) is not allowed; keep state in the RW block")
ASSERT(SIZEOF(.bss) == 0, "code module: static RW data (.bss) is not allowed; keep state in the RW block")
ASSERT(SIZEOF(.got) == 0, "code module: GOT entries (non-hidden or external symbols) are not allowed")
ASSERT(modx_descriptor == 0, "code module: the descriptor must be at offset 0")
