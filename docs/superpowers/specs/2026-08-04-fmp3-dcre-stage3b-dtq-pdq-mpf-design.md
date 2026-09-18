# FMP3 動的生成API 段階3b（dtq/pdq/mpf）設計書

**Goal:** `feature/dynamic-creation` ブランチ上で、データキュー（`acre_dtq`/`del_dtq` +
`AID_DTQ`）・優先度データキュー（`acre_pdq`/`del_pdq` + `AID_PDQ`）・固定長メモリプール
（`acre_mpf`/`del_mpf` + `AID_MPF`）の動的生成を dcre 忠実移植で実現する。
これで dcre 標準の動的生成オブジェクトのうち ISR を除く全てが揃う。

**参照:**
- 段階1 spec（共通基盤・§2.3 残余ウィンドウの受容判断）・段階2 spec・段階3a spec
  （実行済み前例。特に 3a の per-object 型当てはめの型）
- dcre 原典: `/home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/kernel/`
  `dataqueue.{h,c}`・`pridataq.{h,c}`・`mempfix.{h,c}`（現行ソースが正、DIFF は参考。
  DIFF:1022,2658 が管理領域確保の型）
- 調査記録: 6オブジェクト構造調査（2026-08-04）

**スコープ外:** ISR（別計画・案B ハイブリッドで裁定済み、Codex レビュー指摘3件の解決を含む
専用 spec を起草する）。

---

## Global Constraints（段階1/2/3a から継承。実装中に再解釈しない）

1. ブランチ `feature/dynamic-creation`。main へマージしない。pristine 改変は
   `DIVERGENCE_MAP.md` に記録。
2. 段階3b = dtq/pdq/mpf の acre/del + AID 3種 + TA_MBALLOC 定義のみ。ISR を含めない。
3. API 面は dcre 標準のみ：`T_CDTQ`/`T_CPDQ`/`T_CMPF`（dcre include/kernel.h の該当定義と
   同一）。独自 API なし。`AID_*` はクラス外専用（E_RSATR）。
4. **プロセッサ親和なし**: 3オブジェクトの INIB に iprcid/affinity は存在しない
   （現物確認済み）。充填コードを書かないこと。
5. 検証 = F-1：両エンジン同時変更、cfg_equivalence exit 0 のみ合格。
6. CB はヒープ確保しない。free-list リンクは dtq/pdq = `swait_queue`、mpf = `wait_queue`
   の直接流用（dcre と同一）。FIFO（裁定済み・再議しない）。
7. **管理領域はメモリプール（malloc_mpk）から確保**し `TA_MBALLOC` を立てる（dcre 忠実）。
   mpf のブロック領域は mpf==NULL のとき自動確保で `TA_MEMALLOC`。
8. 汎用層不変。KERNEL_FCSRCS 不変。訂正C（del_* は CHECK_TSKCTX_UNL_MYSTATE）を踏襲。
9. rc=124 単独成功判定禁止・パイプ判定禁止・QEMU 個別実行＋pgrep。

---

## 1. プール裁定（段階1最終レビュー Important #1 の宿題 — ユーザ承認済み 2026-08-04）

**裁定: 受容継続。** 根拠 （malloc_mempool は bump allocator で count==0 のときだけ brk を戻す）:

1. **3b の管理領域（p_dtqmb/p_pdqmb/p_mpfmb）は構造的にウィンドウ・フリー**である
   （最終レビューで反証を試みた結果、この範囲に限り成立を確認 — 全参照箇所の
   網羅探索により，カーネルが **giant lock 下でのみ** CB 経由で参照することを確認済み）。
   del_* は待ちタスクを E_DLT で全解放（init_wait_queue、glock 下）してから TA_NOEXS 化し
   free_mpk するため、del 完了後は E_NOEXS ゲートが全 API を遮断し、「解放済み管理領域への
   残余参照」の経路が存在しない。
   段階1 §2.3 の自終了スタック窓（スタックは glock **外**で exiting コアが使う）とは
   質的に異なる（**この「質的に異なる」は管理領域3種に限った評価であり、mpf ブロック
   領域には及ばない — 次項参照**）。

