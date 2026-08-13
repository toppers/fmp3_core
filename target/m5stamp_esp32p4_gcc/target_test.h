/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 * 
 *  Copyright (C) 2007-2018 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 * 
 *  上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
 *  ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
 *  変・再配布（以下，利用と呼ぶ）することを無償で許諾する．
 *  (1) 本ソフトウェアをソースコードの形で利用する場合には，上記の著作
 *      権表示，この利用条件および下記の無保証規定が，そのままの形でソー
 *      スコード中に含まれていること．
 *  (2) 本ソフトウェアを，ライブラリ形式など，他のソフトウェア開発に使
 *      用できる形で再配布する場合には，再配布に伴うドキュメント（利用
 *      者マニュアルなど）に，上記の著作権表示，この利用条件および下記
 *      の無保証規定を掲載すること．
 *  (3) 本ソフトウェアを，機器に組み込むなど，他のソフトウェア開発に使
 *      用できない形で再配布する場合には，次のいずれかの条件を満たすこ
 *      と．
 *    (a) 再配布に伴うドキュメント（利用者マニュアルなど）に，上記の著
 *        作権表示，この利用条件および下記の無保証規定を掲載すること．
 *    (b) 再配布の形態を，別に定める方法によって，TOPPERSプロジェクトに
 *        報告すること．
 *  (4) 本ソフトウェアの利用により直接的または間接的に生じるいかなる損
 *      害からも，上記著作権者およびTOPPERSプロジェクトを免責すること．
 *      また，本ソフトウェアのユーザまたはエンドユーザからのいかなる理
 *      由に基づく請求からも，上記著作権者およびTOPPERSプロジェクトを
 *      免責すること．
 * 
 *  本ソフトウェアは，無保証で提供されているものである．上記著作権者お
 *  よびTOPPERSプロジェクトは，本ソフトウェアに関して，特定の使用目的
 *  に対する適合性も含めて，いかなる保証も行わない．また，本ソフトウェ
 *  アの利用により直接的または間接的に生じたいかなる損害に関しても，そ
 *  の責任を負わない．
 * 
 *  $Id: target_test.h 166 2019-08-28 07:54:41Z ertl-honda $
 */

/*
 *		テストプログラムのターゲット依存部（ZYBO_Z7用）
 */

#ifndef TOPPERS_TARGET_TEST_H
#define TOPPERS_TARGET_TEST_H

/*
 *  コアで共通な定義（チップ依存部は飛ばす）
 */
#include "core_test.h"
#include "esp32p4.h"

/*
 *  割込みテスト(test_int1)用の割込み番号と属性
 *    ESP32-P4 では CLIC 外部線(16〜47)の空き線をテスト用に使う。
 *    16 は USB-Serial/JTAG が使用するため 17 を割当てる。
 *    software で割込みを発生(ras_int=IP セット)させるためエッジトリガ(TA_EDGE)。
 *    割込みマトリクスのソースは割付けない(software IP のみで発生)。
 */
#define INTNO1          17
#define INTNO1_INTPRI   (-4)
#define INTNO1_INTATR   (TA_ENAINT | TA_EDGE)

/*
 *  割込みソースのクリア(ISR から呼ばれる)。
 *    software raise したエッジ割込みの IP を CLIC レジスタ直接操作でクリア。
 *    esp32p4.h の CLIC_INT_CTRL/CLIC_INT_IP_BIT を使い自己完結させる。
 */
#define intno1_clear() \
    (*(volatile uint32_t *)CLIC_INT_CTRL(INTNO1) &= ~CLIC_INT_IP_BIT)

#endif /* TOPPERS_TARGET_TEST_H */
