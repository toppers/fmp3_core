/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
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
 *  $Id$
 */

/*
 *		動的生成API（acre_isr/del_isr）のテスト
 *
 * 【テストの目的】
 *
 *	(A) ENA_DYNISR された intno のディスパッチがキュー方式になり，静的 ISR
 *	    だけのときの呼出し順序（isrpri 昇順・同一 isrpri は記述順）が
 *	    インライン連鎖のときと変わらないこと（ISR段階の訂正I の invariant）．
 *	(B) 静的 ISR と動的 ISR が isrpri 順に混在して呼ばれること．
 *	(C) 同一 isrpri の動的 ISR が acre した順（isrseq 昇順）に呼ばれること．
 *	(D) ★走査中に別コアが del_isr / acre_isr を行っても，取りこぼしも
 *	    二重実行も起きないこと（Codex 指摘 #1 のシナリオそのもの）．
 *	(E) ★del_isr が，他コアで実行中の ISR 本体の完了を待って戻ること
 *	    （quiesce．Codex 指摘 #2）．
 *	(F) エラー：E_OBJ（未 ENA_DYNISR の intno／範囲外の intno／静的 ISR の削除）・
 *	    E_PAR（isrpri 範囲外）・E_RSATR・E_ID・E_NOID・E_NOEXS・E_CTX．
 *	(G) 削除後は当該 ISR が呼ばれなくなること．
 *	(H) ★走査中にキューが完全に空になった後で acre_isr された ISR が，
 *	    同一の割込み起動の中で拾われること（isrseq 単調化の直接の回帰．
 *	    ISR段階の hardening 課題④）．
 *
 * 【この構成でしか実証できないこと／できないこと】
 *
 *	musca_b1 は割込み番号にプロセッサIDを符号化する（(prcid << 16) | irq）
 *	ため，同一の割込み番号を複数コアが受け付ける構成が作れない
 *	（affinity が2コアのクラスに CFG_INT を書くと E_RSATR）．したがって
 *	「同一キューを2コアが同時に走査する」ことは本テストでは実証できない．
 *	実証できるのは「PRC2 で走っている ISR を PRC1 のタスクが del する」
 *	（＝quiesce の本質）までである．PLIC/GIC のグローバル割込みを持つ
 *	ターゲット（polarfire_soc・zynq 系）では前者も到達可能である
 *	（ISR段階の訂正H）．
 *
 * 【ISR の中で syslog を伴う API を呼ばない理由】
 *
 *	本テストの ISR は別コアのタスクと同時に走る．ISR の中で check_point を
 *	呼ぶと，出力の順序がコア間で非決定的になり，行数の期待値が立たない．
 *	そのため ISR は volatile なグローバルへ記録するだけにし，判定はすべて
 *	タスクで行う（test_int2 は単一コアなので ISR 内 check_point でよい）．
 *
 * 【使用リソース】
 *
 *	TASK1: 中優先度タスク，TA_ACT属性（静的・PRC1．主体）
 *	TASK2: 高優先度タスク，TA_NULL属性（静的・PRC1．手順9 の横取り役）
 *	TASK3: 高優先度タスク，TA_ACT属性（静的・PRC2．手先）
 *	INTNO1（PRC1）: 静的 ISR_S4(isrpri 4)・ISR_S2(isrpri 2) ＋ ENA_DYNISR
 *	INTNO2（PRC2）: 静的 ISR なし ＋ ENA_DYNISR（★手順9 の完全ドレインに使う）
 *	INTNO3（PRC1）: 静的 ISR_SELF(isrpri 6) のみ．ENA_DYNISR しない・発火しない
 *	  （自立化用．2レンジ ID 検証／del_isr 静的拒否検証に使う．下記追記参照）
 *	AID_ISR(4): 動的スロット4個
 *
 * 【自立化（hardening パス後の追記）】
 *
 *	旧版は上記2検証に syssvc/serial.cfg 由来の ISR_SIO（別 cfg ファイルの
 *	静的 ISR）を使っていたが、ISR_SIO を除外する downstream 構成（esp32_s3）で
 *	test_dcre5 がビルドできない（ISR_SIO undeclared）ことが判明したため、
 *	本ファイル自身の3本目の静的 ISR（INTNO3／ISR_SELF、発火しない）に
 *	差し替えて自立化した（docs/qa-esp32s3-20260804-2.md Q-3 で確約した対応）。
 *	検証していた性質（動的ID > 静的ID の2レンジ分割、del_isr の静的拒否）は
 *	変わらず保たれている。クロスファイル性（別 cfg 由来の静的 ID でも成り立つ
 *	こと）は失われたが、それ自体は本テストの主目的ではなかった。
 *
 * 【チェックポイント】
 *
 *	PRC1（check_count[0]，TASK1）: 1..10 + check_finish(11)
 *	PRC2（check_count[1]，TASK3）: 1,2（出力は "Check point 2-1/2-2 passed."）
 *	  ＝ログ中の "Check point" 行は合計 13 本（check_finish 自身の1本を含む）
 *	  ★この本数は実測で確かめ，違っていたら実測値を正とする．
 *
 * 【実装メモ】
 *
 *	EXINF は include/t_stddef.h で `typedef intptr_t EXINF;` であるため，
 *	既に intptr_t である値を intptr_t へ再キャストするのは冗長．
 *	`(char) exinf` の単一キャストで警告なく縮小変換できる（-Wall で確認済み，
 *	-Wconversion は本ビルドで使っていない）．
 */

