/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
 *
 *  Copyright (C) 2024-2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  利用条件は TOPPERS ライセンス（polarfire_soc_kit.h と同一）。無保証。
 */

/*
 *    M5Stamp ESP32P4 ボード固有の定義
 */
#ifndef M5STAMP_ESP32P4_KIT_H
#define M5STAMP_ESP32P4_KIT_H

#ifdef M5STAMP_ESP32P4
#define TARGET_NAME   "M5Stamp ESP32P4 <HP RV32IMAFC, RISC-V>"
/*  HP コア周波数（Step1 実機測定）。mtime も同クロックで歩進する。  */
#define CORE_CLK_MHZ  360
/*  コンソールは UART0 を使用（U0TXD=GPIO37/U0RXD=GPIO38）  */
#define USE_UART0
/*  sil_dly_nse 用のチューニング値（TODO: 実測で調整）  */
#define SIL_DLY_TIM1  14
#define SIL_DLY_TIM2  8
#endif /* M5STAMP_ESP32P4 */

#endif /* M5STAMP_ESP32P4_KIT_H */
