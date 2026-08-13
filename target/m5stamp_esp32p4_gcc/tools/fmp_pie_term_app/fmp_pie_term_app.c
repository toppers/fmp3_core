/*
 *  ESP32-P4 FMP3 移植 / lazy PIE タスク終了時の所有権解放(release_context)正当性 実証アプリ
 *
 *  設計: arch/riscv_gcc/esp32p4/pie_hwlp_design.md §9.2．
 *
 *  自己チェーン方式: TERM→(act WORKER)→ext_tsk→WORKER→(act TERM)→ext_tsk→… の ping-pong を
 *  NOISE が割り込んで PIE オーナを奪う．「PIE オーナ TERM が ext_tsk した直後に WORKER が
 *  start_r 起動する」を決定的に再現する(同 prio で TERM 実行中に WORKER を ready 化し，TERM
 *  終了の次に WORKER が走る)．REPORT は駆動せず周期集計のみ．
 */
#include "fmp_pie_term_app.h"
#include "kernel_cfg.h"				/* act_tsk 対象の TERM_TASK/WORKER_TASK 等の自動割付 ID */
#include <stdint.h>

/*
 *  非分割の PIE 累算(golden 計測・TERM・NOISE 用)．q0=0 から q1={seed×4} を iters 回足す．
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
 *  yield を跨ぐ PIE 累算: 前半 half 回 → **自タスクを dly_tsk(YIELD)で短時間ブロック** → 後半 rem 回．
 *  q0(累算器)/q1(seed)は分割を跨いで物理レジスタに live．dly 中に NOISE(prio10)が PIE オーナを
 *  奪うので，lazy が WORKER の q0/q1 を WORKER 自身の保存域へ正しく flush→復帰で復元しない限り，
 *  後半の累算が壊れ結果が golden と食い違う．C 部(dly_tsk)は PIE を使わない．
 */
static uint32_t __attribute__((noinline))
pie_compute_yield(uint32_t seed, uint32_t iters)
{
    uint32_t in[4]   __attribute__((aligned(16)));
    uint32_t zero[4] __attribute__((aligned(16))) = { 0, 0, 0, 0 };
    uint32_t out[4]  __attribute__((aligned(16)));
    uint32_t half = iters / 2u;
    uint32_t rem  = iters - half;

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

    /*  分割点: 短時間ブロックして NOISE(prio10)に PIE オーナを奪わせる  */
    (void) dly_tsk(YIELD_TIME);

    /*  後半: rem 回 → q0 を out へ．復帰後の最初の esp.vadd で q0/q1 を非オーナトラップ復元する  */
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
static volatile uint32_t round_cnt = 0;
static volatile bool_t   ready = false;

/*
 *  PIE オーナになって即終了するタスク(チェーンの一方)．
 *  PIE 累算でこの PE のオーナ(pie_owner[PRC1]=TERM, CSR_PIE_STATE_REG=ON)になり，同 prio の WORKER を
 *  ready 化してから ext_tsk．release_context が無いと所有権が残り，次に start_r 起動する
 *  WORKER が CSR_PIE_STATE_REG=ON を継承して幽霊オーナ化する．
 */
void
term_task(EXINF exinf)
{
    (void) exinf;
    (void) pie_compute(TERM_SEED, TERM_ITERS);   /* この PE の PIE オーナになる(CSR_PIE_STATE_REG=ON) */
    (void) act_tsk(WORKER_TASK);                  /* WORKER を ready 化(TERM 実行中→queue 末尾) */
    (void) ext_tsk();                             /* PIE オーナのまま終了→次に WORKER が start_r */
}

/*
 *  TERM 終了の直後に start_r で起動する新規タスク(チェーンの他方)．
 *  yield を跨ぐ PIE 累算を 1 回行い golden 照合．同 prio の TERM を ready 化してから ext_tsk
 *  (チェーン継続．次ラウンドも TERM→WORKER で WORKER は fresh start_r 起動)．
 */
void
worker_task(EXINF exinf)
{
    uint32_t r;
    (void) exinf;

    r = pie_compute_yield(WORKER_SEED, WORKER_ITERS);
    if (r == golden_w) { okW++; } else { errW++; }
    round_cnt++;
    (void) act_tsk(TERM_TASK);                    /* 次ラウンドの TERM を ready 化 */
    (void) ext_tsk();
}

/*
 *  WORKER の yield 中に連続 PIE してオーナを奪う(常時 ready で free-run)．
 */
void
noise_task(EXINF exinf)
{
    (void) exinf;
    while (!ready) { }
    while (true) {
        (void) pie_compute(NOISE_SEED, NOISE_ITERS);
    }
}

/*
 *  起動時に golden を単独計測し，チェーンを 1 回 kick(act_tsk(TERM))してから周期集計する．
 *  駆動はチェーン自身が行い，本タスクは集計レポートのみ(最上位 prio で確実に flush)．
 */
void
report_task(EXINF exinf)
{
    (void) exinf;

    /*  起動時 settle: boot banner を logtask が吐き切ってから重い PIE チェーンを始める  */
    dly_tsk(100000);

    dis_dsp();                       /* PRC1 上で単独に golden を計測 */
    golden_w = pie_compute(WORKER_SEED, WORKER_ITERS);
    ready = true;
    ena_dsp();

    syslog(LOG_NOTICE, "PIEterm start: golden_w=%d (WORKER %d*%d, term->ext_tsk->worker start_r)",
           (int_t) golden_w, (int_t) WORKER_SEED, (int_t) WORKER_ITERS);

    (void) act_tsk(TERM_TASK);       /* チェーン開始 */

    while (true) {
        (void) dly_tsk(REPORT_PERIOD);
        syslog(LOG_NOTICE, "PIEterm: okW=%d errW=%d rounds=%d  [TTSP_RESULT: %s]",
               (int_t) okW, (int_t) errW, (int_t) round_cnt,
               (errW == 0U) ? "PASS" : "FAIL");
    }
}
