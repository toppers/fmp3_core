/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 * 
 *  Copyright (C) 2000-2003 by Embedded and Real-Time Systems Laboratory
 *                              Toyohashi Univ. of Technology, JAPAN
 *  Copyright (C) 2005-2023 by Embedded and Real-Time Systems Laboratory
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
 *  $Id: interrupt.c 335 2023-04-18 10:50:40Z ertl-honda $
 */

/*
 *		割込み管理機能
 */

#include "kernel_impl.h"
#include "check.h"
#include "task.h"
#include "interrupt.h"
#include "spin_lock.h"

/*
 *  トレースログマクロのデフォルト定義
 */
#ifndef LOG_ISR_ENTER
#define LOG_ISR_ENTER(isrid)
#endif /* LOG_ISR_ENTER */

#ifndef LOG_ISR_LEAVE
#define LOG_ISR_LEAVE(isrid)
#endif /* LOG_ISR_LEAVE */

#ifndef LOG_DIS_INT_ENTER
#define LOG_DIS_INT_ENTER(intno)
#endif /* LOG_DIS_INT_ENTER */

#ifndef LOG_DIS_INT_LEAVE
#define LOG_DIS_INT_LEAVE(ercd)
#endif /* LOG_DIS_INT_LEAVE */

#ifndef LOG_ENA_INT_ENTER
#define LOG_ENA_INT_ENTER(intno)
#endif /* LOG_ENA_INT_ENTER */

#ifndef LOG_ENA_INT_LEAVE
#define LOG_ENA_INT_LEAVE(ercd)
#endif /* LOG_ENA_INT_LEAVE */

#ifndef LOG_CLR_INT_ENTER
#define LOG_CLR_INT_ENTER(intno)
#endif /* LOG_CLR_INT_ENTER */

#ifndef LOG_CLR_INT_LEAVE
#define LOG_CLR_INT_LEAVE(ercd)
#endif /* LOG_CLR_INT_LEAVE */

#ifndef LOG_RAS_INT_ENTER
#define LOG_RAS_INT_ENTER(intno)
#endif /* LOG_RAS_INT_ENTER */

#ifndef LOG_RAS_INT_LEAVE
#define LOG_RAS_INT_LEAVE(ercd)
#endif /* LOG_RAS_INT_LEAVE */

#ifndef LOG_PRB_INT_ENTER
#define LOG_PRB_INT_ENTER(intno)
#endif /* LOG_PRB_INT_ENTER */

#ifndef LOG_PRB_INT_LEAVE
#define LOG_PRB_INT_LEAVE(ercd)
#endif /* LOG_PRB_INT_LEAVE */

#ifndef LOG_CHG_IPM_ENTER
#define LOG_CHG_IPM_ENTER(intpri)
#endif /* LOG_CHG_IPM_ENTER */

#ifndef LOG_CHG_IPM_LEAVE
#define LOG_CHG_IPM_LEAVE(ercd)
#endif /* LOG_CHG_IPM_LEAVE */

#ifndef LOG_GET_IPM_ENTER
#define LOG_GET_IPM_ENTER(p_intpri)
#endif /* LOG_GET_IPM_ENTER */

#ifndef LOG_GET_IPM_LEAVE
#define LOG_GET_IPM_LEAVE(ercd, p_intpri)
#endif /* LOG_GET_IPM_LEAVE */

/*
 *  割込み番号の範囲の判定
 */
#ifndef VALID_INTNO_DISINT
#define VALID_INTNO_DISINT(prcid, intno)	VALID_INTNO(prcid, intno)
#endif /* VALID_INTNO_DISINT */

#ifndef VALID_INTNO_CLRINT
#define VALID_INTNO_CLRINT(prcid, intno)	VALID_INTNO(prcid, intno)
#endif /* VALID_INTNO_CLRINT */

#ifndef VALID_INTNO_RASINT
#define VALID_INTNO_RASINT(prcid, intno)	VALID_INTNO(prcid, intno)
#endif /* VALID_INTNO_RASINT */

#ifndef VALID_INTNO_PRBINT
#define VALID_INTNO_PRBINT(prcid, intno)	VALID_INTNO(prcid, intno)
#endif /* VALID_INTNO_PRBINT */

/*
 *  割込み優先度の範囲の判定
 */
#ifndef VALID_INTPRI_CHGIPM
#define VALID_INTPRI_CHGIPM(intpri)	\
					(TMIN_INTPRI <= (intpri) && (intpri) <= TIPM_ENAALL)
#endif /* VALID_INTPRI_CHGIPM */

/*
 *  割込みサービスルーチンIDから割込みサービスルーチン管理ブロックを取り
 *  出すためのマクロ
 */
#define INDEX_ISR(isrid)	((uint_t)((isrid) - TMIN_ISRID))
#define get_isrcb(isrid)	(p_isrcb_table[INDEX_ISR(isrid)])

