/*
 *  ESP32-P4 FMP3 移植 / lazy PIE マイグレーション pattern B/C/D 実証アプリ
 *
 *  設計: arch/riscv_gcc/esp32p4/pie_hwlp_design.md §9.2 / §9.2.1．
 */
#include "fmp_pie_mig2_app.h"
#include <stdint.h>

/*  非分割 PIE 累算(golden 計測・noise 用)  */
static uint32_t __attribute__((noinline))
pie_compute(uint32_t seed, uint32_t iters)
{
    uint32_t in[4]   __attribute__((aligned(16)));
    uint32_t zero[4] __attribute__((aligned(16))) = { 0, 0, 0, 0 };
    uint32_t out[4]  __attribute__((aligned(16)));
    in[0] = seed; in[1] = seed; in[2] = seed; in[3] = seed;

    Asm("mv a2,%[zero]\n esp.vld.128.ip q0,a2,0\n mv a2,%[in]\n esp.vld.128.ip q1,a2,0\n"
        "mv a3,%[iters]\n 1: esp.vadd.u32 q0,q0,q1\n addi a3,a3,-1\n bnez a3,1b\n"
        "mv a2,%[out]\n esp.vst.128.ip q0,a2,0\n"
        : : [in]"r"(in),[zero]"r"(zero),[out]"r"(out),[iters]"r"(iters) : "a2","a3","memory");
    return out[0];
}

/*  同期/制御用の共有状態  */
static volatile ID       worker_tid = 0;
static volatile int      worker_mode = 0;    /* 0=B,1=C,2=D */
static volatile int      worker_at_sync = 0;
static volatile int      migrated_flag = 0;
static volatile uint32_t golden_w = 0;
static volatile uint32_t okW = 0, errW = 0;
static volatile uint32_t cntB = 0, cntC = 0, cntD = 0;
static volatile bool_t   ready = false;

/*
 *  移行(他タスク移行)を跨ぐ PIE 累算: 前半 half 回 → 同期点で mode に応じた状態へ →
 *  (CONTROLLER が PRC2 へ移行) → 後半 rem 回(PRC2 で非オーナ→トラップ復元)．
 *  q0/q1 は分割を跨いで物理レジスタに live．
 */
static uint32_t __attribute__((noinline))
worker_compute(uint32_t seed, uint32_t iters, int mode)
{
    uint32_t in[4]   __attribute__((aligned(16)));
    uint32_t zero[4] __attribute__((aligned(16))) = { 0, 0, 0, 0 };
    uint32_t out[4]  __attribute__((aligned(16)));
    uint32_t half = iters / 2u;
    uint32_t rem  = iters - half;
    in[0] = seed; in[1] = seed; in[2] = seed; in[3] = seed;

    /*  前半: q0=0, q1=seed, half 回(WORKER は PIE オーナになる)  */
    Asm("mv a2,%[zero]\n esp.vld.128.ip q0,a2,0\n mv a2,%[in]\n esp.vld.128.ip q1,a2,0\n"
        "mv a3,%[half]\n 1: esp.vadd.u32 q0,q0,q1\n addi a3,a3,-1\n bnez a3,1b\n"
        : : [in]"r"(in),[zero]"r"(zero),[half]"r"(half) : "a2","a3","memory");

    /*  同期点: mode に応じた状態になり，CONTROLLER の移行を待つ  */
    migrated_flag = 0;
    worker_mode = mode;
    worker_at_sync = 1;
    if (mode == 0) {
        while (migrated_flag == 0) { }       /* B: RUNNABLE のまま busy-wait */
    }
    else if (mode == 1) {
        (void) slp_tsk();                    /* C: 無タイムアウト待ち */
    }
    else {
        (void) tslp_tsk(10000000);           /* D: タイムアウト付き待ち(10s 安全網) */
    }
    /*  ここに来た時点で PRC2 へ移行済み  */

    /*  後半: rem 回 → q0 を out へ(PRC2 で最初の esp.vadd が q0/q1 をトラップ復元)  */
    Asm("mv a3,%[rem]\n 1: esp.vadd.u32 q0,q0,q1\n addi a3,a3,-1\n bnez a3,1b\n"
        "mv a2,%[out]\n esp.vst.128.ip q0,a2,0\n"
        : : [out]"r"(out),[rem]"r"(rem) : "a2","a3","memory");
    return out[0];
}