#include <kernel.h>
#include <t_syslog.h>
#include "syssvc/test_svc.h"
#include "kernel_cfg.h"
#include "test_dcre5.h"

/* DO NOT DELETE THIS LINE -- gentest depends on it. */

/*
 *  ISR の呼出し順序のログ
 */
static volatile uint_t	isr_log_cnt;
static volatile char	isr_log[ISR_LOG_SIZE];

/*
 *  走査中の del/acre（手順4）のハンドシェイク
 */
static volatile bool_t	hs_enable;		/*  ISR が待ち合わせるか  */
static volatile bool_t	hs_in_isr;		/*  ISR が走査の途中にいる  */
static volatile bool_t	hs_done;		/*  PRC2 側の del/acre が完了した  */
static volatile bool_t	hs_isr_timeout;	/*  ISR の待ちが上限に達した  */

/*
 *  quiesce の実証（手順5）
 */
static volatile bool_t	long_started;
static volatile bool_t	long_finished;
static volatile uint32_t spin_sink;

/*
 *  ISR 文脈からのサービスコール（手順7）
 */
static volatile ER		ctx_acre_ercd;
static volatile ER		ctx_del_ercd;

/*
 *  ★走査中の完全ドレイン→enqueue（手順9・hardening 課題④）
 */
static volatile bool_t	dr_a_started;	/*  A の本体に入った  */
static volatile bool_t	dr_a_finished;	/*  A の本体を抜けた  */
static volatile bool_t	dr_a_timeout;	/*  A の待ちが上限に達した  */
static volatile bool_t	dr_b_acred;		/*  TASK2 が B を生成し終えた  */
static volatile bool_t	dr_b_fired;		/*  B の本体が呼ばれた  */
static volatile ER_ID	dr_erid_b;		/*  B の ISRID（結果）  */

/*
 *  PRC2 のタスクへの指令
 */
static volatile uint_t	prc2_cmd;
static volatile ID		hs_del_isrid;	/*  走査中に削除する ISRID  */
static volatile ER_ID	hs_acre_erid;	/*  走査中に生成した ISRID（結果）  */
static volatile bool_t	prc2_quit;

/*
 *  TASK1 の test_start() 完了合図（実測で判明した起動レースの回避）
 *
 *  test_start() は check_count[] を全プロセッサ分ゼロクリアする．
 *  TASK3（PRC2）が独立カウンタ check_count[1] へ check_point_prc(1, 2) を
 *  打つタイミングが，TASK1（PRC1）の test_start() 呼び出しより先行すると，
 *  その直後に test_start() がカウンタを黙って 0 に巻き戻し，のちの
 *  check_point_prc(2, 2) が「Unexpected check point 2-2」で失敗する
 *  （実測で発生を確認．2コアが同時に起動レースするため非決定的）．
 *  TASK1 が test_start() を終えたことを合図する volatile フラグを設け，
 *  TASK3 はそれを待ってから最初の check_point_prc を打つ．
 */
