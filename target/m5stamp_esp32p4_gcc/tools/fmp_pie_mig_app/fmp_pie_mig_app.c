/*
 *  ESP32-P4 FMP3 移植 / lazy PIE マイグレーション(mig_tsk)正当性 実証アプリ
 *
 *  設計: arch/riscv_gcc/esp32p4/pie_hwlp_design.md §9.2 / §9.2.1．
 */
#include "fmp_pie_mig_app.h"
#include <stdint.h>

/*
 *  非分割の PIE 累算(golden 計測・noise 用)．q0=0 から q1={seed×4} を iters 回足す．
 */
static uint32_t __attribute__((noinline))
pie_compute(uint32_t seed, uint32_t iters)
{
    uint32_t in[4]   __attribute__((aligned(16)));
    uint32_t zero[4] __attribute__((aligned(16))) = { 0, 0, 0, 0 };
    uint32_t out[4]  __attribute__((aligned(16)));
    in[0] = seed; in[1] = seed; in[2] = seed; in[3] = seed;

    Asm(
        "mv   a2, %[zero]          \n"
        "esp.vld.128.ip  q0, a2, 0 \n"
        "mv   a2, %[in]           \n"
        "esp.vld.128.ip  q1, a2, 0 \n"
        "mv   a3, %[iters]        \n"
        "1:                        \n"
        "esp.vadd.u32    q0, q0, q1 \n"
        "addi a3, a3, -1           \n"
        "bnez a3, 1b               \n"
        "mv   a2, %[out]          \n"
        "esp.vst.128.ip  q0, a2, 0 \n"
        :
        : [in] "r" (in), [zero] "r" (zero), [out] "r" (out), [iters] "r" (iters)
        : "a2", "a3", "memory");
    return out[0];
}

/*
 *  移行を跨ぐ PIE 累算: 前半 half 回 → **自タスクを他 PE へ移行(pattern A)** → 後半 rem 回．
 *  q0(累算器)/q1(seed)は分割を跨いで物理レジスタに live．移行で lazy が q0/q1 を保存域へ
 *  flush し，移行先 PE の後半 esp.vadd で「非オーナ→トラップ復元」されない限り結果が壊れる．
 *  C 部(get_pid/mig_tsk)は PIE を使わないので q0/q1 は保たれる(GCC は q レジスタを管理外)．
 */
static uint32_t __attribute__((noinline))
pie_compute_mig(uint32_t seed, uint32_t iters)
{
    uint32_t in[4]   __attribute__((aligned(16)));
    uint32_t zero[4] __attribute__((aligned(16))) = { 0, 0, 0, 0 };
    uint32_t out[4]  __attribute__((aligned(16)));
    uint32_t half = iters / 2u;
    uint32_t rem  = iters - half;
    ID cur = PRC1;

    in[0] = seed; in[1] = seed; in[2] = seed; in[3] = seed;

    /*  前半: q0=0, q1=seed, half 回  */
    Asm(
        "mv   a2, %[zero]          \n"
        "esp.vld.128.ip  q0, a2, 0 \n"
        "mv   a2, %[in]           \n"
        "esp.vld.128.ip  q1, a2, 0 \n"
        "mv   a3, %[half]         \n"
        "1:                        \n"
        "esp.vadd.u32    q0, q0, q1 \n"
        "addi a3, a3, -1           \n"
        "bnez a3, 1b               \n"
        :
        : [in] "r" (in), [zero] "r" (zero), [half] "r" (half)
        : "a2", "a3", "memory");

    /*  分割点: 自タスクを「現在と異なる PE」へ移行(pattern A)  */
    (void) get_pid(&cur);
    (void) mig_tsk(TSK_SELF, (cur == PRC1) ? PRC2 : PRC1);

    /*  後半: rem 回 → q0 を out へ．移行先で最初の esp.vadd が q0/q1 をトラップ復元する  */
    Asm(
        "mv   a3, %[rem]          \n"
        "1:                        \n"
        "esp.vadd.u32    q0, q0, q1 \n"
        "addi a3, a3, -1           \n"
        "bnez a3, 1b               \n"
        "mv   a2, %[out]          \n"
        "esp.vst.128.ip  q0, a2, 0 \n"
        :
        : [out] "r" (out), [rem] "r" (rem)
        : "a2", "a3", "memory");

    return out[0];
}

static volatile uint32_t golden_w = 0;
static volatile uint32_t okW = 0, errW = 0;
static volatile uint32_t mig_cnt = 0;
static volatile bool_t   ready = false;

/*  各 PE で連続 PIE してオーナを奪い合う(WORKER 到着時の非オーナトラップを誘発)  */
void
noise_task(EXINF exinf)
{
    uint32_t seed = (uint32_t) exinf;
    while (!ready) { }
    while (true) {
        (void) pie_compute(seed, NOISE_ITERS);
    }
}

/*  移行を跨ぐ PIE 累算を回し golden 照合．1回ごとに 1 回 mig_tsk(自タスク移行)する  */
void
worker_task(EXINF exinf)
{
    (void) exinf;
    while (!ready) { }
    while (true) {
        uint32_t r = pie_compute_mig(WORKER_SEED, WORKER_ITERS);
        mig_cnt++;
        if (r == golden_w) { okW++; } else { errW++; }
    }
}

void
report_task(EXINF exinf)
{
    (void) exinf;

    dis_dsp();                       /* PRC1 上で単独に golden を計測 */
    golden_w = pie_compute(WORKER_SEED, WORKER_ITERS);
    ready = true;
    ena_dsp();

    syslog(LOG_NOTICE, "PIEmig start: golden_w=%d (WORKER %d*%d, split-migrate)",
           (int_t) golden_w, (int_t) WORKER_SEED, (int_t) WORKER_ITERS);

    while (true) {
        dly_tsk(REPORT_PERIOD);
        syslog(LOG_NOTICE, "PIEmig: okW=%d errW=%d mig=%d  [TTSP_RESULT: %s]",
               (int_t) okW, (int_t) errW, (int_t) mig_cnt,
               (errW == 0U) ? "PASS" : "FAIL");
    }
}