void
worker_task(EXINF exinf)
{
    ID  tid = 0;
    int mode = 0;
    (void) exinf;
    (void) get_tid(&tid);
    worker_tid = tid;                /* CONTROLLER へ自 ID を公開 */
    while (!ready) { }
    while (true) {
        uint32_t r = worker_compute(WORKER_SEED, WORKER_ITERS, mode);
        if (r == golden_w) { okW++; } else { errW++; }
        (void) mig_tsk(TSK_SELF, PRC1);  /* PRC1 へ戻る(pattern A 再利用) */
        mode = (mode + 1) % 3;
    }
}

/*  CONTROLLER(PRC1, 高優先): WORKER の状態を ref_tsk で判別し PRC2 へ移行  */
void
controller_task(EXINF exinf)
{
    T_RTSK rtsk;
    ID     myprc = PRC1;
    (void) exinf;
    /* busy-wait で低優先の WORKER を飢餓させないよう dly_tsk で yield する */
    while (!ready || worker_tid == 0) { dly_tsk(1000); }
    (void) get_pid(&myprc);
    while (true) {
        dly_tsk(1000);                       /* 1ms ポーリング */
        if (worker_at_sync == 0) { continue; }
        if (ref_tsk(worker_tid, &rtsk) != E_OK) { continue; }
        if (rtsk.prcid != myprc) { continue; }   /* WORKER が自 PE に居ない */

        if (worker_mode == 0) {
            /*  B: WORKER が RUNNABLE のとき移行(自 PE 他 RUNNABLE タスク)  */
            if (rtsk.tskstat == TTS_RDY) {
                worker_at_sync = 0;
                if (mig_tsk(worker_tid, PRC2) == E_OK) { migrated_flag = 1; cntB++; }
            }
        }
        else {
            /*  C/D: WORKER が待ち状態のとき移行 → 起床  */
            if (rtsk.tskstat == TTS_WAI) {
                worker_at_sync = 0;
                if (mig_tsk(worker_tid, PRC2) == E_OK) {
                    (void) wup_tsk(worker_tid);
                    if (worker_mode == 1) { cntC++; } else { cntD++; }
                }
            }
        }
    }
}

/*  PRC2 で連続 PIE してオーナを奪い，WORKER 到着時の非オーナトラップを誘発  */
void
noise_task(EXINF exinf)
{
    uint32_t seed = (uint32_t) exinf;
    while (!ready) { }
    while (true) {
        (void) pie_compute(seed, NOISE_ITERS);
    }
}

void
report_task(EXINF exinf)
{
    (void) exinf;

    dis_dsp();
    golden_w = pie_compute(WORKER_SEED, WORKER_ITERS);
    ready = true;
    ena_dsp();

    syslog(LOG_NOTICE, "PIEmig2 start: golden_w=%d (WORKER %d*%d, ctrl-migrate B/C/D)",
           (int_t) golden_w, (int_t) WORKER_SEED, (int_t) WORKER_ITERS);

    while (true) {
        dly_tsk(REPORT_PERIOD);
        syslog(LOG_NOTICE, "PIEmig2: okW=%d errW=%d  B=%d C=%d D=%d",
               (int_t) okW, (int_t) errW, (int_t) cntB, (int_t) cntC, (int_t) cntD);
        syslog(LOG_NOTICE, "PIEmig2 result: [TTSP_RESULT: %s] (B/C/D all>0 = 全分岐通過)",
               (errW == 0U && cntB > 0U && cntC > 0U && cntD > 0U) ? "PASS" : "FAIL");
    }
}