static volatile bool_t	task1_ready;

/*
 *  ログの記録（ISR からのみ呼ばれる）
 */
static void
isr_log_put(char c)
{
	if (isr_log_cnt < ISR_LOG_SIZE) {
		isr_log[isr_log_cnt] = c;
		isr_log_cnt += 1U;
	}
}

/*
 *  ログの比較（タスクからのみ呼ばれる）
 */
static bool_t
isr_log_is(const char *expected)
{
	uint_t	i;

	for (i = 0U; expected[i] != '\0'; i++) {
		if (i >= isr_log_cnt || isr_log[i] != expected[i]) {
			return(false);
		}
	}
	return(isr_log_cnt == i);
}

/*
 *  静的 ISR（exinf がログに記録する文字）
 */
void
static_isr(EXINF exinf)
{
	intno1_clear();
	isr_log_put((char) exinf);
}

/*
 *  静的 ISR（自立化用・INTNO3・発火されない）
 *
 *  test_dcre5 の自立化（syssvc/serial.cfg 由来の ISR_SIO への依存の解消，
 *  docs/qa-esp32s3-20260804-2.md Q-3）のために追加した，本ファイル内の
 *  3本目の静的 ISR．2レンジ ID 検証と del_isr の静的拒否（E_OBJ）の検証に
 *  のみ使う．INTNO3 は ras_int されない（意図的に発火しない）ため，
 *  本体は呼ばれない（呼ばれても他の静的 ISR と同じ流儀で記録するだけ）．
 */
void
static_isr_self(EXINF exinf)
{
	intno3_clear();
	isr_log_put((char) exinf);
}

/*
 *  動的 ISR（exinf がログに記録する文字）
 *
 *  hs_enable が真のとき，'A' の ISR だけが「走査の途中」で待ち合わせる．
 */
void
dyn_isr(EXINF exinf)
{
	uint32_t	i;
	char		c = (char) exinf;

	intno1_clear();
	isr_log_put(c);

	if (hs_enable && c == 'A') {
		hs_in_isr = true;
		for (i = 0U; i < SPIN_LIMIT; i++) {
			if (hs_done) {
				break;
			}
		}
		if (!hs_done) {
			hs_isr_timeout = true;
		}
	}
}

/*
 *  quiesce 実証用の長い ISR（PRC2 で走る）
 */
void
long_isr(EXINF exinf)
{
	uint32_t	i;

	intno2_clear();
	long_started = true;
	for (i = 0U; i < LONG_ISR_SPIN; i++) {
		spin_sink = spin_sink + 1U;
	}
	long_finished = true;
	isr_log_put('L');
}

/*
 *  ISR 文脈からサービスコールを呼ぶ ISR（E_CTX の検査）
 */
void
ctx_isr(EXINF exinf)
{
	T_CISR	cisr;

	intno1_clear();
	isr_log_put('X');

	cisr.isratr = TA_NULL;
	cisr.exinf = (EXINF) 'Z';
	cisr.intno = INTNO1;
	cisr.isr = dyn_isr;
	cisr.isrpri = 8;
	ctx_acre_ercd = (ER) acre_isr(&cisr);
	ctx_del_ercd = del_isr(ISR_S2);
}

/*
 *  手順9 の A：キューに自分しかいない状態で走り，B が生成されるまで待つ
 *
 *  ★この待ちが「走査中にキューが完全に空になる」窓そのものである．
 *  待ちは有界で，上限に達したら dr_a_timeout を立てて抜ける（QEMU を
 *  ハングさせない）．
 */
void
drain_isr_a(EXINF exinf)
{
	uint32_t	i;

	intno2_clear();
	isr_log_put('A');
	dr_a_started = true;

	for (i = 0U; i < SPIN_LIMIT && !dr_b_acred; i++) {
	}
	if (!dr_b_acred) {
		dr_a_timeout = true;
	}
	dr_a_finished = true;
}

