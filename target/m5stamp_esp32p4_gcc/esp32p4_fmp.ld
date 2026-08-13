/*
 *  ESP32-P4 / FMP3 リンカスクリプト（standalone ビルド検証用）
 *
 *  注: 本スクリプトは configure.rb/make による FMP3 単独ビルドのリンク検証用。
 *  実機イメージは方式(a)（最小 IDF アプリへ静的リンク）で生成する想定のため、
 *  最終的なメモリ配置は ESP-IDF のリンカスクリプトに従う。ここでは start.S と
 *  カーネルが要求するシンボル（toppers_start, __global_pointer$, __bss/__sbss,
 *  __data/__idata）を満たし、内蔵 SRAM(L2MEM)上にリンクできることを目的とする。
 *
 *  メモリ配置は Step1 起動ログ（L2MEM/内蔵SRAM 0x4FF0_0000 付近, 768KB）に基づく暫定値。
 */

OUTPUT_ARCH("riscv")
ENTRY(toppers_start)

MEMORY
{
    RAM (rwxa) : ORIGIN = 0x4FF00000, LENGTH = 512K
}

SECTIONS
{
    . = ORIGIN(RAM);

    .text : ALIGN(4)
    {
        *(.text.toppers_start)
        *(.text .text.*)
        *(.gnu.linkonce.t.*)
    } > RAM

    .rodata : ALIGN(4)
    {
        *(.rodata .rodata.*)
        *(.gnu.linkonce.r.*)
        *(.srodata .srodata.*)
    } > RAM

    /*  グローバルポインタ（gp相対アクセスの基準）  */
    . = ALIGN(8);
    PROVIDE(__global_pointer$ = . + 0x800);

    .data : ALIGN(8)
    {
        __data_start = .;
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        *(.sdata .sdata.*)
        *(.gnu.linkonce.s.*)
        . = ALIGN(8);
        __data_end = .;
    } > RAM AT> RAM
    /*  RAM 上に直接配置するため LMA==VMA。start.S のコピーは自己コピーで無害。  */
    __idata_start = LOADADDR(.data);
    __idata_end   = __idata_start + SIZEOF(.data);

    /*
     *  TLS(スレッドローカルストレージ)テンプレート境界シンボル。
     *  arch/riscv_gcc/esp32p4/chip_asm.inc の init_additional_regs_start_r
     *  (start_r フック, TOPPERS_SUPPORT_TLS)が参照する。実機イメージは方式(a)で
     *  ESP-IDF 側リンカスクリプトの同名シンボルを使うためここでは未使用だが，
     *  本ファイルによる単独ビルド検証リンクを通すために定義する
     *  (この検証用 fmp バイナリ自体は実機で実行されない)。
     */
    .tdata : ALIGN(8)
    {
        _thread_local_data_start = .;
        *(.tdata .tdata.*)
        _thread_local_data_end = .;
    } > RAM

    .tbss (NOLOAD) : ALIGN(8)
    {
        _thread_local_bss_start = .;
        *(.tbss .tbss.*)
        *(.tcommon)
        _thread_local_bss_end = .;
    } > RAM

    .sbss (NOLOAD) : ALIGN(8)
    {
        __sbss_start = .;
        *(.sbss .sbss.*)
        *(.scommon)
        __sbss_end = .;
    } > RAM

    .bss (NOLOAD) : ALIGN(8)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > RAM

    . = ALIGN(16);
    PROVIDE(__heap_start = .);
    PROVIDE(_end = .);
    PROVIDE(end = .);

    /DISCARD/ : { *(.eh_frame) *(.comment) *(.riscv.attributes) }
}
