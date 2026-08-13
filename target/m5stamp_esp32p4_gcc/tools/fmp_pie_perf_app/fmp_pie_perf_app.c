/*
 *  ESP32-P4 FMP3 / lazy PIE 非オーナ初回ベクタ命令の例外処理時間 計測アプリ
 *
 *  設計: arch/riscv_gcc/esp32p4/pie_hwlp_design.md §9.2．計測方式はヘッダ参照．
 */
#include "fmp_pie_perf_app.h"
#include "kernel_cfg.h"				/* wup_tsk 対象 STEAL_TASK の自動割付 ID */
#include <stdint.h>

/*  mcycle(CSR 0xB00) 下位32bit 直読み  */
static inline uint32_t rd_mcycle(void)
{
    uint32_t c;
    Asm("csrr %0, 0xB00" : "=r"(c) :: "memory");
    return c;
}

static volatile uint32_t min_trap  = 0xFFFFFFFFu;  /* 非オーナ esp.vadd(トラップ往復含む) */
static volatile uint32_t min_instr = 0xFFFFFFFFu;  /* オーナ esp.vadd(トラップなし) */
static volatile bool_t   done = false;

/*
 *  オーナを奪う側(prio9)．MEAS の wup で起き，esp.vadd でオーナになり slp で戻る．
 */
void
steal_task(EXINF exinf)
{
    uint32_t in[4] __attribute__((aligned(16))) = { 3u, 3u, 3u, 3u };
    (void) exinf;

    Asm("mv a2, %0                \n"
        "esp.vld.128.ip  q0, a2, 0 \n"
        "esp.vld.128.ip  q1, a2, 0 \n"
        :: "r"(in) : "a2", "memory");   /* q0/q1 をロード(初回はトラップしてオーナ取得) */

    while (true) {
        (void) slp_tsk();                       /* MEAS の wup を待つ */
        Asm("esp.vadd.u32  q0, q0, q1" ::: "memory");  /* 非オーナ→トラップ→オーナ取得 */
    }
}

/*
 *  計測側(prio10)．STEAL にオーナを奪わせて非オーナ化→自分の esp.vadd トラップ往復を測る．
 */
void
meas_task(EXINF exinf)
{
    uint32_t in[4] __attribute__((aligned(16))) = { SEED, SEED, SEED, SEED };
    uint32_t t0, t1, t2, t3, d;
    uint_t i;
    (void) exinf;

    dly_tsk(100000);                 /* 起動 settle(boot banner 吐き切り) */

    Asm("mv a2, %0                \n"
        "esp.vld.128.ip  q0, a2, 0 \n"
        "esp.vld.128.ip  q1, a2, 0 \n"
        :: "r"(in) : "a2", "memory");   /* warm up: オーナになる */

    for (i = 0; i < NMEAS; i++) {
        (void) wup_tsk(STEAL_TASK);     /* STEAL がプリエンプト→オーナ奪取→slp で戻る．MEAS は非オーナ */

        t0 = rd_mcycle();
        Asm("esp.vadd.u32  q0, q0, q1" ::: "memory");   /* 非オーナ初回→illegal トラップ往復 */
        t1 = rd_mcycle();

        t2 = rd_mcycle();
        Asm("esp.vadd.u32  q0, q0, q1" ::: "memory");   /* 今度はオーナ→トラップなし */
        t3 = rd_mcycle();

        d = t1 - t0; if (d < min_trap)  { min_trap  = d; }
        d = t3 - t2; if (d < min_instr) { min_instr = d; }
    }
    done = true;

    while (true) {
        syslog(LOG_NOTICE,
            "PIEexc: trap+instr=%d cyc / instr_only=%d cyc / **trap_cost=%d cyc** (min of %d)  [TTSP_RESULT: DONE]",
            (int_t) min_trap, (int_t) min_instr, (int_t)(min_trap - min_instr), (int_t) NMEAS);
        dly_tsk(1000000);
    }
}