/*
 *  手順9 の B：空になったキューへ acre_isr された ISR
 *
 *  ★2度目の ras_int をしていないのにこれが呼ばれることが，
 *  isrseq の単調性（＝空キューでリセットしないこと）の証拠である．
 */
void
drain_isr_b(EXINF exinf)
{
	intno2_clear();
	isr_log_put('B');
	dr_b_fired = true;
}

/*
 *  PRC1 側の横取り役（手順9）
 *
 *  TASK1（MID）より高優先度なので act_tsk された瞬間に走るが，
 *  最初に dly_tsk で眠って TASK1 を del_isr の quiesce まで進ませる．
 *  起床後は quiesce のロック解放窓で TASK1 を横取りし，**空になった**
 *  INTNO2 のキューへ B を acre_isr する．
 */
void
task2(EXINF exinf)
{
	T_CISR	cisr;

	(void) dly_tsk(DRAIN_DELAY);

	cisr.isratr = TA_NULL;
	cisr.exinf = (EXINF) 'B';
	cisr.intno = INTNO2;
	cisr.isr = drain_isr_b;
	cisr.isrpri = 1;			/*  ★A と同じ isrpri（isrseq でしか区別できない）  */
	dr_erid_b = acre_isr(&cisr);
	dr_b_acred = true;
	ext_tsk();
}

/*
 *  PRC2 側の手先
 */
void
task3(EXINF exinf)
{
	uint32_t	i;
	T_CISR		cisr;

	/*
	 *  TASK1 の test_start() が check_count[] をゼロクリアし終えるのを
	 *  待ってから，PRC2 側の最初の check_point_prc を打つ（起動レース回避）．
	 */
	for (i = 0U; i < SPIN_LIMIT && !task1_ready; i++) {
	}
	check_assert(task1_ready);

	check_point_prc(1, 2);

	while (!prc2_quit) {
		if (prc2_cmd == CMD_HANDSHAKE) {
			/*
			 *  PRC1 の走査が 'A' の ISR に入るのを待ってから，
			 *  同一 isrpri の 'B' を削除し，'D' を生成する．
			 */
			for (i = 0U; i < SPIN_LIMIT && !hs_in_isr; i++) {
			}
			(void) del_isr(hs_del_isrid);
			cisr.isratr = TA_NULL;
			cisr.exinf = (EXINF) 'D';
			cisr.intno = INTNO1;
			cisr.isr = dyn_isr;
			cisr.isrpri = 3;
			hs_acre_erid = acre_isr(&cisr);
			hs_done = true;
			prc2_cmd = CMD_NONE;
		}
		else if (prc2_cmd == CMD_FIRE_LONG) {
			(void) ras_int(INTNO2);
			prc2_cmd = CMD_NONE;
		}
		else if (prc2_cmd == CMD_FIRE_DRAIN) {
			/*  手順9：INTNO2 を1度だけ発火する（★2度目は無い）  */
			(void) ras_int(INTNO2);
			prc2_cmd = CMD_NONE;
		}
		else {
			(void) dly_tsk(1U);
		}
	}

	check_point_prc(2, 2);
	ext_tsk();
}

