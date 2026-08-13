/*
 *  ESP32-P4 FMP3 移植 / lazy PIE: ras_ter 要求による終了経路の release_context 正当性 実証アプリ
 *
 *  設計: arch/riscv_gcc/esp32p4/pie_hwlp_design.md §9.2．
 */
#include "fmp_pie_raster_app.h"
#include "kernel_cfg.h"				/* act_tsk/ras_ter 対象の自動割付 ID */
#include <stdint.h>

/*  非分割の PIE 累算(golden 計測・TERM・NOISE 用)．q0=0 から q1={seed×4} を iters 回足す．  */
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

/*  yield を跨ぐ PIE 累算: 前半 → dly_tsk(YIELD, NOISE にオーナを奪わせる) → 後半．  */
static uint32_t __attribute__((noinline))
pie_compute_yield(uint32_t seed, uint32_t iters)
{
    uint32_t in[4]   __attribute__((aligned(16)));
    uint32_t zero[4] __attribute__((aligned(16))) = { 0, 0, 0, 0 };
    uint32_t out[4]  __attribute__((aligned(16)));
    uint32_t half = iters / 2u;
    uint32_t rem  = iters - half;

    in[0] = seed; in[1] = seed; in[2] = seed; in[3] = seed;

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

    (void) dly_tsk(YIELD_TIME);

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
 *  PIE オーナになり，ras_ter で要求された終了を ena_ter で自タスク終了するタスク．
 *  dis_ter で終了禁止にしてから PIE オーナ(CSR_PIE_STATE_REG=ON)になり，REQUESTER を wup して
 *  ras_ter(自分)で raster をセットさせる(この間プリエンプトされてもオーナは据え置き)．
 *  WORKER を ready 化してから ena_ter→raster 保留ゆえ**オーナのまま自タスク終了**する．
 *  release_context が無いと所有権が残り，次に start_r 起動する WORKER が幽霊オーナ化する．
 */
void
term_task(EXINF exinf)
{
    (void) exinf;
    (void) dis_ter();                             /* 終了禁止: ras_ter は raster セットに留まる */
    (void) pie_compute(TERM_SEED, TERM_ITERS);    /* この PE の PIE オーナになる(CSR_PIE_STATE_REG=ON) */
    (void) wup_tsk(REQUESTER_TASK);               /* REQUESTER を起こし ras_ter(自分)させる */
    (void) act_tsk(WORKER_TASK);                  /* 後続 WORKER を ready 化 */
    (void) ena_ter();                             /* raster 保留→自タスク終了(オーナのまま) */
    (void) ext_tsk();                             /* 念のため(raster 未設定時の保険) */
}

/*
 *  TERM の wup で起き，TERM の終了を ras_ter で要求する(raster セット)．
 *  TERM は dis_ter 済みなので即終了せず，TERM 自身の ena_ter まで終了が延期される．
 */
void
requester_task(EXINF exinf)
{
    (void) exinf;
    while (true) {
        (void) slp_tsk();                         /* TERM の wup を待つ */
        (void) ras_ter(TERM_TASK);                /* TERM の終了を要求(raster セット) */
    }
}

/*
 *  TERM 自終了の直後に start_r 起動する新規タスク．yield を跨ぐ PIE 累算を golden 照合し，
 *  次ラウンドの TERM を ready 化してから ext_tsk．
 */
void
worker_task(EXINF exinf)
{
    uint32_t r;
    (void) exinf;

    /*  起動時ランプ: 最初の数ラウンドはチェーンを緩め，logtask が初期ログを吐き切れるようにする
     *  (wup/slp + ena_ter + NOISE の密なチェーンが起動直後に logtask の flush を飢餓させるのを防ぐ)  */
    if (round_cnt < 10u) { dly_tsk(40000); }
    r = pie_compute_yield(WORKER_SEED, WORKER_ITERS);
    if (r == golden_w) { okW++; } else { errW++; }
    round_cnt++;
    (void) act_tsk(TERM_TASK);                    /* 次ラウンドの TERM を ready 化 */
    (void) ext_tsk();
}

/*  WORKER の yield 中に連続 PIE してオーナを奪う(常時 ready で free-run)．  */
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
 *  golden 計測・チェーン kick・周期集計．起動直後に dly_tsk で settle し(boot banner を
 *  logtask が吐き切ってから重い PIE チェーンを始める)，チェーンを 1 回 kick する．
 */
void
report_task(EXINF exinf)
{
    (void) exinf;

    dly_tsk(100000);                 /* boot banner 吐き切りを待つ(重い PIE チェーン開始前) */

    dis_dsp();                       /* PRC1 上で単独に golden を計測 */
    golden_w = pie_compute(WORKER_SEED, WORKER_ITERS);
    ready = true;
    ena_dsp();

    syslog(LOG_NOTICE, "PIEraster start: golden_w=%d (WORKER %d*%d, ras_ter->ena_ter self-term)",
           (int_t) golden_w, (int_t) WORKER_SEED, (int_t) WORKER_ITERS);

    (void) act_tsk(TERM_TASK);       /* チェーン開始 */

    while (true) {
        (void) dly_tsk(REPORT_PERIOD);
        syslog(LOG_NOTICE, "PIEraster: okW=%d errW=%d rounds=%d  [TTSP_RESULT: %s]",
               (int_t) okW, (int_t) errW, (int_t) round_cnt,
               (errW == 0U) ? "PASS" : "FAIL");
    }
}