/*
 *  走査キー（isrpri, isrseq）の辞書式比較
 *
 *  「(pri1, seq1) が (pri2, seq2) より真に大きい」を判定する．call_isrの
 *  走査が，ジャイアントロックを外してISR本体を呼んだ後に「次に呼ぶISR」を
 *  再決定するために使う．
 */
#define ISR_KEY_GT(pri1, seq1, pri2, seq2)								\
			(((pri1) > (pri2))											\
				|| (((pri1) == (pri2)) && ((seq1) > (seq2))))

/*
 *  割込みサービスルーチンキューへの登録
 *
 *  キューは(isrpri, isrseq)の辞書式昇順に保たれる．isrpriが自分より真に
 *  大きい最初の要素の直前へ挿入するので，同一isrpriの中ではenqueueした
 *  順（＝isrseqの昇順）に並ぶ（dcre interrupt.c:182-195と同じ形）．
 *
 *  ★isrseqはキューの生存期間を通じて単調増加する．リセットしない
 *  （段階4 Task 4のレビューで判明した合成の隙間を，Task 5でコントローラが
 *  裁定して撤回した．元は「キューが空になったときisrseqを0へ戻す」実装
 *  だったが，走査中（call_isrがジャイアントロックを外してISR本体を呼んで
 *  いる間）にキューが一時的に空になり，その隙にacre_isrされたISRは
 *  isrseqが0から振り直されるため，走査側の継続キーcurより小さくなり，
 *  同一の割込み起動では拾われない場合があった．u32のラップには1つの
 *  キューへシステム寿命の間に2^32回enqueueする必要があり，実用上到達
 *  不能である（リセットを正当化していたのと同じ論法を，リセットではなく
 *  ラップの受容に転用する）．
 *
 *  isrseqの単調性により，走査中のキュー一時空→再enqueueでも，走査位置
 *  （cur）と同じかそれより後の isrpri 位置に入る新エントリのキーは必ず
 *  cur より大きく，同一起動内で取りこぼされない（del→再acre の同一
 *  isrpri ケースを含む）．cur より高優先（isrpri が小さい）位置に入る
 *  新エントリは走査が既に通過した位置のため本起動では実行されず，
 *  次回の割込み発生から有効になる — 静的インライン連鎖の「起動開始
 *  時点の ISR 集合を処理する」意味論と同じであり，仕様である．
 *  （Codex外部レビュー指摘：ISR_KEY_GTはisrpriが第一キーの辞書式比較の
 *  ため，isrseqが単調でもcurより高優先側では保証が及ばない．従来の
 *  コメントの「必ず走査位置より大きく」はisrpri>=curの領域にしか
 *  成立しない過大主張だった．機構はTask 5時点のまま変更していない）．
 *
 *  ★この単調性の回帰テストは test/test_dcre5.c の手順9 である
 *  （静的ISRを持たないINTNO2のキューを走査中に完全ドレインさせ，
 *  同一isrpriで再acreしたISRが同一起動内で拾われることを実測する）．
 *  リセット分岐を戻すと手順9 が倒れることを変異controlで確認済み．
 *
 *  ジャイアントロックを取得した状態で呼び出すこと．
 */
Inline void
enqueue_isr(ISRQCB *p_isr_queue, ISRCB *p_isrcb)
{
	QUEUE	*p_entry;
	PRI		isrpri = p_isrcb->p_isrinib->isrpri;

	p_isrcb->isrseq = p_isr_queue->isrseq;
	p_isr_queue->isrseq += 1U;

	for (p_entry = p_isr_queue->isr_queue.p_next;
							p_entry != &(p_isr_queue->isr_queue);
							p_entry = p_entry->p_next) {
		if (isrpri < ((ISRCB *) p_entry)->p_isrinib->isrpri) {
			break;
		}
	}
	queue_insert_prev(p_entry, &(p_isrcb->isr_queue));
}

/*
 *  割込みサービスルーチン呼出しキューの検索
 *
 *  cfgが生成するグローバルな適格intno表（isr_queue_list）を二分探索する．
 *  この表はENA_DYNISRされたintnoだけを昇順に持つので，
 *  ・範囲外のintno
 *  ・CFG_INTの無いintno
 *  ・ENA_DYNISRされていないintno
 *  ・DEF_INHが競合するintno
 *  はいずれもNULLになる．per-coreのビットマップ（check_intno_cfg）を使わ
 *  ないため，判定結果が呼出しコアに依存しない．
 *
 *  dcre interrupt.c:267-293の転写（型がISRQCB *に変わるだけ）．
 */
Inline ISRQCB *
search_isr_queue(INTNO intno)
{
	int_t	left, right, i;

	if (tnum_isr_queue == 0) {
		return(NULL);
	}

	left = 0;
	right = tnum_isr_queue - 1;
	while (left < right) {
		i = (left + right + 1) / 2;
		if (intno < isr_queue_list[i].intno) {
			right = i - 1;
		}
		else {
			left = i;
		}
	}
	if (isr_queue_list[left].intno == intno) {
		return(isr_queue_list[left].p_isr_queue);
	}
	else {
		return(NULL);
	}
}

