/*
 *  ESP32-P4 FMP3 移植 / Step3: FMP3 ローダ殻（方式(a), SMP対応）
 *
 *  最小 IDF アプリ(UNICORE: core0 のみ)の app_main から FMP3 を起動する．
 *  ESP-IDF 2nd ブートローダがクロック/PSRAM/cache/MMU を初期化済み．
 *  SMP では，core0(master)が FMP3 を起動する前に core1 を
 *  toppers_start へ向けて起動する(IDF の start_other_core と同じ手順)．
 *  core1 は hart1=PRC2 として slave_wait に入り，master が start_sync で解放する．
 */
#include <stdio.h>
#include <stdint.h>
#include "esp_cpu.h"
#include "soc/soc.h"
#include "soc/hp_sys_clkrst_reg.h"

/* FMP3 の起動エントリ(start.S, IRAM 配置)．全コアがここに入る． */
extern void toppers_start(void);

/* ROM API: app(2nd)コアのブートアドレス設定 */
extern void ets_set_appcpu_boot_addr(uint32_t addr);

void app_main(void)
{
#if !CONFIG_FREERTOS_UNICORE
    /* dual-core IDF だと core1 は IDF FreeRTOS 稼働中．UNICORE 前提． */
#endif
    /*
     *  core1(hart1)を FMP3 の toppers_start で起動する．
     *  手順は IDF cpu_start.c の start_other_core と同じ:
     *    クロック有効化 → リセット解除 → ブートアドレス設定 → unstall．
     *  core1 は ROM appcpu 初期化後 toppers_start に入り，my_pid=PRC2 で
     *  slave_wait に入って master(core0)の start_sync 解放を待つ．
     *
     *  FMP_SINGLE_CORE定義時（PRC_NUM=1切り分け実験）はcore1を起動しない
     *  （PRC_NUM=1カーネルをcore1にも入れると未定義動作になるため）。
     */
#ifndef FMP_SINGLE_CORE
    REG_SET_BIT(HP_SYS_CLKRST_SOC_CLK_CTRL0_REG, HP_SYS_CLKRST_REG_CORE1_CPU_CLK_EN);
    REG_CLR_BIT(HP_SYS_CLKRST_HP_RST_EN0_REG, HP_SYS_CLKRST_REG_RST_EN_CORE1_GLOBAL);
    ets_set_appcpu_boot_addr((uint32_t) &toppers_start);
    esp_cpu_unstall(1);

    printf("\n[fmp_loader] core1 launched; core0 entering FMP3 (toppers_start)...\n");
#else
    printf("\n[fmp_loader] FMP_SINGLE_CORE: core1 not launched; core0 entering FMP3 (toppers_start)...\n");
#endif
    fflush(stdout);

    /* core0 が master として FMP3 を起動．戻ってこない． */
    toppers_start();

    printf("[fmp_loader] ERROR: returned from toppers_start\n");
    for (;;) { }
}
