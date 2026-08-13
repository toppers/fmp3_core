/*
 *  ESP32-P4 FMP3 移植 / TLS(スレッドローカルストレージ)初期化 検証アプリ
 *
 *  各 PE のタスクが exinf から決めた自分専用の種で srand() し，rand() を
 *  TLS_TEST_COUNT 回呼ぶ。picolibc の rand() 実装(_tls_rand_next の LCG)を
 *  本ファイルでも独立に計算し，毎回の戻り値と突き合わせる。
 *
 *  tp が正しくタスク毎に独立していれば，2 タスクが交互に(dly_tsk で切り替え
 *  ながら) rand() を呼んでも，各タスクの計算列は自分の種から始まる純粋な
 *  LCG 列と完全に一致するはず。tp が未初期化(=0)や共有されていれば，他タスク
 *  の呼出しで内部状態が混線し，期待値と食い違う（あるいは即 Load access
 *  fault で本アプリまで到達しない）。
 */
#include "fmp_tls_app.h"
#include <stdlib.h>

static volatile int_t g_ng_total   = 0;
static volatile int_t g_done_count = 0;

/*
 *  picolibc esp_libc/src/picolibc/rand.c の実装と同一の式。
 *  (_Thread_local unsigned long long _tls_rand_next; 初期値1、
 *   srand(seed): _tls_rand_next = seed;
 *   rand(): _tls_rand_next = _tls_rand_next * 6364136223846793005ULL + 1;
 *           return (int)((_tls_rand_next >> 32) & RAND_MAX);)
 */
static int
expect_next(unsigned long long *p_state)
{
    *p_state = *p_state * 6364136223846793005ULL + 1ULL;
    return (int) ((*p_state >> 32) & RAND_MAX);
}

void
tls_task(EXINF exinf)
{
    ID     prcid;
    int_t  n;
    int_t  ng = 0;
    unsigned int      seed;
    unsigned long long expect_state;

    (void) get_pid(&prcid);
    seed = (unsigned int) (exinf * 12345 + 1);
    syslog(LOG_NOTICE, "fmp_tls_app: task started on prc%d seed=%u",
           (int_t) prcid, seed);

    srand(seed);
    expect_state = (unsigned long long) seed;

    for (n = 0; n < TLS_TEST_COUNT; n++) {
        int got    = rand();
        int expect = expect_next(&expect_state);

        if (got != expect) {
            syslog(LOG_NOTICE,
                   "## Unexpected prc%d n=%d got=%d expect=%d",
                   (int_t) prcid, (int_t) n, got, expect);
            ng++;
        }
        if ((n % (TLS_TEST_COUNT / 10)) == 0) {
            syslog(LOG_NOTICE, "tls prc%d n=%d val=%d",
                   (int_t) prcid, (int_t) n, got);
        }
        dly_tsk(TLS_TEST_INTERVAL);
    }

    syslog(LOG_NOTICE, "fmp_tls_app: prc%d done, %d/%d mismatches",
           (int_t) prcid, (int_t) ng, TLS_TEST_COUNT);

    {
        bool_t is_last;
        int_t  ng_total;

        loc_cpu();
        g_ng_total += ng;
        g_done_count++;
        is_last  = (g_done_count == TNUM_PRCID);
        ng_total = g_ng_total;
        unl_cpu();

        if (is_last) {
            if (ng_total == 0) {
                syslog(LOG_NOTICE,
                       "TTSP_RESULT: PASS (%d prc x %d iter, "
                       "all rand() sequences independent & correct)",
                       TNUM_PRCID, TLS_TEST_COUNT);
            }
            else {
                syslog(LOG_NOTICE,
                       "TTSP_RESULT: FAIL (%d mismatches total)",
                       (int_t) ng_total);
            }
        }
    }

    while (true) {
        dly_tsk(1000000);
    }
}
