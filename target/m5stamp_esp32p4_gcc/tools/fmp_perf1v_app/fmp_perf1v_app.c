/*
 *  ESP32-P4 FMP3 / perf1(タスク切換え時間) の「ベクタ命令使用」改造版
 *
 *  test/perf1.c をベースに，各タスクが切換え直後(計測区間内)に PIE ベクタ命令を1回使う．
 *  計測法は perf1 と同一(histogram, init_hist/begin_measure/end_measure/print_hist)．
 *  設計: arch/riscv_gcc/esp32p4/pie_hwlp_design.md §9.2．
 */
#include "fmp_perf1v_app.h"
#include "syssvc/histogram.h"
#include "kernel_cfg.h"
#include <stdint.h>

/*  PIE: q0/q1 をロードしてオーナになる(warm up)／ベクタ命令を1回使う  */
#ifdef TOPPERS_SUPPORT_PIE
static void pie_warmup(void)
{
    uint32_t v[4] __attribute__((aligned(16))) = { 7u, 7u, 7u, 7u };
    Asm("mv a2, %0                \n"
        "esp.vld.128.ip  q0, a2, 0 \n"
        "esp.vld.128.ip  q1, a2, 0 \n"
        :: "r"(v) : "a2", "memory");
}
#define PIE_WARMUP()  pie_warmup()
#define PIE_USE()     Asm("esp.vadd.u32  q0, q0, q1" ::: "memory")
#else
#define PIE_WARMUP()  ((void) 0)
#define PIE_USE()     ((void) 0)
#endif

/*
 *  計測タスク1(高優先度)．slp_tsk で待ち，起こされたら(計測区間内で)ベクタ命令を1回使う．
 */
void task1(EXINF exinf)
{
    uint_t	i;
    (void) exinf;

    PIE_WARMUP();

    (void) slp_tsk();
    PIE_USE();                       /* 切換え後の初回ベクタ(lazy では非オーナ→トラップ) */
    (void) end_measure(1);

    for (i = 1; i < NO_MEASURE; i++) {
        (void) begin_measure(2);
        (void) slp_tsk();
        PIE_USE();
        (void) end_measure(1);
    }
    (void) begin_measure(2);
    (void) slp_tsk();
}

/*
 *  計測タスク2(中優先度)．wup_tsk で task1 を起こし(切換え)，戻ったらベクタ命令を1回使う．
 */
void task2(EXINF exinf)
{
    uint_t	i;
    (void) exinf;

    PIE_WARMUP();

    for (i = 0; i < NO_MEASURE; i++) {
        (void) begin_measure(1);
        (void) wup_tsk(TASK1);
        PIE_USE();                   /* 切換え後の初回ベクタ(lazy では非オーナ→トラップ) */
        (void) end_measure(2);
    }
    (void) wup_tsk(TASK1);
}

/*
 *  メインタスク(低優先度)．
 */
void main_task(EXINF exinf)
{
    (void) exinf;

    dly_tsk(100000);                 /* 起動 settle */
    syslog_0(LOG_NOTICE, "Performance (perf1 + vector use)");
    (void) init_hist(1);
    (void) init_hist(2);
    (void) act_tsk(TASK1);
    (void) act_tsk(TASK2);

    /*  task1/task2 が NO_MEASURE 回まわるのを待ってから分布を表示  */
    dly_tsk(2000000);

    syslog_0(LOG_NOTICE, "Execution times of wup_tsk -> slp_tsk (with vector)");
    (void) print_hist(1);
    syslog_0(LOG_NOTICE, "Execution times of slp_tsk -> wup_tsk (with vector)");
    (void) print_hist(2);
    syslog_0(LOG_NOTICE, "TTSP_RESULT: DONE");

    while (true) { dly_tsk(1000000); }
}