2. **mpf ブロック領域は第2の受容済みウィンドウ**（最終レビューで特定）。
   `get_mpf`/`pget_mpf`/`tget_mpf` が返す生ポインタは、アプリケーションタスクが
   カーネルロックの**外**で直接読み書きする（μITRON/dcre の意味論そのもの）。
   `del_mpf` は貸出中のブロックがあっても領域ごと `free_mpk` する
   （`kernel/mempfix.c:340-345` のコメント「獲得済みのメモリブロックは返却されない
   まま領域ごと解放される（dcre意味論）」・`test/test_dcre4.c:477-478` で
   `pget_mpf` 未返却のまま `del_mpf` する経路を実際に演習して実証）。
   これは dcre 忠実・アプリケーション契約上の設計であり、E_NOEXS ゲートは
   `get_mpf`/`rel_mpf` 等の **API 呼出し**には効くが、既に得た **生ポインタへの
   stale 参照**には効かない（ゲートが守るのは CB 経由のアクセスのみ）。
   **成立条件（具体的な競合列）**: タスクA が `pget_mpf` でブロック `blk` を保持
   → タスクB が `del_mpf`（プールの count が 0 になり、bump allocator の brk が
   リセットされる）→ タスクB が `acre_dtq`（新しい p_dtqmb がリセット直後の同一
   バイト列に確保される）→ タスクA が（del_mpf の完了を知らないまま）`*blk` へ
   書き込む → DTQMB の管理領域が破壊される。
   段階1 §2.3 の自終了スタック窓と**同類**（同じ受容済みウィンドウの CLASS）の
   受容済みウィンドウであり、「質的に異なる」評価が当てはまるのは管理領域3種のみ。
   受容の裁定は段階1と同一の根拠（dcre/上流忠実・アプリ側の資源規律で回避可能）
   による。
3. 段階1で受容した自終了スタック窓そのものは 3b で変質しない。ただし 3b により
   プールの alloc/free 往復が増え、count==0（全割当解放 → brk リセット → 先頭から再利用）
   状態の出現頻度が変わるため、窓の成立機会の確率分布は変わる。これは事実として
   記録する（定量評価はしない）。
4. hardening（プール再利用の遅延・del_tsk の quiesce 等）は複雑さに見合わない。
   上流 FMP3 自身が ext_tsk の release_glock 後実行という同型の窓を持ち受容している。

## 2. API 定義

### 2.1 パケット型（dcre include/kernel.h の該当定義と同一。T_CMTX の後に置く）

```c
typedef struct t_cdtq {
	ATR		dtqatr;		/* データキュー属性 */
	uint_t	dtqcnt;		/* データキュー管理領域に格納できるデータ数 */
	void	*dtqmb;		/* データキュー管理領域の先頭番地 */
} T_CDTQ;

typedef struct t_cpdq {
	ATR		pdqatr;		/* 優先度データキュー属性 */
	uint_t	pdqcnt;		/* 優先度データキュー管理領域に格納できるデータ数 */
	PRI		maxdpri;	/* 優先度データキューに送信できるデータ優先度の最
						   大値 */
	void	*pdqmb;		/* 優先度データキュー管理領域の先頭番地 */
} T_CPDQ;

typedef struct t_cmpf {
	ATR		mpfatr;		/* 固定長メモリプール属性 */
	uint_t	blkcnt;		/* 獲得できる固定長メモリブロックの数 */
	uint_t	blksz;		/* 固定長メモリブロックのサイズ */
	MPF_T	*mpf;		/* 固定長メモリプール領域の先頭番地 */
	void	*mpfmb;		/* 固定長メモリプール管理領域の先頭番地 */
} T_CMPF;
```

### 2.2 サービスコール

```c
extern ER_ID	acre_dtq(const T_CDTQ *pk_cdtq) throw();
extern ER		del_dtq(ID dtqid) throw();
extern ER_ID	acre_pdq(const T_CPDQ *pk_cpdq) throw();
extern ER		del_pdq(ID pdqid) throw();
extern ER_ID	acre_mpf(const T_CMPF *pk_cmpf) throw();
extern ER		del_mpf(ID mpfid) throw();
```

TFN コードの既存有無は実装前確認（3a では6個とも既存だった）。