void
task1(EXINF exinf)
{
	T_CISR		cisr;
	ER_ID		erid;
	ID			id_a, id_b, id_c, id_l, id_x;
	uint32_t	i;

	test_start(__FILE__);
	task1_ready = true;	/*  TASK3 の起動レース回避（実測で発覚，上のコメント参照）  */
	check_point(1);

	cisr.isratr = TA_NULL;
	cisr.intno = INTNO1;
	cisr.isr = dyn_isr;

	/*
	 *  1) 静的 ISR だけの呼出し順序（訂正I の invariant）
	 *
	 *  ENA_DYNISR された intno でも，静的 ISR は isrpri 昇順に呼ばれる．
	 *  記述順は S4→S2 だが，呼出しは S2（isrpri 2）→S4（isrpri 4）である．
	 */
	isr_log_cnt = 0U;
	check_ercd(ras_int(INTNO1), E_OK);
	check_assert(isr_log_is("24"));
	check_point(2);

	/*
	 *  2) 静的 ISR と動的 ISR の isrpri 順の混在
	 */
	cisr.exinf = (EXINF) 'a';	cisr.isrpri = 1;
	erid = acre_isr(&cisr);
	check_assert(erid > ISR_S2);	/*  2レンジ ISRID の直接検証  */
	check_assert(erid > ISR_S4);
	check_assert(erid > ISR_SELF);	/*  自立化：本ファイル内の3本目の静的ISR（旧・他cfgのISR_SIO）でも
					 *  動的ID > 静的ID の2レンジ分割が成り立つことを検証する
					 *  （クロスファイル性は薄れたが，分割の性質自体は保たれる）  */
	id_a = (ID) erid;

	cisr.exinf = (EXINF) 'b';	cisr.isrpri = 3;
	erid = acre_isr(&cisr);
	check_assert(erid > ISR_S4);
	id_b = (ID) erid;

	cisr.exinf = (EXINF) 'c';	cisr.isrpri = 5;
	erid = acre_isr(&cisr);
	check_assert(erid > ISR_S4);
	id_c = (ID) erid;

	isr_log_cnt = 0U;
	check_ercd(ras_int(INTNO1), E_OK);
	check_assert(isr_log_is("a2b4c"));

	check_ercd(del_isr(id_a), E_OK);
	check_ercd(del_isr(id_b), E_OK);
	check_ercd(del_isr(id_c), E_OK);
	check_point(3);

	/*
	 *  3) 同一 isrpri の動的 ISR は acre した順（isrseq 昇順）に呼ばれる
	 *
	 *  ★走査キーが isrpri だけだと，'A' を呼んだ後に「isrpri > 3」の
	 *    要素（'4'）へ飛んでしまい 'B'/'C' を取りこぼす．本手順は
	 *    ISR_KEY_GT の第2キー（isrseq）が生きていることの直接検証である．
	 *
	 *  ※本 control（negative control 2）が検証するのは「同一 isrpri の順序が
	 *    isrseq タイブレークに依存すること」であり，enqueue_isr の単調カウンタ化
	 *    （Task 5 のリセット撤去）が直した「走査中の完全ドレイン→enqueue で
	 *    cur 以降の isrpri 位置に入る新エントリが取りこぼされない」性質は
	 *    検証していない（★この性質は isrpri >= cur の位置に限る保証であり，
	 *    cur より高優先な新エントリはそもそも同一起動では拾われない仕様——
	 *    interrupt.c の enqueue_isr コメント／spec 訂正G追記を参照）．本テストの
	 *    INTNO1 キューは静的 ISR_S4/ISR_S2 が常駐し，走査中に完全に空には
	 *    ならないため（手順4のハンドシェイクでも enqueue 時点で A/C/S4 が
	 *    残存），旧リセット挙動（キューが空になったときだけ isrseq を 0 へ
	 *    戻す）でも本テストは通ってしまう．
	 *    ★この性質の回帰は hardening パス Task 3 で**手順9 に追加した**
	 *    （INTNO2＝静的 ISR ゼロの動的専用キューを使い，走査中に完全ドレイン
	 *    させてから同一 isrpri で acre する）．旧リセット分岐を戻すと手順9 が
	 *    倒れることを変異 control で実演済みである．本手順（手順3）は
	 *    引き続き「同一 isrpri の順序が isrseq タイブレークに依存すること」
	 *    だけを検査する．
	 */
	cisr.isrpri = 3;
	cisr.exinf = (EXINF) 'A';	erid = acre_isr(&cisr);	check_assert(erid > ISR_S4);
	id_a = (ID) erid;
	cisr.exinf = (EXINF) 'B';	erid = acre_isr(&cisr);	check_assert(erid > ISR_S4);
	id_b = (ID) erid;
	cisr.exinf = (EXINF) 'C';	erid = acre_isr(&cisr);	check_assert(erid > ISR_S4);
	id_c = (ID) erid;

	isr_log_cnt = 0U;
	check_ercd(ras_int(INTNO1), E_OK);
	check_assert(isr_log_is("2ABC4"));
	check_point(4);

	/*
	 *  4) ★走査中の del/acre（Codex 指摘 #1 のシナリオ）
	 *
	 *  PRC1 の走査が 'A'（isrpri 3）の中で待ち合わせている間に，PRC2 の
	 *  TASK3 が 'B' を削除し 'D'（isrpri 3・新しい isrseq）を生成する．
	 *  走査は 'A' の (3, seqA) より大きい最小の要素から再開するので，
	 *    ・'B' は削除済みなので呼ばれない
	 *    ・'C'（3, seqC > seqA）は取りこぼされない
	 *    ・'D'（3, seqD > seqC）は呼ばれる（spec §5 の例と同じ）
	 *    ・'A' は二重に呼ばれない
	 *  結果のログは "2ACD4" になる．
	 */
	hs_in_isr = false;
	hs_done = false;
	hs_isr_timeout = false;
	hs_del_isrid = id_b;
	hs_acre_erid = 0;
	hs_enable = true;
	prc2_cmd = CMD_HANDSHAKE;

	isr_log_cnt = 0U;
	check_ercd(ras_int(INTNO1), E_OK);

	/*  TASK3 の acre_isr の結果が確定するのを待つ  */
	for (i = 0U; i < SPIN_LIMIT && !hs_done; i++) {
	}
	hs_enable = false;
	check_assert(!hs_isr_timeout);
	check_assert(hs_done);
	check_assert(hs_acre_erid > ISR_S4);
	check_assert(isr_log_is("2ACD4"));

	check_ercd(del_isr(id_a), E_OK);
	check_ercd(del_isr(id_c), E_OK);
	check_ercd(del_isr((ID) hs_acre_erid), E_OK);
	check_ercd(del_isr(id_b), E_NOEXS);	/*  TASK3 が削除済み  */
	check_point(5);

	/*
	 *  5) ★quiesce の実証（Codex 指摘 #2）
	 *
	 *  PRC2 で long_isr が空回ししている最中に PRC1 から del_isr を呼ぶ．
	 *  del_isr は running が 0 になるまで戻らないので，戻った時点で
	 *  long_finished は必ず真である．quiesce が無ければ偽になる．
	 */
	cisr.intno = INTNO2;
	cisr.isr = long_isr;
	cisr.exinf = (EXINF) 'L';
	cisr.isrpri = 1;
	erid = acre_isr(&cisr);
	check_assert(erid > ISR_S4);
	id_l = (ID) erid;

	isr_log_cnt = 0U;
	long_started = false;
	long_finished = false;
	prc2_cmd = CMD_FIRE_LONG;

	for (i = 0U; i < SPIN_LIMIT && !long_started; i++) {
	}
	check_assert(long_started);
	check_ercd(del_isr(id_l), E_OK);
	check_assert(long_finished);		/*  ★quiesce の証拠  */
	check_assert(isr_log_is("L"));

	cisr.intno = INTNO1;
	cisr.isr = dyn_isr;
	check_point(6);

	/*
	 *  6) エラー
	 */
	cisr.exinf = (EXINF) 'z';
	cisr.isrpri = 1;

	cisr.intno = INTNO_UNOPTED;
	check_assert(acre_isr(&cisr) == E_OBJ);		/*  ENA_DYNISR されていない  */
	cisr.intno = INTNO_BAD;
	check_assert(acre_isr(&cisr) == E_OBJ);		/*  範囲外も E_OBJ（訂正A）  */
	cisr.intno = INTNO1;

	cisr.isrpri = TMIN_ISRPRI - 1;
	check_assert(acre_isr(&cisr) == E_PAR);
	cisr.isrpri = TMAX_ISRPRI + 1;
	check_assert(acre_isr(&cisr) == E_PAR);
	cisr.isrpri = 1;

	cisr.isratr = TA_NULL | 0x01U;
	check_assert(acre_isr(&cisr) == E_RSATR);
	cisr.isratr = TA_NULL;

	check_ercd(del_isr(0), E_ID);
	check_ercd(del_isr(TNUM_ISRID + 1), E_ID);
	check_ercd(del_isr(ISR_S2), E_OBJ);			/*  静的生成オブジェクト  */
	check_ercd(del_isr(ISR_SELF), E_OBJ);			/*  自前で acre していない静的ISRでも del_isr が
							 *  拒否することの検証（旧・ISR_SIO）  */

	/*  スロット4個を使い切る → E_NOID  */
	cisr.exinf = (EXINF) 'p';	erid = acre_isr(&cisr);	check_assert(erid > ISR_S4);
	id_a = (ID) erid;
	cisr.exinf = (EXINF) 'q';	erid = acre_isr(&cisr);	check_assert(erid > ISR_S4);
	id_b = (ID) erid;
	cisr.exinf = (EXINF) 'r';	erid = acre_isr(&cisr);	check_assert(erid > ISR_S4);
	id_c = (ID) erid;
	cisr.exinf = (EXINF) 's';	erid = acre_isr(&cisr);	check_assert(erid > ISR_S4);
	id_x = (ID) erid;
	check_assert(acre_isr(&cisr) == E_NOID);

	/*  空きが1個だけの状態で del → 再 acre（FIFO/LIFO 不問で決定的）  */
	check_ercd(del_isr(id_x), E_OK);
	cisr.exinf = (EXINF) 't';
	erid = acre_isr(&cisr);
	check_assert(erid == id_x);
	check_ercd(del_isr(id_x), E_OK);
	check_ercd(del_isr(id_x), E_NOEXS);
	check_ercd(del_isr(id_a), E_OK);
	check_ercd(del_isr(id_b), E_OK);
	check_ercd(del_isr(id_c), E_OK);
	check_point(7);

	/*
	 *  7) ISR 文脈からのサービスコールは E_CTX
	 */
	cisr.exinf = (EXINF) 'X';
	cisr.isr = ctx_isr;
	cisr.isrpri = 1;
	erid = acre_isr(&cisr);
	check_assert(erid > ISR_S4);
	id_x = (ID) erid;

	ctx_acre_ercd = E_OK;
	ctx_del_ercd = E_OK;
	isr_log_cnt = 0U;
	check_ercd(ras_int(INTNO1), E_OK);
	check_assert(isr_log_is("X24"));
	check_ercd(ctx_acre_ercd, E_CTX);
	check_ercd(ctx_del_ercd, E_CTX);
	check_ercd(del_isr(id_x), E_OK);
	cisr.isr = dyn_isr;
	check_point(8);

	/*
	 *  8) 削除後は呼ばれない（静的 ISR だけに戻る）
	 */
	isr_log_cnt = 0U;
	check_ercd(ras_int(INTNO1), E_OK);
	check_assert(isr_log_is("24"));
	check_point(9);

	/*
	 *  9) ★走査中にキューが完全に空になってからの acre_isr
	 *     （ISR段階の hardening 課題④。isrseq 単調化の直接の回帰）
	 *
	 *  INTNO2 は静的 ISR を1本も持たない動的専用の割込み番号である
	 *  （システム全体としての「静的 CRE_ISR が1本以上」という cfg の要求は
	 *  INTNO1 の ISR_S4/ISR_S2 が満たしている — kernel/interrupt.py の
	 *  ENA_DYNISR チェックはシステム全体の本数で判定する）．
	 *  そこへ A（isrpri 1）だけを生成して発火させると，PRC2 の call_isr は
	 *  A を呼んでいる間，キューに A しか持たない．A の実行中に PRC1 から
	 *  del_isr(A) すると，unlink の時点で **キューは完全に空** になる．
	 *  その空のキューへ TASK2 が B（★A と同じ isrpri 1）を acre_isr する．
	 *
	 *  【検査する性質】isrseq はキューの生存期間を通じて単調なので，B の
	 *  isrseq は A の isrseq より大きく，走査側の継続キー cur = (1, seqA) を
	 *  上回る．したがって B は **同じ割込み起動の中で** 呼ばれる
	 *  （2度目の ras_int をしていないのに 'B' がログに載る）．
	 *  旧実装（キューが空のとき isrseq を 0 へ戻す）では B の isrseq が 0 に
	 *  なって cur に負け，本起動では呼ばれなかった．ISR段階 Task 5 で
	 *  リセットを撤去した裁定の直接の回帰である．
	 *
	 *  【振り付け】
	 *   (1) TASK1(PRC1,MID): A を acre → TASK3 へ CMD_FIRE_DRAIN
	 *   (2) TASK3(PRC2,HIGH): ras_int(INTNO2) → PRC2 で call_isr → A を実行
	 *   (3) A の本体: 'A' を記録し dr_b_acred を待つ（有界）
	 *   (4) TASK1: dr_a_started を見てから act_tsk(TASK2)
	 *   (5) TASK2(PRC1,HIGH): 即座に dly_tsk(DRAIN_DELAY) で眠る
	 *   (6) TASK1: del_isr(A) → unlink（★ここでキューが空）→ TA_NOEXS → quiesce
	 *   (7) TASK2: 起床し，quiesce のロック解放窓で TASK1 を横取りして
	 *              B を acre_isr（空のキューへ enqueue）→ dr_b_acred
	 *   (8) A の本体: dr_b_acred を見て終了 → call_isr が走査を再決定し
	 *              B を **同一起動で** 実行 → 'B' を記録
	 *   (9) TASK1: quiesce 完了で del_isr(A) が E_OK を返す
	 *
	 *  【なぜ act_tsk を A の本体から呼ばないか】
	 *  A の本体から act_tsk(TASK2) すると，「TASK1 が del_isr に入る前に
	 *  TASK2 が走ってしまう」窓を排除できない．TASK1 自身が act_tsk して
	 *  から del_isr を呼び，TASK2 は最初に眠る形にすると，TASK2 が起きる
	 *  のは TASK1 が del_isr の中でロックを解放したとき（＝quiesce ループの
	 *  中）に限られる．★それでも「TASK2 が早すぎる」可能性は完全には
	 *  排除できないが，その場合キューは空にならず，変異 control（旧リセット
	 *  分岐の再導入）が倒れなくなる．すなわち control の成否が振り付けの
	 *  成否を兼ねている（本テストが空虚でないことの検出器である）．
	 */
	cisr.isratr = TA_NULL;
	cisr.intno = INTNO2;
	cisr.isr = drain_isr_a;
	cisr.exinf = (EXINF) 'A';
	cisr.isrpri = 1;
	erid = acre_isr(&cisr);
	check_assert(erid > ISR_S4);
	id_a = (ID) erid;

	isr_log_cnt = 0U;
	dr_a_started = false;
	dr_a_finished = false;
	dr_a_timeout = false;
	dr_b_acred = false;
	dr_b_fired = false;
	dr_erid_b = 0;

	prc2_cmd = CMD_FIRE_DRAIN;

	for (i = 0U; i < SPIN_LIMIT && !dr_a_started; i++) {
	}
	check_assert(dr_a_started);

	check_ercd(act_tsk(TASK2), E_OK);		/*  TASK2 は即眠るのですぐ戻る  */
	check_ercd(del_isr(id_a), E_OK);		/*  unlink→空→TA_NOEXS→quiesce  */
	check_assert(dr_a_finished);			/*  quiesce の帰結（手順5と同型）  */
	check_assert(!dr_a_timeout);
	check_assert(dr_b_acred);
	check_assert(dr_erid_b > ISR_S4);

	/*  ★B が同一起動の走査で呼ばれる（2度目の ras_int はしていない）  */
	for (i = 0U; i < SPIN_LIMIT && !dr_b_fired; i++) {
	}
	check_assert(dr_b_fired);
	check_assert(isr_log_is("AB"));

	check_ercd(del_isr((ID) dr_erid_b), E_OK);
	check_ercd(del_isr(id_a), E_NOEXS);
	check_point(10);

	prc2_quit = true;
	check_finish(11);
}
