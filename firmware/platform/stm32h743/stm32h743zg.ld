/*
 * STM32H743ZGT6 (1 MB flash in two 512 KB banks, 1 MB SRAM) + 32 MB SDRAM +
 * 16 MB QSPI NOR. See README.md "Memory map".
 */
ENTRY(Reset_Handler)

MEMORY
{
  ITCM    (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
  FLASH   (rx)  : ORIGIN = 0x08000000, LENGTH = 512K   /* bank 1: code */
  FLASH2  (rx)  : ORIGIN = 0x08100000, LENGTH = 512K   /* bank 2: overflow (.flash2) */
  DTCM    (rw)  : ORIGIN = 0x20000000, LENGTH = 128K   /* stack, .noinit, .dtcm */
  AXISRAM (rwx) : ORIGIN = 0x24000000, LENGTH = 512K   /* .data, .bss, heap */
  SRAM123 (rw)  : ORIGIN = 0x30000000, LENGTH = 288K   /* D2: fallback framebuffer, cold statics */
  SRAM4   (rw)  : ORIGIN = 0x38000000, LENGTH = 64K    /* D3 */
  QSPI    (rx)  : ORIGIN = 0x90000000, LENGTH = 16M    /* memory-mapped W25Q128 */
  SDRAM   (rw)  : ORIGIN = 0xD0000000, LENGTH = 32M    /* W9825G6KH on FMC bank 2 */
}

/* Main stack: the CAS recurses; hal_cas_stack_limit() lets it use all but
 * 24 KB of this (app + console frames below cas_eval, and interrupt frames). */
_stack_size = 96K;
_estack = ORIGIN(DTCM) + LENGTH(DTCM);
_min_heap_size = 64K;

SECTIONS
{
  .isr_vector ORIGIN(FLASH) :
  {
    KEEP(*(.isr_vector))
  } > FLASH

  /* Bank 2 of internal flash (0x08100000; not contiguous with bank 1 on the
   * 1 MB part). Same speed as bank 1. It holds the CAS library's code and
   * constants (object files under cas/src/, or libcas.a with CAS=lib), the help
   * manual and anything tagged FLASH2_RODATA / FLASH2_TEXT (core/sections.h).
   * This statement must come before .text/.rodata so these input sections are
   * matched here first. To move another module to bank 2, add its objects'
   * pattern below, e.g.  *app/stat_*.o(.text .text.* .rodata .rodata.*)
   * Flashed as release/calc_bank2.bin (or from the .elf/.hex). */
  .flash2 : ALIGN(4)
  {
    *(.flash2 .flash2.*)
    *cas/src/*.o(.text .text.* .rodata .rodata.*)
    *cas/ext/*.o(.text .text.* .rodata .rodata.*) /* core tier: the CAS extensions linked in */
    *libcas.a:*(.text .text.* .rodata .rodata.*)
    /* App code stays in bank 1 (bank 2 holds the CAS and the help manual); move a
     * module here only if bank 1 fills up (make release reports both banks). */
    *app/sheet_*.o(.text .text.* .rodata .rodata.*)
    /* the geometry app (the core tier links the module's code in; the QSPI tiers keep only
     * its core side, app/geo_core.c), the finance app and the 3D graph's UI (its model, mesh
     * and raster loops stay in bank 1: the model is re-evaluated every frame of an animation) */
    *modules/geometry/*.o(.text .text.* .rodata .rodata.*)
    *app/geo_core.o(.text .text.* .rodata .rodata.*)
    *app/py_core.o(.text .text.* .rodata .rodata.*) /* Python's core side (app/py_core.h) */
    *app/finance.o(.text .text.* .rodata .rodata.*)
    *app/gcell.o(.text .text.* .rodata .rodata.*) /* the home notebook's graph cells */
    *app/nbhtml.o(.text .text.* .rodata .rodata.*) /* NOTEBOOK.HTML of the CALC drive */
    *app/png.o(.text .text.* .rodata .rodata.*)    /* its PNG writer (the Python module has a copy) */
    *app/g3d_edit.o(.text .text.* .rodata .rodata.*)
    *app/g3d_screen.o(.text .text.* .rodata .rodata.*)
    *app/gr_de.o(.text .text.* .rodata .rodata.*)  /* the 2D graph's DE mode (slope fields) */
    /* USB: TinyUSB, the device glue, the drives (the interrupt handler too: bank 2 is
     * internal flash like bank 1, unaffected by QSPI writes) */
    *third_party/tinyusb/*.o(.text .text.* .rodata .rodata.*)
    */fw/platform/stm32h743/usb.o(.text .text.* .rodata .rodata.*) /* (not the bring-up image's stubs) */
    /* (core/vdrive.o: bank 1, which has more room since the notebook) */
    *core/usbdisk.o(.text .text.* .rodata .rodata.*)
    *app/usb_app.o(.text .text.* .rodata .rodata.*)
    /* content modules: install/manage UI and the store (cold code) */
    *app/modules_ui.o(.text .text.* .rodata .rodata.*)
    *core/modstore.o(.text .text.* .rodata .rodata.*)
    . = ALIGN(4);
  } > FLASH2

  .text :
  {
    . = ALIGN(4);
    *(.text .text.*)
    *(.glue_7) *(.glue_7t) *(.eh_frame)
    KEEP(*(.init)) KEEP(*(.fini))
    . = ALIGN(4);
  } > FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } > FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx* .gnu.linkonce.armexidx.*) __exidx_end = .; } > FLASH

  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > FLASH
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } > FLASH
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH

  /* Code copied to ITCM at boot (mark functions __attribute__((section(".itcm")))) */
  .itcm : ALIGN(4)
  {
    _sitcm = .;
    *(.itcm .itcm.*)
    . = ALIGN(4);
    _eitcm = .;
  } > ITCM AT> FLASH
  _siitcm = LOADADDR(.itcm);

  /* The first 8 bytes of DTCM: the RW pointer of the code module being called
   * (app/modx.h MODX_RW, modx_ctx in app/modx.c). Module images read it at this
   * fixed address (MODX_CTX_ADDR), so it must never move. Not zeroed at start-up
   * (modx_mem_init sets it). */
  .modx_ctx (NOLOAD) :
  {
    __modx_ctx_start = .;
    KEEP(*(.modx_ctx))
    . = __modx_ctx_start + 8;
  } > DTCM
  ASSERT(ADDR(.modx_ctx) == 0x20000000, "modx_ctx must be the first word of DTCM (MODX_CTX_ADDR)")

  /* Initialised data in DTCM (hot variables) */
  .dtcm_data : ALIGN(4)
  {
    _sdtcm_data = .;
    *(.dtcm_data .dtcm_data.*)
    . = ALIGN(4);
    _edtcm_data = .;
  } > DTCM AT> FLASH
  _sidtcm_data = LOADADDR(.dtcm_data);

  .dtcm_bss (NOLOAD) : ALIGN(4)
  {
    _sdtcm_bss = .;
    *(.dtcm_bss .dtcm_bss.*)
    /* USB statics (TinyUSB's endpoint and FIFO buffers, the drives' state): AXI SRAM is full.
     * Zeroed at start-up like .bss; the OTG_FS core moves data by CPU (no DMA), so DTCM is fine. */
    *third_party/tinyusb/*.o(.bss .bss.* COMMON)
    *platform/stm32h743/usb.o(.bss .bss.* COMMON)
    *core/usbdisk.o(.bss .bss.* COMMON)
    *app/usb_app.o(.bss .bss.* COMMON)
    . = ALIGN(4);
    _edtcm_bss = .;
  } > DTCM

  /* Zeroed statics in D2 SRAM (SRAM1-3, 0x30000000; next to the fallback framebuffer, .sram_d2):
   * AXI SRAM was 99 % full. The UI's larger buffers that no loop touches per pixel or per sample
   * live here; the CPU reaches them through the D-cache like AXI SRAM (default memory map:
   * write-back), no DMA uses them (the firmware has no DMA), Stop keeps D2 powered, and they do
   * not depend on the SDRAM, so the fallback mode after an SDRAM failure is unchanged. Zeroed by
   * startup.c like .bss. This statement comes before .bss: the first matching pattern wins. */
  .d2_bss (NOLOAD) : ALIGN(4)
  {
    _sd2_bss = .;
    *app/home.o(.bss .bss.* COMMON)
    *platform/stm32h743/cmds.o(.bss .bss.* COMMON)
    *app/matrix.o(.bss .bss.* COMMON)
    *app/help.o(.bss .bss.* COMMON)
    *app/solver.o(.bss .bss.* COMMON)
    *app/vars.o(.bss .bss.* COMMON)
    *app/stat_form.o(.bss .bss.* COMMON)
    *app/m2d_preview.o(.bss .bss.* COMMON)
    *app/nbdoc.o(.bss .bss.* COMMON)
    *app/apps.o(.bss .bss.* COMMON)
    *app/stat_lists.o(.bss .bss.* COMMON)
    *app/app_console.o(.bss .bss.* COMMON)
    *app/form.o(.bss .bss.* COMMON)
    *app/rpn.o(.bss .bss.* COMMON)
    *core/fat.o(.bss .bss.* COMMON)
    *app/stat_csv.o(.bss .bss.* COMMON)
    *app/finance.o(.bss .bss.* COMMON)
    *app/sheet_ui.o(.bss .bss.* COMMON)
    *app/geo_core.o(.bss .bss.* COMMON)
    *app/g3d_edit.o(.bss .bss.* COMMON)
    *app/modules_ui.o(.bss .bss.* COMMON)
    *app/nbhtml.o(.bss .bss.* COMMON)
    . = ALIGN(4);
    _ed2_bss = .;
  } > SRAM123

  /* Survives reset (crash record, bootloader magic). Never zeroed. */
  .noinit (NOLOAD) : ALIGN(4)
  {
    *(.noinit .noinit.*)
    . = ALIGN(4);
  } > DTCM

  ._stack (NOLOAD) :
  {
    . = ALIGN(8);
    _sstack = .;
    . = . + _stack_size;
    . = ALIGN(8);
  } > DTCM
  ASSERT(. <= _estack, "DTCM overflow")

  .data : ALIGN(4)
  {
    _sdata = .;
    *(.data .data.*)
    . = ALIGN(4);
    _edata = .;
  } > AXISRAM AT> FLASH
  _sidata = LOADADDR(.data);

  .bss (NOLOAD) : ALIGN(4)
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } > AXISRAM

  /* Fallback app/CAS arena, used only if the SDRAM fails its boot check (so
   * the calculator and console still work for debugging). Before the heap so
   * sbrk can never grow into it. */
  .axi_fallback (NOLOAD) : ALIGN(32)
  {
    *(.axi_fallback .axi_fallback.*)
  } > AXISRAM

  ._heap (NOLOAD) : ALIGN(8)
  {
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _min_heap_size;
  } > AXISRAM
  _heap_limit = ORIGIN(AXISRAM) + LENGTH(AXISRAM);

  .sram_d2 (NOLOAD) : ALIGN(32) { *(.sram_d2 .sram_d2.*) } > SRAM123
  .sram_d3 (NOLOAD) : ALIGN(32) { *(.sram_d3 .sram_d3.*) } > SRAM4

  /* QSPI (memory-mapped). Unused: content lives in relocatable modules in the
   * QSPI module store (core/modstore.h), not at link-time addresses. Kept so
   * fixed-address experiments can be flashed via OpenOCD's stmqspi bank. */
  .qspi : ALIGN(4) { *(.qspi .qspi.*) } > QSPI

  /* SDRAM: never initialised by startup (SDRAM is set up in hal_init). The
   * framebuffer is first so that its address is fixed at 0xD0000000. */
  .sdram_fb (NOLOAD) : ALIGN(32)
  {
    __sdram_fb_start = .;
    KEEP(*(.sdram_fb))
    __sdram_fb_end = .;
  } > SDRAM
  ASSERT(__sdram_fb_start == 0xD0000000, "framebuffer must be at 0xD0000000")

  .sdram (NOLOAD) : ALIGN(32)
  {
    *(.sdram .sdram.*)
    . = ALIGN(32);
    __sdram_arena_start = .;
  } > SDRAM
  __sdram_arena_end = ORIGIN(SDRAM) + LENGTH(SDRAM);

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