/*
 *  割込みサービスルーチン機能の初期化
 */
#ifdef TOPPERS_isrini

/*
 *  使用していない割込みサービスルーチン管理ブロックのリスト
 *
 *  ISRCBの先頭フィールドがQUEUE（isr_queue）なので，そのままfree-listの
 *  リンクに流用する（dcre interrupt.c:202,229と同一）．
 */
QUEUE	free_isrcb;

void
initialize_isr(PCB *p_my_pcb)
{
	uint_t	i, j;
	ISRCB	*p_isrcb;
	ISRINIB	*p_isrinib;

	/*
	 *  割込みサービスルーチンはプロセッサ親和を持たない（ISRINIBに
	 *  iprcid/affinityが無く，ISRCBにp_pcbが無い）．実行プロセッサは
	 *  intnoの配線（CFG_INTのクラス）で決まるため，カーネルオブジェクト
	 *  としては非親和である．したがってマスタプロセッサだけが初期化し，
	 *  他プロセッサへの可視性は本関数の呼出し後のbarrier_syncが保証する
	 *  （段階1のfree_tcb・段階3bのfree_dtqcbと同じ論証）．
	 */
	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		for (i = 0; i < tnum_isr_queue; i++) {
			queue_initialize(&(isr_queue_table[i].isr_queue));
			isr_queue_table[i].isrseq = 0U;
		}

		/*
		 *  静的生成ISRの初期化
		 *
		 *  isrorder_tableはisrid昇順である（cfgが生成する．dcreの挿入順
		 *  からの意図的な逸脱で，理由はENA_DYNISRの有無で呼出し順序が
		 *  変わらないようにするため）．この順にenqueue_isrすることで，
		 *  キューの並びは「isrid昇順を基底とするisrpriの安定ソート」＝
		 *  インライン連鎖の呼出し順序と完全に一致する．
		 *
		 *  ★p_isr_queueがNULLのISRは，ENA_DYNISRされていないintnoに
		 *  登録された静的ISRである．インライン連鎖から直接呼ばれるので
		 *  キューには入れない（dcreには無い分岐．dcreは全intnoをキュー化
		 *  するためNULLになりえない）．
		 */
		for (i = 0; i < tnum_sisr; i++) {
			j = INDEX_ISR(isrorder_table[i]);
			p_isrcb = p_isrcb_table[j];
			p_isrcb->p_isrinib = &(isrinib_table[j]);
			p_isrcb->isrseq = 0U;
			p_isrcb->running = 0U;
			if (p_isrcb->p_isrinib->p_isr_queue != NULL) {
				enqueue_isr(p_isrcb->p_isrinib->p_isr_queue, p_isrcb);
			}
		}

		/*
		 *  動的生成用スロットの初期化
		 *
		 *  free-listはFIFO（queue_insert_prevで末尾へ．段階1で裁定済み）．
		 *  iは静的ループから引き継ぐ（dcre interrupt.c:217,224と同じ書き方）．
		 */
		queue_initialize(&free_isrcb);
		for (j = 0; i < tnum_isr; i++, j++) {
			p_isrcb = p_isrcb_table[i];
			p_isrinib = &(aisrinib_table[j]);
			p_isrinib->isratr = TA_NOEXS;
			p_isrcb->p_isrinib = ((const ISRINIB *) p_isrinib);
			p_isrcb->isrseq = 0U;
			p_isrcb->running = 0U;
			queue_insert_prev(&free_isrcb, &(p_isrcb->isr_queue));
		}
	}
}

#endif /* TOPPERS_isrini */

/*
 *  割込みサービスルーチンの呼出し
 */
#ifdef TOPPERS_isrcal

