/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 * 
 *  Copyright (C) 2024 by Embedded and Real-Time Systems Laboratory
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
 *  $Id: target_kernel_impl.c 335 2023-04-18 10:50:40Z ertl-honda $
 */

/*
 *    カーネルのターゲット依存部（Plarfire SoC Kit用）
 */

#include "kernel_impl.h"
#include <sil.h>
#include "riscv.h"

/*
 *  カーネル動作時のメモリマップと関連する定義(ToDo)
 */

/*
 *  システムログの低レベル出力のための初期化
 */
#ifndef TOPPERS_OMIT_TECS

/*
 *  セルタイプtPutLogSIOPort内に実装されている関数を直接呼び出す．
 */
extern void tPutLogSIOPort_initialize(void);

#else /* TOPPERS_OMIT_TECS */

extern void sio_initialize(EXINF exinf);
extern void target_fput_initialize(void);

#endif /* TOPPERS_OMIT_TECS */

/*
 *  entry point (start.S)
 */
extern void start(void);

/*
 *  ハードウェアの初期化
 */
void
hardware_init_hook(void)
{
#ifdef TOPPERS_SUPPORT_HWLP
    /*
     *  HWLP コプロセッサコンテキスト管理(eager)の起動時初期化．
     *  本フックは start.S から master/slave 分岐前に呼ばれる＝全 PE で実行される．
     *  - 0x7F1(CSR_HWLP_STATE_REG)=CLEAN(2) で HWLP を常時有効化(ループ CSR を常時アクセス可)．
     *  - ループ CSR(0x7C6-0x7CB)を 0 初期化(boot 時の残留 count による偽ループを防止)．
     *  以後の新規タスクは hwlp_push(save 側)が切替毎に CSR をゼロ化するため count=0 を見る
     *  (共通部 start.S/start_r を変更せずに設計メモ #1 を満たす)．
     */
    Asm("csrwi 0x7f1, 2  \n\t"   /* HWLP enable: CLEAN          */
        "csrwi 0x7c6, 0  \n\t"   /* LOOP0_START_ADDR            */
        "csrwi 0x7c7, 0  \n\t"   /* LOOP0_END_ADDR              */
        "csrwi 0x7c8, 0  \n\t"   /* LOOP0_COUNT                 */
        "csrwi 0x7c9, 0  \n\t"   /* LOOP1_START_ADDR            */
        "csrwi 0x7ca, 0  \n\t"   /* LOOP1_END_ADDR              */
        "csrwi 0x7cb, 0  \n\t"   /* LOOP1_COUNT                 */
        ::: "memory");
#endif /* TOPPERS_SUPPORT_HWLP */

#ifdef TOPPERS_SUPPORT_PIE
#ifndef TOPPERS_PIE_LAZY
    /*
     *  PIE コプロセッサコンテキスト管理(eager)の起動時初期化．
     *  全PEで CSR_PIE_STATE_REG(0x7F2) = ON(1)にし，以後の pie_push/pie_pop の
     *  esp.* 命令を合法化する．レジスタ値は初期化不要(eager で毎切替 保存復帰し，
     *  PIE は制御に影響しないデータのみ．使うタスクは使用前に自分で設定する)．
     */
    Asm("csrwi 0x7f2, 1  \n\t"   /* PIE enable (INITIAL; IDF pie_enable と同値) */
        ::: "memory");
#else /* TOPPERS_PIE_LAZY */
    /*
     *  lazy: 起動時は CSR_PIE_STATE_REG(0x7f2)=OFF(0) にする．オーナ(pie_owner[PE])は起動時 NULL ゆえ
     *  どのタスクも非オーナで，初回 PIE 命令を illegal トラップさせて pie_exc_handler で
     *  オンデマンドに退避/復元する(ディスパッチ先のタスクの復元で chip_asm.inc
     *  pie_lazy_restore がオーナのみ enable に戻す)．
     */
    Asm("csrwi 0x7f2, 0  \n\t"   /* PIE OFF (lazy: 非オーナはトラップ) */
        ::: "memory");
#endif /* TOPPERS_PIE_LAZY */
#endif /* TOPPERS_SUPPORT_PIE */
}

/*
 *  Master processor initialization before str_ker().
 */
void
target_mprc_initialize(void)
{
    chip_mprc_initialize();
}

/*
 *  ターゲット依存の初期化
 */
void
target_initialize(PCB *p_my_pcb)
{    
    /*
     *  チップ依存の初期化
     */
    chip_initialize(p_my_pcb);

    /*
     *  SIOを初期化
     *    ESP32-P4 では UART0 のクロック有効化/リセット解除/ピン設定は
     *    ESP-IDF ブートローダが実施済みのため，ここでは行わない．
     *    (方式(a): IDF ローダ殻に乗せて起動)
     */
    sio_initialize(0);
    target_fput_initialize();
}