### 2.3 確保とエラー（dcre 準拠）

- `acre_dtq`: 検査（dtqatr、ユーザ供給 dtqmb != NULL のときは MB_ALIGN）→ E_NOID（free-list 空を先に）→
  dtqcnt != 0 && p_dtqmb == NULL なら `p_dtqmb = malloc_mpk(sizeof(DTQMB) * dtqcnt)`、NULL なら E_NOMEM、
  成功なら `dtqatr |= TA_MBALLOC`。dtqcnt==0 のとき管理領域は確保しない。
- `acre_pdq`: 同型（`sizeof(PDQMB) * pdqcnt`）。加えて `CHECK_PAR(VALID_DPRI(maxdpri))`
  （VALID_DPRI は FMP3 の `kernel/check.h` へ追加）。
- `acre_mpf`: **2段確保**。①mpf==NULL なら `malloc_mpk(ROUND_MPF_T(blksz) * blkcnt)` +
  `TA_MEMALLOC`（NULL なら E_NOMEM）②管理領域 `malloc_mpk(sizeof(MPFMB) * blkcnt)` +
  `TA_MBALLOC`（NULL なら **①で確保した分を free_mpk してから** E_NOMEM。巻き戻し条件は
  `pk_cmpf->mpf == NULL`）。検査は `CHECK_PAR(blkcnt != 0)`・`CHECK_PAR(blksz != 0)`・
  ユーザ供給 mpf != NULL のとき `CHECK_PAR(MPF_ALIGN(mpf))`・ユーザ供給 mpfmb != NULL のとき
  `CHECK_PAR(MB_ALIGN(p_mpfmb))`。INIBに格納する blksz は `ROUND_MPF_T(blksz)`。
- `del_*`: E_NOEXS → E_OBJ（静的）→ 両待ちキュー（dtq/pdq は swait+rwait、mpf は
  wait_queue のみ）を init_wait_queue で E_DLT 解放 → TA_MEMALLOC なら mpf ブロック領域 free_mpk →
  TA_MBALLOC なら管理領域 free_mpk
  - **訂正F**: `del_dtq` は `CHECK_ID(VALID_DTQID(dtqid))`（E_ID）を使う。dcre の del_dtq は
    `CHECK_PAR`（E_PAR）だが、これは dcre 自身の非一貫（del_pdq/del_mpf は dcre でも
    CHECK_ID）であり、3a の del_flg（訂正D）に続く2例目。FMP3 は CHECK_ID に統一する
    （意図的逸脱・DIVERGENCE_MAP 記録・上流報告候補 d の対象拡大）。
  - **属性を読んでから** TA_NOEXS に書き込む
    （TA_NOEXS は全ビット 1 なので、先に属性を読んでから書き込む必要がある） → free-list。
    滞留データ（dtq の未受信データ）は破棄される（dcre 意味論 — テストで実証）。

## 3. cfg 層

- `kernel_api.def` に AID 3行（`AID_DTQ .nodtq` 等、dcre と同一）。
- per-object テンプレート変更ゼロ（3a で3家族目まで実証済みの汎用枠組み）。
- 訂正E ガード・クラス外専用検査は自動適用。エラー回帰 cfg 6件
  （in-class E_RSATR ×3 + no-static E_OBJ ×3。no-static の include 構成は
  3a の serial.cfg 教訓を踏まえ現物確認して組む）。
- 管理された差分・positive control・実コンパイル検査（test_dcre_mix の cfg 拡張が媒体）・
  negative control は 3a と同型。

## 4. カーネル層

- `TA_MBALLOC` を kernel_impl.h へ（Task 2 に置くのは依存の衛生が理由。cfg は今日すでに
  DTQMB/PDQMB/MPFMB/MPF_T/COUNT_MPF_T を出力し、コンパイルが通っている。TA_MBALLOC は
  cfg の出力トークンではなく、Task 2 で Task 1 の結果に対する「後付けの属性セット」であり、
  cfg グラフへの入力ではない）。
- free_dtqcb/free_pdqcb/free_mpfcb、initialize_* の動的スロット節（master-only 内・
  非親和）、DTQID/PDQID/MPFID の2レンジ置換。