/*
 *  割込みサービスルーチン呼出しキューの走査
 *
 *  ENA_DYNISRされた割込み番号の割込みハンドラ（cfgが生成する
 *  _kernel_inthdr_<intno>）から呼ばれる．キューに登録された割込みサービス
 *  ルーチンを(isrpri, isrseq)の辞書式昇順に呼び出す．
 *
 *  【dcre（interrupt.c:240-260）との相違と，その理由】
 *
 *  dcreは単方向リストを p_queue = p_queue->p_next で辿るだけである．これは
 *  「走査（割込み文脈）とacre_isr/del_isr（タスク文脈）が時間的に排他である」
 *  という単一プロセッサの性質に依存している．FMP3ではコアAの走査とコアBの
 *  acre_isr/del_isrが真に並行しうるため，この形は使えない．
 *
 *  (1) キューの参照はジャイアントロックの下でのみ行う．acre_isr/del_isrも
 *      ジャイアントロックの下でキューを操作するので，両者は排他される．
 *      割込み文脈でジャイアントロックを取ること自体はsignal_time
 *      （time_event.c:709-722）に先例がある．
 *
 *  (2) ISR本体はロックを外して呼ぶ（現行のインライン連鎖と同じく，ISRは
 *      CPUロック解除状態で実行される）．ロックを外している間にキューが
 *      書き換わりうるので，次に呼ぶISRは「前回呼んだISRの(isrpri, isrseq)
 *      より大きい最小の要素」としてロック再取得後に再決定する．
 *
 *      ★ポインタやISRIDを継続キーにできない理由：del_isrで返却された
 *      スロットがacre_isrで再利用されると，同じ番地・同じIDが別のISRを
 *      指すようになり，「もう呼んだかどうか」を判定できなくなる．
 *      isrseqはenqueueのたびに単調増加する世代番号なので，再利用されても
 *      新しい値が付き，曖昧にならない．
 *
 *      ★isrpriだけでは足りない理由：同一isrpriのISRが複数あるとき，
 *      「> 前回のisrpri」では同一優先度の残りを取りこぼし，
 *      「>= 前回のisrpri」では呼んだものを二重に呼ぶ．
 *
 *      キューは(isrpri, isrseq)の昇順に保たれている（enqueue_isr）ので，
 *      先頭から見て条件を最初に満たした要素が最小である．
 *
 *  (3) ISR本体の実行中はp_isrcb->runningに自プロセッサのビットを立てる．
 *      del_isrはキューから外した後にこのビットが全て落ちるまで待つ
 *      （quiesce）ので，実行中のISRCBがfree-listへ戻って再利用されること
 *      はない．したがってISR本体から戻った後にp_next->runningを触っても安全
 *      である（del_isr側はまだ待っている）．
 *
 *      runningを（カウンタでなく）プロセッサ数分のビットマップとして
 *      実装できるのは，「同一intnoの割込みハンドラが同一コアで多重に
 *      走ることはない」からである．doc/porting.txt:790,807の割込み出入口
 *      処理（*f）は，受け付けた割込みの処理中は割込み優先度マスクを
 *      その割込みの優先度に設定し（同一intno＝同一優先度の再入を含めて
 *      マスクする），出口でのみ元へ戻す．したがって同一コアでは
 *      _kernel_inthdr_<intno>（＝call_isr）が自分自身に多重に入ることは
 *      なく，1コアにつき「実行中／非実行中」の1状態しか要らない．
 *
 *  (4) ISR本体からの復帰後のロック状態の復元はcall_cyclic（cyclic.c:541-549）
 *      と同じ3分岐である．sense_lock()が真のときはCPUロック状態のままなので
 *      lock_cpu()を呼んではならない．force_unlock_spinはスピンロックだけを
 *      解放し，CPUロック状態には触らない（spin_lock.c:162-176）．
 *
 *  (5) ★走査の終了時は必ずCPUロック解除状態で戻る．インライン連鎖は
 *      ロック復元コードをISRとISRの間にしか置かない（interrupt.trb:613の
 *      「index > 0」）ため，最後のISRがCPUロックしたまま戻るとそのロックが
 *      割り込まれたタスクへ漏れる．キュー方式ではこれが起きない．この差が
 *      及ぶのはENA_DYNISRされた割込み番号だけである．
 */
void
call_isr(ISRQCB *p_isr_queue)
{
	PCB			*p_my_pcb = get_my_pcb();
	QUEUE		*p_entry;
	ISRCB		*p_isrcb;
	ISRCB		*p_next;
	PRI			cur_isrpri;
	uint32_t	cur_isrseq;
	bool_t		first;
	ISR			isr;
	EXINF		exinf;
	uint_t		my_bit;

	assert(sense_context(p_my_pcb));
	assert(!sense_lock());

	/*
	 *  割込みハンドラは他のプロセッサへマイグレートしないので，CPUロック
	 *  状態にする前に自プロセッサのPCBを取得してよい（interrupt.trb:607-609
	 *  の既存コメントと同じ根拠）．
	 */
	my_bit = (1U << get_my_prcidx());

	lock_cpu();
	acquire_glock();

	first = true;
	cur_isrpri = 0;
	cur_isrseq = 0U;

	for (;;) {
		/*
		 *  (isrpri, isrseq)がcurより大きい最小の要素を求める．キューは
		 *  この順序で昇順に保たれているので，先頭から見て最初に条件を
		 *  満たした要素が答えである．
		 */
		p_next = NULL;
		for (p_entry = p_isr_queue->isr_queue.p_next;
								p_entry != &(p_isr_queue->isr_queue);
								p_entry = p_entry->p_next) {
			p_isrcb = ((ISRCB *) p_entry);
			if (first || ISR_KEY_GT(p_isrcb->p_isrinib->isrpri,
									p_isrcb->isrseq,
									cur_isrpri, cur_isrseq)) {
				p_next = p_isrcb;
				break;
			}
		}
		if (p_next == NULL) {
			break;
		}

		cur_isrpri = p_next->p_isrinib->isrpri;
		cur_isrseq = p_next->isrseq;
		first = false;

		/*
		 *  ジャイアントロックを外している間にISRINIBが書き換わることは
		 *  ないが（acre_isrはfree-listから取り出したISRCBのINIBしか触らず，
		 *  このISRCBはrunningが立っている間free-listへ戻らない），
		 *  dcreと同じくローカルへコピーしてから呼ぶ．
		 */
		isr = p_next->p_isrinib->isr;
		exinf = p_next->p_isrinib->exinf;
		p_next->running |= my_bit;

		LOG_ISR_ENTER(ISRID(p_next));
		release_glock();
		unlock_cpu();

		(*isr)(exinf);

		LOG_ISR_LEAVE(ISRID(p_next));

		/*  ISRの呼出し前の状態に戻す（call_cyclic と同じ3分岐）  */
		if (sense_lock()) {
			force_unlock_spin(p_my_pcb);
		}
		else {
			lock_cpu();
		}
		acquire_glock();

		p_next->running &= ~my_bit;
	}

	release_glock();
	unlock_cpu();
}

