/*
 *  ESP32-P4 FMP3 移植 / HWLP コプロセッサコンテキスト save/restore 実証アプリ
 *
 *  設計: arch/riscv_gcc/esp32p4/pie_hwlp_design.md (HWLP eager)．
 *  実装: arch/riscv_gcc/esp32p4/chip_asm.inc(hwlp_push/pop), target_kernel_impl.c
 *        (hardware_init_hook で 0x7F1=CLEAN + ループ CSR 初期ゼロ化)．
 */
#include "fmp_hwlp_app.h"

/*
 *  ハードウェアループ(HWLP)を count 回回し, ループ本体の addi 実行回数を返す.
 *  ツールチェーンは HWLP 命令を未サポートのため, ループ CSR を手動設定し本体は
 *  通常命令で構成する(ESP-IDF test_hwlp_routines.S と同方式):
 *    0x7C6=LOOP0_START_ADDR, 0x7C7=LOOP0_END_ADDR, 0x7C8=LOOP0_COUNT.
 *  [1f,2f] を count 回自動反復する. 本体は >=16 16bit / >=8 32bit 命令が必要なので
 *  余裕をもって 20 個の addi を置く. 戻り値の正確な倍率は問わず, 単独計測した
 *  golden と比較して save/restore の正否を判定する(意味論非依存).
 */
static uint32_t __attribute__((noinline))
hwlp_sum(uint32_t count)
{
    uint32_t result;
    Asm("la    t1, 1f          \n"
        "csrw  0x7c6, t1       \n"   /* LOOP0_START_ADDR */
        "la    t1, 2f          \n"
        "csrw  0x7c7, t1       \n"   /* LOOP0_END_ADDR   */
        "csrw  0x7c8, %[cnt]   \n"   /* LOOP0_COUNT      */
        "li    %[res], 0       \n"
        "1:                    \n"
        "addi  %[res],%[res],1 \n"   /* 1  */
        "addi  %[res],%[res],1 \n"   /* 2  */
        "addi  %[res],%[res],1 \n"   /* 3  */
        "addi  %[res],%[res],1 \n"   /* 4  */
        "addi  %[res],%[res],1 \n"   /* 5  */
        "addi  %[res],%[res],1 \n"   /* 6  */
        "addi  %[res],%[res],1 \n"   /* 7  */
        "addi  %[res],%[res],1 \n"   /* 8  */
        "addi  %[res],%[res],1 \n"   /* 9  */
        "addi  %[res],%[res],1 \n"   /* 10 */
        "addi  %[res],%[res],1 \n"   /* 11 */
        "addi  %[res],%[res],1 \n"   /* 12 */
        "addi  %[res],%[res],1 \n"   /* 13 */
        "addi  %[res],%[res],1 \n"   /* 14 */
        "addi  %[res],%[res],1 \n"   /* 15 */
        "addi  %[res],%[res],1 \n"   /* 16 */
        "addi  %[res],%[res],1 \n"   /* 17 */
        "addi  %[res],%[res],1 \n"   /* 18 */
        "addi  %[res],%[res],1 \n"   /* 19 */
        "2:                    \n"
        "addi  %[res],%[res],1 \n"   /* 20 (end) */
        : [res] "=&r" (result)
        : [cnt] "r" (count)
        : "t1", "memory");
    return result;
}

static volatile uint32_t golden_low  = 0;
static volatile uint32_t golden_high = 0;
static volatile uint32_t okA = 0, errA = 0;   /* LOW  task の PASS/FAIL */
static volatile uint32_t okB = 0, errB = 0;   /* HIGH task の PASS/FAIL */
static volatile bool_t   ready = false;

/*  LOW: 低優先度・連続. hwloop を回し続け, HIGH に hwloop 実行中にプリエンプトされる. */
void
low_task(EXINF exinf)
{
    (void) exinf;
    while (!ready) { }
    while (true) {
        uint32_t r = hwlp_sum(LOW_COUNT);
        if (r == golden_low) { okA++; } else { errA++; }
    }
}

/*  HIGH: 高優先度・周期起床. 別 count の hwloop を回し, 起床のたびに LOW を奪う. */
void
high_task(EXINF exinf)
{
    (void) exinf;
    while (!ready) { }
    while (true) {
        uint32_t r = hwlp_sum(HIGH_COUNT);
        if (r == golden_high) { okB++; } else { errB++; }
        dly_tsk(HIGH_PERIOD);
    }
}

/*  REPORT: 最高優先度. 起動時に golden を単独計測(dis_dsp でタスク切替を抑止し
 *  HWLP CSR を他タスクに汚されないクリーンな基準値を得る. 割込みは生かすので tick は
 *  進む). 以後 1s 毎に PASS/FAIL を報告する. */
void
report_task(EXINF exinf)
{
    (void) exinf;

    dis_dsp();
    golden_low  = hwlp_sum(LOW_COUNT);
    golden_high = hwlp_sum(HIGH_COUNT);
    ready = true;
    ena_dsp();

    syslog(LOG_NOTICE, "HWLP test start: golden_low=%d golden_high=%d (LOW_COUNT=%d HIGH_COUNT=%d)",
           (int_t) golden_low, (int_t) golden_high, (int_t) LOW_COUNT, (int_t) HIGH_COUNT);

    while (true) {
        dly_tsk(REPORT_PERIOD);
        syslog(LOG_NOTICE, "HWLP: okA=%d errA=%d | okB=%d errB=%d  [TTSP_RESULT: %s]",
               (int_t) okA, (int_t) errA, (int_t) okB, (int_t) errB,
               ((errA == 0U) && (errB == 0U)) ? "PASS" : "FAIL");
    }
}