- **E_NOEXS 23関数の構造変更**: dataqueue.c 9・pridataq.c 8・mempfix.c 6。
  このうち **5関数は lock_cpu 前に p_xxxinib を読むため構造変更が必須**：
  - `fsnd_dtq`（dataqueue.c:486）: CHECK_ILUSE が p_dtqcb->p_dtqinib->dtqcnt を読む
  - `snd_pdq`（pridataq.c:301）: CHECK_PAR が p_pdqcb->p_pdqinib->maxdpri を読む
  - `psnd_pdq`（pridataq.c:357）: CHECK_PAR が p_pdqcb->p_pdqinib->maxdpri を読む
  - `tsnd_pdq`（pridataq.c:408）: CHECK_PAR が p_pdqcb->p_pdqinib->maxdpri を読む
  - `rel_mpf`（mempfix.c:325-330）: CHECK_PAR が p_mpfcb->p_mpfinib fields を読む
  
  → 防御の形は dcre の当該5関数の構造変更（ロック後への検査移動）をそのまま転写する。
- 配線: allfunc.h 6行・Makefile.kernel・kernel_rename.def + 再生成。

## 5. MP 安全性

- §1 の裁定どおり: **管理領域3種（p_dtqmb/p_pdqmb/p_mpfmb）はウィンドウ・フリー**
  （glock 下参照のみ + E_NOEXS ゲート — 最終レビューの反証で確認済み）。
  **mpf ブロック領域はウィンドウ・フリーではない**（第2の受容済みウィンドウ。
  get_mpf の生ポインタはロック外でアプリが使用、del_mpf は貸出中でも解放するため、
  E_NOEXS ゲートは stale ポインタ参照を防げない。§1 item 2 参照）。
  malloc_mempool は bump allocator で count==0 のときだけ brk を戻す。
- E_DLT 解放・ディスパッチ尻尾は 3a と同じ既存機構（init_wait_queue / ini_* の型）。
- acre_* の E_NOMEM 巻き戻し経路で free-list の一貫性が保たれること（TCB 相当の
  CB は E_NOMEM 時に pop しない — acre_tsk と同じ「確保成功後に pop」順序。
  dcre 現物の順序を実装前確認§8-4 で確定）。

## 6. テスト（test_dcre4、musca_b1-2core、AID_DTQ(2)/AID_PDQ(1)/AID_MPF(1) + DEF_MPK）

**テスト編成**: `TASK1`=MID/PRC1、`TASK2`=HIGH/PRC1、`TASK3`=HIGH/PRC2。
**静的テストオブジェクト**: `DTQ1`（データキュー）・`PDQ1`（優先度データキュー）・
`MPF1`（固定長メモリプール）。`MPK_SIZE` は段階1 + 動的スロット用追加サイズ
（Task 6 で確定）。

**テスト項目**:

1. acre_dtq → snd/rcv の実通信（データ整合）→ del → E_NOEXS
2. **E_NOMEM 実証**: プールに入らない dtqcnt で acre_dtq → E_NOMEM
   （スロットは残っている状態で — E_NOID との順序の実証）
3. **送信待ち E_DLT**（満杯 dtq に snd_dtq で待たせて del）と
   **受信待ち E_DLT**（空 dtq に rcv_dtq で待たせて del）の両方
4. **滞留データ破棄**: データを入れたまま del → 再 acre → 空であることを確認
5. pdq: acre → 優先度順配送の確認 → del
6. mpf: acre（mpf=NULL 自動確保）→ get/rel → ブロック内容の書込み読出し → del、
   mpf の E_NOMEM（ブロック領域が入らない blksz*blkcnt）
7. 枯渇 E_NOID・静的 E_OBJ・決定形の同一 ID 再 acre
8. カーネル変異 negative control（del_dtq の free-list 返却）
   
   **プール再利用の実証**: malloc_mempool の bump 特性（count==0 で brk リセット）により
   連続した acre/del サイクルで同一メモリ領域が再利用されることを確認。
   
9. 非退行: test_dcre1/2/3・test_dcre_mix・test_int2

## 7. 統治