#endif /* TOPPERS_isrcal */

/*
 *  割込みサービスルーチンの生成
 *
 *  pk_cisr->exinfは，エラーチェックをせず，一度しか参照しないため，ロー
 *  カル変数にコピーする必要がない（途中で書き換わっても支障がない）．
 */
#ifdef TOPPERS_acre_isr

#ifndef LOG_ACRE_ISR_ENTER
#define LOG_ACRE_ISR_ENTER(pk_cisr)
#endif /* LOG_ACRE_ISR_ENTER */

#ifndef LOG_ACRE_ISR_LEAVE
#define LOG_ACRE_ISR_LEAVE(ercd)
#endif /* LOG_ACRE_ISR_LEAVE */

ER_ID
acre_isr(const T_CISR *pk_cisr)
{
	ISRCB		*p_isrcb;
	ISRINIB		*p_isrinib;
	ISRQCB		*p_isr_queue;
	ATR			isratr;
	INTNO		intno;
	ISR			isr;
	PRI			isrpri;
	ER			ercd;

	LOG_ACRE_ISR_ENTER(pk_cisr);
	CHECK_TSKCTX_UNL();

	isratr = pk_cisr->isratr;
	intno = pk_cisr->intno;
	isr = pk_cisr->isr;
	isrpri = pk_cisr->isrpri;

	CHECK_VALIDATR(isratr, TARGET_ISRATR);
	CHECK_PAR(FUNC_ALIGN(isr));
	CHECK_PAR(FUNC_NONNULL(isr));
	CHECK_PAR(VALID_ISRPRI(isrpri));

	/*
	 *  割込み番号の検査
	 *
	 *  ★dcre（interrupt.c:324）はここでCHECK_PAR(VALID_INTNO_CREISR(intno))を
	 *  行うが，FMP3のVALID_INTNOは(prcid, intno)の2引数であり，呼出しコアの
	 *  情報を要求する．acre_isrの結果が呼出しコアによって変わってはならない
	 *  ので，範囲検査は行わず，cfgが生成するグローバルな適格intno表
	 *  （isr_queue_list）の二分探索だけでintnoを検証する．
	 *
	 *  この結果，
	 *    ・範囲外のintno
	 *    ・CFG_INTの無いintno
	 *    ・ENA_DYNISRされていないintno
	 *    ・DEF_INHが競合するintno
	 *  はいずれもE_OBJになる（dcreは1つ目をE_PARにするが，FMP3ではコア非依存に
	 *  区別する手段が存在しない．意図的な逸脱である）．
	 */
	p_isr_queue = search_isr_queue(intno);
	CHECK_OBJ(p_isr_queue != NULL);

	lock_cpu();
	acquire_glock();
	if (tnum_isr == tnum_sisr || queue_empty(&free_isrcb)) {
		ercd = E_NOID;
	}
	else {
		p_isrcb = ((ISRCB *) queue_delete_next(&free_isrcb));
		p_isrinib = (ISRINIB *)(p_isrcb->p_isrinib);
		p_isrinib->isratr = isratr;
		p_isrinib->exinf = pk_cisr->exinf;
		p_isrinib->p_isr_queue = p_isr_queue;
		p_isrinib->isr = isr;
		p_isrinib->isrpri = isrpri;

		/*
		 *  del_isrはrunningが0になるまで待ってからfree-listへ戻すので，
		 *  ここでのrunningは必ず0である．防御的に明示しておく．
		 */
		p_isrcb->running = 0U;

		/*  isrseqはenqueue_isrが採番する  */
		enqueue_isr(p_isr_queue, p_isrcb);
		ercd = ISRID(p_isrcb);
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_ACRE_ISR_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_acre_isr */

/*
 *  割込みサービスルーチンの削除
 */
#ifdef TOPPERS_del_isr

#ifndef LOG_DEL_ISR_ENTER
#define LOG_DEL_ISR_ENTER(isrid)
#endif /* LOG_DEL_ISR_ENTER */

#ifndef LOG_DEL_ISR_LEAVE
#define LOG_DEL_ISR_LEAVE(ercd)
#endif /* LOG_DEL_ISR_LEAVE */

ER
del_isr(ID isrid)
{
	ISRCB	*p_isrcb;
	ISRINIB	*p_isrinib;
	ER		ercd;

	LOG_DEL_ISR_ENTER(isrid);
	/*
	 *  ★CHECK_TSKCTX_UNL_MYSTATE（段階3aの訂正C）は使わない．訂正Cは
	 *  「del_*が待ちタスクを解除するのでディスパッチ判断が要る」場合の
	 *  規約であり，割込みサービスルーチンにはオブジェクト固有の待ち
	 *  キューが無く，del_isrは1つのタスクも待ち解除しない．したがって
	 *  ディスパッチ判断そのものが不要である（dcre interrupt.c:369も
	 *  CHECK_TSKCTX_UNL()である）．
	 */
	CHECK_TSKCTX_UNL();
	CHECK_ID(VALID_ISRID(isrid));
	p_isrcb = get_isrcb(isrid);

	lock_cpu();
	acquire_glock();
	if (p_isrcb->p_isrinib->isratr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (isrid <= tmax_sisrid) {
		ercd = E_OBJ;
	}
	else {
		/*
		 *  キューから外す．これ以降，call_isrの走査はこのISRCBを拾わない
		 *  （走査はジャイアントロックの下でキューを辿るため，本関数が
		 *  ロックを保持している間にキューが読まれることはない）．
		 */
		queue_delete(&(p_isrcb->isr_queue));

		/*
		 *  ★TA_NOEXSを「quiesceの前」に書く理由（訂正B）
		 *
		 *  下のquiesceループはジャイアントロックとCPUロックを解放して
		 *  待つ．その隙に別のタスクが同じisridへdel_isrを呼ぶと，
		 *  isratrがまだTA_NOEXSでなければE_NOEXSの枝に落ちず，すでに
		 *  外したキューエントリに対してqueue_deleteを再実行してしまう．
		 *  queue_deleteは削除済みエントリの古いp_prev/p_nextを書き換える
		 *  ので，キューが壊れる．先にTA_NOEXSを書けば，後続のdel_isrは
		 *  E_NOEXSを返して何もしない（オブジェクトはこの時点で論理的に
		 *  削除済みなので，これは正しい意味論である）．
		 *
		 *  ★段階3bの不変量「属性の読みはTA_NOEXSの書込みより前で完了
		 *  していること」は，ISRには適用されない．TA_NOEXSは((ATR)(-1))
		 *  ＝全ビットが1なので，TA_NOEXSを書いた後に属性をビット検査
		 *  （atr & TA_MBALLOC 等）すると必ず真になってしまう，という
		 *  のがあの不変量の理由である．割込みサービスルーチンのisratrは
		 *  ビット検査にもマスク比較にも使われず（== TA_NOEXSの同値比較と
		 *  代入だけである），call_isrはisratrを一切読まない．したがって
		 *  早く書いても誤判定する式が存在しない．
		 *
		 *  ★ISRID(p_isrcb)はp_isrinibポインタの差分で求めるので，
		 *  isratrがTA_NOEXSになってもcall_isr側のLOG_ISR_LEAVEは
		 *  正しいIDを得る．
		 */
		p_isrinib = (ISRINIB *)(p_isrcb->p_isrinib);
		p_isrinib->isratr = TA_NOEXS;

		/*
		 *  quiesce：他プロセッサで当該ISRの本体が実行中の間，待つ．
		 *
		 *  【保証する意味論】del_isrがE_OKを返した時点で，対象の割込み
		 *  サービスルーチンは実行中でなく，以後実行されない．したがって
		 *  del_isrの完了後は，exinfの指す資源を安全に解放できる．
		 *  （dcreは単一プロセッサなのでこの保証が構造的に成立していた．
		 *  FMP3では明示的に待たないと成立しない — Codex指摘 #2）
		 *
		 *  【待ち方】ジャイアントロックとCPUロックを解放し，
		 *  delay_for_interruptを挟んで取り直す．これはwait_tmout /
		 *  wait_tmout_ok（wait.c:126-131, 152-157）とまったく同じ5行で
		 *  あり，汎用カーネルに既にある正統な待ち方である．
		 *
		 *  【デッドロックが起こらないこと】
		 *  (a) runningのビットを立てるのも落とすのもcall_isrだけであり，
		 *      立てた直後にISR本体を呼び，戻った直後に落とす．ISR本体は
		 *      TOPPERSのISR規約により短時間で完了するので，待ち時間は
		 *      ISR本体の実行時間で有界である．
		 *  (b) 待っている間，本関数はジャイアントロックを保持していない．
		 *      したがって他コアのcall_isrはロックを取ってrunningを落とせる．
		 *  (c) 待っている間，本関数はCPUロックも保持していない（さらに
		 *      delay_for_interruptで割込みを受け付ける）．したがって
		 *      自コアの割込み処理やディスパッチが阻害されない．
		 *  (d) 自コアのビットが立っていることはない．割込みハンドラが
		 *      走っている間，そのコアではタスクが走らないので，del_isrを
		 *      呼んでいるタスクのコアで当該ISRが実行中ということは
		 *      ありえない．すなわち待つ相手は必ず他コアである．
		 *  (e) 待っている間に自コアが当該intnoの割込みを受けても，対象の
		 *      ISRCBはすでにキューから外れているので走査に拾われない．
		 *      自分で自分を待つ状態にはならない（★これは上のqueue_delete
		 *      を待機より前に置いていることに依存する）．
		 *
		 *  【本関数は自プロセッサのPCBを一切参照しない】ので，待機中に
		 *  このタスクが他プロセッサへマイグレートしても影響がない
		 *  （段階2のp_pcb-stale問題は本関数には存在しない）．
		 */
		while (p_isrcb->running != 0U) {
			release_glock();
			unlock_cpu();
			delay_for_interrupt();
			lock_cpu();
			acquire_glock();
		}

		/*  free-listはFIFO（queue_insert_prevで末尾へ．段階1で裁定済み）  */
		queue_insert_prev(&free_isrcb, &(p_isrcb->isr_queue));
		ercd = E_OK;
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_DEL_ISR_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_del_isr */

/*
 *  割込み管理機能の初期化
 */
#ifdef TOPPERS_intini
#ifndef OMIT_INITIALIZE_INTERRUPT

void
initialize_interrupt(PCB *p_my_pcb)
{
	uint_t			i;
	const INHINIB	*p_inhinib;
	const INTINIB	*p_intinib;

	for (i = 0; i < tnum_def_inhno; i++) {
		p_inhinib = &(inhinib_table[i]);
		if (p_inhinib->prcid == p_my_pcb->prcid) {
			define_inh(p_my_pcb, p_inhinib->inhno, p_inhinib->int_entry);
		}
	}
	for (i = 0; i < tnum_cfg_intno; i++) {
		p_intinib = &(intinib_table[i]);
		if (p_intinib->iprcid == p_my_pcb->prcid) {
			config_int(p_my_pcb, p_intinib->intno, p_intinib->intatr,
							p_intinib->intpri, p_intinib->affinity);
		}
	}
}

#endif /* OMIT_INITIALIZE_INTERRUPT */
#endif /* TOPPERS_intini */

/*
 *  割込みの禁止［NGKI3555］
 */
#ifdef TOPPERS_dis_int
#ifdef TOPPERS_SUPPORT_DIS_INT					/*［NGKI3093］*/

ER
dis_int(INTNO intno)
{
	bool_t	locked;
	ER		ercd;

	LOG_DIS_INT_ENTER(intno);

	locked = sense_lock();
	if (!locked) {
		lock_cpu();
	}
	if (!VALID_INTNO_DISINT(get_my_pcb()->prcid, intno)) {
		ercd = E_PAR;							/*［NGKI3083］［NGKI3087］*/
	}
	else if (check_intno_cfg(intno)) {
		disable_int(intno);						/*［NGKI3086］*/
		ercd = E_OK;
	}
	else {
		ercd = E_OBJ;							/*［NGKI3085］*/
	}
	if (!locked) {
		unlock_cpu();
	}

	LOG_DIS_INT_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_SUPPORT_DIS_INT */
#endif /* TOPPERS_dis_int */

/*
 *  割込みの許可［NGKI3556］
 */
#ifdef TOPPERS_ena_int
#ifdef TOPPERS_SUPPORT_ENA_INT					/*［NGKI3106］*/

ER
ena_int(INTNO intno)
{
	bool_t	locked;
	ER		ercd;

	LOG_ENA_INT_ENTER(intno);

	locked = sense_lock();
	if (!locked) {
		lock_cpu();
	}
	if (!VALID_INTNO_DISINT(get_my_pcb()->prcid, intno)) {
		ercd = E_PAR;							/*［NGKI3096］［NGKI3100］*/
	}
	else if (check_intno_cfg(intno)) {
		enable_int(intno);						/*［NGKI3099］*/
		ercd = E_OK;
	}
	else {
		ercd = E_OBJ;							/*［NGKI3098］*/
	}
	if (!locked) {
		unlock_cpu();
	}

	LOG_ENA_INT_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_SUPPORT_ENA_INT */
#endif /* TOPPERS_ena_int */

/*
 *  割込み要求のクリア［NGKI3920］
 */
#ifdef TOPPERS_clr_int
#ifdef TOPPERS_SUPPORT_CLR_INT					/*［NGKI3927］*/

ER
clr_int(INTNO intno)
{
	bool_t	locked;
	ER		ercd;

	LOG_CLR_INT_ENTER(intno);

	locked = sense_lock();
	if (!locked) {
		lock_cpu();
	}
	if (!VALID_INTNO_CLRINT(get_my_pcb()->prcid, intno)) {
		ercd = E_PAR;							/*［NGKI3921］［NGKI3930］*/
	}
	else if (check_intno_cfg(intno) && check_intno_clear(intno)) {
		clear_int(intno);						/*［NGKI3924］*/
		ercd = E_OK;
	}
	else {
		ercd = E_OBJ;							/*［NGKI3923］［NGKI3929］*/
	}
	if (!locked) {
		unlock_cpu();
	}

	LOG_CLR_INT_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_SUPPORT_CLR_INT */
#endif /* TOPPERS_clr_int */

/*
 *  割込みの要求［NGKI3932］
 */
#ifdef TOPPERS_ras_int
#ifdef TOPPERS_SUPPORT_RAS_INT					/*［NGKI3939］*/

ER
ras_int(INTNO intno)
{
	bool_t	locked;
	ER		ercd;

	LOG_RAS_INT_ENTER(intno);

	locked = sense_lock();
	if (!locked) {
		lock_cpu();
	}
	if (!VALID_INTNO_RASINT(get_my_pcb()->prcid, intno)) {
		ercd = E_PAR;							/*［NGKI3933］［NGKI3942］*/
	}
	else if (check_intno_cfg(intno) && check_intno_raise(intno)) {
		raise_int(intno);						/*［NGKI3936］*/
		ercd = E_OK;
	}
	else {
		ercd = E_OBJ;							/*［NGKI3935］［NGKI3941］*/
	}
	if (!locked) {
		unlock_cpu();
	}

	LOG_RAS_INT_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_SUPPORT_RAS_INT */
#endif /* TOPPERS_ras_int */

/*
 *  割込み要求のチェック［NGKI3944］
 */
#ifdef TOPPERS_prb_int
#ifdef TOPPERS_SUPPORT_PRB_INT					/*［NGKI3951］*/

ER_BOOL
prb_int(INTNO intno)
{
	bool_t	locked;
	ER_BOOL	ercd;

	LOG_PRB_INT_ENTER(intno);

	locked = sense_lock();
	if (!locked) {
		lock_cpu();
	}
	if (!VALID_INTNO_PRBINT(get_my_pcb()->prcid, intno)) {
		ercd = E_PAR;							/*［NGKI3945］［NGKI3952］*/
	}
	else if (check_intno_cfg(intno)) {
		ercd = (ER_BOOL) probe_int(intno);		/*［NGKI5214］［NGKI5215］*/
	}
	else {
		ercd = E_OBJ;							/*［NGKI3947］*/
	}
	if (!locked) {
		unlock_cpu();
	}

	LOG_PRB_INT_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_SUPPORT_PRB_INT */
#endif /* TOPPERS_prb_int */

/*
 *  割込み優先度マスクの変更［NGKI3107］
 */
#ifdef TOPPERS_chg_ipm

ER
chg_ipm(PRI intpri)
{
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_CHG_IPM_ENTER(intpri);
	CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk);		/*［NGKI3108］［NGKI3109］*/
	CHECK_PAR(VALID_INTPRI_CHGIPM(intpri));		/*［NGKI3113］［NGKI3114］*/

	lock_cpu();
  retry:
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_selftsk != p_my_pcb->p_schedtsk) {
		release_glock();
		dispatch();
		goto retry;
	}
	if (intpri == TIPM_ENAALL && p_my_pcb->enadsp) {
		/* set_dspflgは，割込み優先度マスクをTIPM_ENAALLにする．*/
		set_dspflg(p_my_pcb);
		if (p_selftsk->raster && p_selftsk->enater) {
			if (task_terminate(p_my_pcb, p_selftsk)) {
				exit_and_migrate(p_my_pcb, p_selftsk);
			}
			else {
				release_glock();
				exit_and_dispatch();
			}
			ercd = E_SYS;
			goto unlock_and_exit;
		}
		else {
			if (p_selftsk != p_my_pcb->p_schedtsk) {
				release_glock();
				dispatch();
				ercd = E_OK;
				goto unlock_and_exit;
			}
		}
		ercd = E_OK;
	}
	else {
		t_set_ipm(intpri);						/*［NGKI3111］*/
		p_my_pcb->dspflg = false;
		ercd = E_OK;
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu();

  error_exit:
	LOG_CHG_IPM_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_chg_ipm */

/*
 *  割込み優先度マスクの参照［NGKI3115］
 */
#ifdef TOPPERS_get_ipm

ER
get_ipm(PRI *p_intpri)
{
	ER		ercd;

	LOG_GET_IPM_ENTER(p_intpri);
	CHECK_TSKCTX_UNL();							/*［NGKI3116］［NGKI3117］*/

	lock_cpu();
	*p_intpri = t_get_ipm();					/*［NGKI3120］*/
	ercd = E_OK;
	unlock_cpu();

  error_exit:
	LOG_GET_IPM_LEAVE(ercd, p_intpri);
	return(ercd);
}

#endif /* TOPPERS_get_ipm */
