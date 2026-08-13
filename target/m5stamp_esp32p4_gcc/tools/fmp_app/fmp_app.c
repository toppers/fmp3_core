/*
 *  ESP32-P4 FMP3 移植 / Step3: 周期タスク (SMP対応)
 *
 *  各 PE で自動起動し，get_pid() で自分の動作プロセッサを報告しながら
 *  dly_tsk で周期 syslog する．両コアで動けば prc1/prc2 両方の tick が出る．
 */
#include "fmp_app.h"

void
tick_task(EXINF exinf)
{
    int_t n = 0;
    ID    prcid;

    (void) get_pid(&prcid);
    syslog(LOG_NOTICE, "fmp_app: task started on prc%d (exinf=%d)",
           (int_t) prcid, (int_t) exinf);
    while (true) {
        (void) get_pid(&prcid);
        syslog(LOG_NOTICE, "tick prc%d #%d", (int_t) prcid, n);
        n++;
        dly_tsk(TICK_PERIOD);
    }
}