- **7タスク構成**（実装前確認 → cfg → dtq → pdq → mpf → test_dcre4 → 最終回帰。
  段階2 hardening のような別枠が無いため）。
- 全 pristine 編集は DIVERGENCE_MAP。上流報告候補は従来4件のまま。

## 8. 実装前確認リスト（計画 Task 1 で現物確認）

1. T_CDTQ/T_CPDQ/T_CMPF の dcre 現物とのバイト照合（§2.1 は要旨）
2. acre_dtq の dtqcnt==0 分岐とユーザ供給 dtqmb/pdqmb/mpfmb の dcre での扱い
   （NULL 必須か受理か。受理なら TA_MBALLOC を立てない分岐がある）
3. acre_mpf の2段確保の巻き戻し（②失敗時に①を free するか）の dcre 現物確認
4. acre_* の「CB pop と確保の順序」（E_NOMEM 時に free-list 不変か）
5. TFN 6コードの既存有無
6. DTQID/PDQID/MPFID マクロの現行実装（3a と同じく既存 inib 方式の置換のはず）
7. DTQMB/PDQMB/MPFMB 型と COUNT/ROUND 系マクロの FMP3 既存有無
   （段階1 COUNT_MB_T の教訓 — cfg が出力するなら定義側も要確認）
8. no-static 回帰 cfg の include 構成（隠れ静的インスタンスの有無を全 syssvc/target で確認）

---

## 9. 実装前確認の結果（2026-08-04 実測）

### 確認概要

**ゲート条件**: すべてクリア ✅

1. iprcid/affinity/p_pcb が INIB/CB に存在しない
2. initialize_dataqueue/initialize_pridataq/initialize_mempfix がマスタプロセッサ限定
3. CB の先頭フィールドが QUEUE（型 punning OK）
4. dcre の del_* が TA_NOEXS 書込みを free_mpk より後に実施

**訂正適用状況**: 訂正 A〜H をすべて spec に反映済み

### TFN コード（既存値）

| コード | 値 |
|--------|-----|
| TFN_ACRE_DTQ | -196 |
| TFN_ACRE_PDQ | -197 |
| TFN_ACRE_MPF | -201 |
| TFN_DEL_DTQ | -212 |
| TFN_DEL_PDQ | -213 |
| TFN_DEL_MPF | -217 |

### DTQID/PDQID/MPFID の現行実装

- **マクロ形式**: inib ポインタ差分式（既存・2レンジ化済み）
- **CB テーブル**: ポインタ表（`DTQCB *const p_dtqcb_table[]` など）
- **tnum_* 位置**: `.c` ファイルで定義（`.h` への移設が必須）
- **queue.h include**: 3つの `.h` ファイル `:51` に既存

### dcre 転写元の行範囲（後続 Task 参照用）

| 関数 | ファイル | 範囲 |
|------|---------|------|
| acre_dtq | dataqueue.c | 339-397 |
| del_dtq | dataqueue.c | 402-444 |
| acre_pdq | pridataq.c | 316-379 |
| del_pdq | pridataq.c | 384-426 |
| acre_mpf | mempfix.c | 199-279 |
| del_mpf | mempfix.c | 284-328 |

### E_NOEXS 23 関数と構造変更 5 関数

**23 関数の内訳**:
- dataqueue.c: snd_dtq(313) psnd_dtq(367) tsnd_dtq(417) fsnd_dtq(474) rcv_dtq(521) prcv_dtq(578) trcv_dtq(622) ini_dtq(683) ref_dtq(727)
- pridataq.c: snd_pdq(290) psnd_pdq(345) tsnd_pdq(396) rcv_pdq(455) prcv_pdq(513) trcv_pdq(557) ini_pdq(619) ref_pdq(663)
- mempfix.c: get_mpf(173) pget_mpf(222) tget_mpf(257) rel_mpf(311) ini_mpf(370) ref_mpf(413)

**構造変更が必要な 5 関数**（lock_cpu より前に p_xxxinib を読む）:

