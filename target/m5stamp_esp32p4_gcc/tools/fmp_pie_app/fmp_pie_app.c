/*
 *  ESP32-P4 FMP3 移植 / PIE コプロセッサコンテキスト save/restore 実証アプリ
 *
 *  実装: arch/riscv_gcc/esp32p4/chip_asm.inc(pie_push/pie_pop, eager 方式), Makefile.chip
 *        (-march=...xesppie, -DTOPPERS_SUPPORT_PIE), target_kernel_impl.c(hardware_init_hook で
 *        0x7F2 enable)．設計: arch/riscv_gcc/esp32p4/pie_hwlp_design.md．
 */
#include "fmp_pie_app.h"
#include <stdint.h>

/*
 *  PIE ベクタ累算: q0(累算器)=0 から開始し，q1={seed,seed,seed,seed} を iters 回
 *  esp.vadd.u32 で足し込む．q0 は反復中ずっと live なので，途中でプリエンプトされると
 *  HIGH 側 PIE 演算に q0/q1 を壊される(eager save/restore が無ければ)．
 *  結果(out[0]=seed*iters)を返す．倍率は問わず単独計測 golden と比較する．
 *
 *  esp.* の base レジスタは x8-x15 のみ可のため，asm 内で `mv a2, <ptr>` 経由で
 *  a2 を base に使う(コンパイラ割当の任意 r を a2 へ移す)．q レジスタは GCC 管理外．
 */
static uint32_t __attribute__((noinline))
pie_compute(uint32_t seed, uint32_t iters)
{
    uint32_t in[4]   __attribute__((aligned(16)));
    uint32_t zero[4] __attribute__((aligned(16))) = { 0, 0, 0, 0 };
    uint32_t out[4]  __attribute__((aligned(16)));
    in[0] = seed; in[1] = seed; in[2] = seed; in[3] = seed;

    Asm(
        "mv   a2, %[zero]              \n"
        "esp.vld.128.ip  q0, a2, 0     \n"   /* q0 = 0(累算器) */
        "mv   a2, %[in]               \n"
        "esp.vld.128.ip  q1, a2, 0     \n"   /* q1 = {seed,seed,seed,seed} */
        "mv   a3, %[iters]            \n"
        "1:                            \n"
        "esp.vadd.u32    q0, q0, q1    \n"   /* q0 += q1 (この間 q0/q1 が live) */
        "addi a3, a3, -1               \n"
        "bnez a3, 1b                   \n"
        "mv   a2, %[out]              \n"
        "esp.vst.128.ip  q0, a2, 0     \n"   /* out = q0 */
        :
        : [in] "r" (in), [zero] "r" (zero), [out] "r" (out), [iters] "r" (iters)
        : "a2", "a3", "memory");

    return out[0];
}

static volatile uint32_t golden_low  = 0;
static volatile uint32_t golden_high = 0;
static volatile uint32_t okA = 0, errA = 0;
static volatile uint32_t okB = 0, errB = 0;
static volatile bool_t   ready = false;

void
low_task(EXINF exinf)
{
    (void) exinf;
    while (!ready) { }
    while (true) {
        uint32_t r = pie_compute(LOW_SEED, LOW_ITERS);
        if (r == golden_low) { okA++; } else { errA++; }
    }
}

void
high_task(EXINF exinf)
{
    (void) exinf;
    while (!ready) { }
    while (true) {
        uint32_t r = pie_compute(HIGH_SEED, HIGH_ITERS);
        if (r == golden_high) { okB++; } else { errB++; }
        dly_tsk(HIGH_PERIOD);
    }
}

void
report_task(EXINF exinf)
{
    (void) exinf;

    dis_dsp();                       /* 単独でクリーンに golden を計測(タスク切替抑止) */
    golden_low  = pie_compute(LOW_SEED,  LOW_ITERS);
    golden_high = pie_compute(HIGH_SEED, HIGH_ITERS);
    ready = true;
    ena_dsp();

    syslog(LOG_NOTICE, "PIE test start: golden_low=%d golden_high=%d (LOW %d*%d / HIGH %d*%d)",
           (int_t) golden_low, (int_t) golden_high,
           (int_t) LOW_SEED, (int_t) LOW_ITERS, (int_t) HIGH_SEED, (int_t) HIGH_ITERS);

    while (true) {
        dly_tsk(REPORT_PERIOD);
        syslog(LOG_NOTICE, "PIE: okA=%d errA=%d | okB=%d errB=%d  [TTSP_RESULT: %s]",
               (int_t) okA, (int_t) errA, (int_t) okB, (int_t) errB,
               ((errA == 0U) && (errB == 0U)) ? "PASS" : "FAIL");
    }
}