/*
 *  デフォルトのsoftware_term_hook（weak定義）
 */
__attribute__((weak))
void software_term_hook(void)
{
}

/*
 *  ターゲット依存の終了処理
 */
void
target_exit(void)
{
    /*
     *  software_term_hookの呼出し
     */
    software_term_hook();

    /*
     *  チップ依存の終了処理
     */
    chip_terminate();

    while (true) ;
}


/*
 *  システムログの低レベル出力（本来は別のファイルにすべき）
 */
#include "target_syssvc.h"
#include "target_serial.h"

/*
 *  低レベル出力用のSIOポート管理ブロック
 */
static SIOPCB  *p_siopcb_target_fput;

/*
 *  SIOポートの初期化
 */
void
target_fput_initialize(void)
{
    p_siopcb_target_fput = sio_opn_por(SIOPID_FPUT, 0);
}

/*
 *  SIOポートへのポーリング出力
 */
Inline void
polafire_soc_kit_uart_fput(char c)
{
    /*
     *  送信できるまでポーリング．ただしホストが受信(drain)していないと
     *  USB TX FIFO が永久に空かず，ここで無限スピンする．SMP 下では本関数を
     *  CPU ロック(MIE=0)中に呼ぶ経路があり，その場合は他コアを巻き込んだ
     *  デッドロックになる．そこで試行回数を有界にし，上限到達時は当該文字を
     *  捨てて先へ進む(ログ欠落は許容，ハングは不可)．
     */
    uint32_t spin = 0U;
    while (!(sio_snd_chr(p_siopcb_target_fput, c))) {
        if (++spin >= 100000U) {
            break;
        }
        sil_dly_nse(100);
    }
}

/*
 *  SIOポートへの文字出力
 */
void
target_fput_log(char c)
{
    if (c == '\n') {
        polafire_soc_kit_uart_fput('\r');
    }
    polafire_soc_kit_uart_fput(c);
}

/*
 *  _sbrk（newlib のヒープ確保）
 *
 *  newlib の malloc/printf 系（_sbrk_r 経由）がヒープを要求するため，自己完結
 *  の静的ヒープを用いた最小限の _sbrk を提供する．SDK の newlib_stubs.c は
 *  _write 等が FMP3 のシリアル出力と競合するため取り込まず，本実装を用いる．
 */
#include <sys/types.h>

#ifndef TARGET_HEAP_SIZE
#define TARGET_HEAP_SIZE  (64 * 1024)
#endif /* TARGET_HEAP_SIZE */

static char target_heap[TARGET_HEAP_SIZE];

caddr_t
_sbrk(int incr)
{
    static char *heap_end = target_heap;
    char        *prev_heap_end = heap_end;

    if ((heap_end + incr) > (target_heap + TARGET_HEAP_SIZE)
            || (heap_end + incr) < target_heap) {
        return (caddr_t) -1;            /* ヒープ不足 */
    }
    heap_end += incr;
    return (caddr_t) prev_heap_end;
}












/*
 *  newlib 再入(__getreent): rand/assert/malloc 等が要求する．
 *  単一の _impure_ptr を返す(シングル _reent)．TTSP3 の rand/assert 用途では十分．
 */
#include <reent.h>
/*  IDF リンク時は freertos が strong な __getreent を，newlib が _exit を提供するため
 *  これらは weak にして衝突を避ける．スタンドアロン FMP3 リンク(IDF 無し)ではこちらが使われる． */
__attribute__((weak)) struct _reent *
__getreent(void)
{
    return _impure_ptr;
}

/*
 *  newlib syscall スタブ群．
 *  __getreent を提供すると newlib の reent 層(closer/readr/writer/...)が
 *  これらの下位 syscall を要求するため，最小スタブを置く．
 *  TTSP3 ではファイル I/O は使わない(出力は sio 経由)ので失敗を返すだけでよい．
 */
#include <sys/stat.h>
#include <errno.h>

int   _close(int fd)                       { (void)fd; return -1; }
int   _fstat(int fd, struct stat *st)      { (void)fd; if (st) st->st_mode = S_IFCHR; return 0; }
int   _isatty(int fd)                      { (void)fd; return 1; }
int   _lseek(int fd, int off, int wh)      { (void)fd; (void)off; (void)wh; return 0; }
int   _read(int fd, char *buf, int len)    { (void)fd; (void)buf; (void)len; return 0; }
int   _write(int fd, const char *buf, int len) { (void)fd; (void)buf; return len; }
int   _getpid(void)                        { return 1; }
int   _kill(int pid, int sig)              { (void)pid; (void)sig; errno = EINVAL; return -1; }
__attribute__((weak)) void _exit(int code) { (void)code; target_exit(); for (;;) ; }