| 関数 | ファイル | 行 | 読む対象 |
|------|---------|-----|----------|
| fsnd_dtq | dataqueue.c | 485 | p_dtqcb->p_dtqinib->dtqcnt (CHECK_ILUSE) |
| snd_pdq | pridataq.c | 301 | p_pdqcb->p_pdqinib->maxdpri (CHECK_PAR) |
| psnd_pdq | pridataq.c | 357 | p_pdqcb->p_pdqinib->maxdpri (CHECK_PAR) |
| tsnd_pdq | pridataq.c | 408 | p_pdqcb->p_pdqinib->maxdpri (CHECK_PAR) |
| rel_mpf | mempfix.c | 324-330 | p_mpfcb->p_mpfinib fields (CHECK_PAR ×5) |

**FMP3 固有関数**: 段階2 の `msta_cyc`/`msta_alm` に相当するものは dtq/pdq/mpf には **無し**（上流に先例なし）

### acre_*/del_* の順序確認

**acre_* の確保順序**（dcre 忠実）:
1. 検査
2. `lock_cpu()`
3. **E_NOID**（tnum_* == 0 || queue_empty） ← 最初
4. 管理領域の malloc_mpk
5. NULL なら E_NOMEM → **E_NOMEM のとき free-list は 1 要素も減らない**（段階1 acre_tsk と同型）
6. 確保成功後に CB pop

**del_* の属性読み書き順序**（TA_NOEXS は全ビット 1）:
1. E_NOEXS チェック
2. E_OBJ チェック（静的）
3. 待ちキュー E_DLT 解放
4. **属性を読む** （TA_MBALLOC/TA_MEMALLOC ビット判定）
5. free_mpk
6. **属性を TA_NOEXS に書き込む** ← 読みが先

### no-static 回帰 cfg 構成

**静的 CRE_DTQ/PDQ/MPF を含む cfg**（7 本）:
- sample/sample1.cfg（CRE_DTQ のみ）
- target/polarfire_soc_kit_gcc/softconsole/sample1/sample1.cfg
- test/perf2.cfg
- test/test_dtq1.cfg
- test/test_mpf1.cfg
- test/test_notify1.cfg
- test/test_pdq1.cfg

**含まない cfg**:
- syssvc/*.cfg（すべて）
- test/test_common1.cfg

**判定**: 段階3a serial.cfg 教訓（回避が必要）は **dtq/pdq/mpf では不要**。3 件とも `INCLUDE("test/test_common1.cfg")` で可（flg/mtx 型）。Task 2 Step 12 で E_OBJ が実測確認できれば確定。

### cfg 共通枠組みの per-object 変更確認

**DataqueueObject/PridataqObject/MempfixObject** の実装:
- 共通枠組み（KernelObject）を継承
- `prepare`・`generateInib` のみカスタマイズ
- `inibList`/`omit_cb`/`generateData` は既定のまま（変更ゼロ）

**判定**: per-object テンプレートの変更ゼロ。編集したくなったら共通枠組みの理解誤りの合図。

### initialize_* 関数の確認

**3 関数すべてが master-only**:
```c
if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) { ... }
```

**INIB のプロセッサ親和なし**: iprcid/affinity/p_pcb なし

**新規の解除機構不要**: 既存 `init_wait_queue` (wait.c:215-228 MP 対応済み) を使用

**判定**: 静的ループ境界と動的スロット節を追加するのみ。プロセッサフィルタ新設なし。

### 型・マクロの FMP3 既存有無確認

**すべて既存** ✅:
- DTQMB/PDQMB/MPFMB 構造体
- MPF_T・COUNT_MPF_T・ROUND_MPF_T マクロ
- MPF_ALIGN・MB_ALIGN・INDEX_NULL・INDEX_ALLOC マクロ
- TA_NOEXS・TA_MEMALLOC 属性値

**TA_MBALLOC**: cfg 出力トークンではない。Task 2 置き（依存衛生）。

### malloc_mempool の bump 特性

**根拠**: count==0 のときだけ brk を戻す（段階1 §1 裁定の帰結）

**3点での記載**: §1 根拠追記・§5 MP 安全性に明記・§6 テスト 8 番項目に実証追加

---

**実装前確認状況**: **PASS** ✅  
**ゲート条件**: **すべてクリア** ✅  
**訂正状況**: **A〜H すべて反映完了** ✅  
**後続 Task への記録**: 本セクション 9 が基盤
