# FMP3 動的 ISR 生成（acre_isr/del_isr）設計書

**Goal:** `feature/dynamic-creation` ブランチ上で、割込みサービスルーチンの動的生成
（`acre_isr`/`del_isr` + 予約 API）を**案B: ハイブリッド方式**で実現する。
静的 ISR のみの intno は現行の平坦なインライン連鎖を無変更維持し、明示的に opt-in した
intno のみランタイムキュー方式に切り替える。

**方式裁定の経緯:**
- 案B（ハイブリッド＋API 拡張）はユーザ裁定済み（2026-08-04。「ISR のコア指定に関しては
  API の拡張ないし追加を許容する」）。
- Codex 外部レビュー（2026-08-04、3件とも独立検証で真と判定）の指摘を本 spec で解決する:
  ①走査再開位置の安定キー ②del_isr 後の実行中 ISR の寿命意味論 ③intno 検証のコア非依存化。
- 背景事実の詳細は `.superpowers/sdd/isr-policy-for-review.md`（方針書）と
  `.superpowers/sdd/stage3-isr-design-notes.md`（設計メモ）。

**参照:**
- dcre 原典: `/home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/kernel/interrupt.{h,c,trb}`
  （ISRCB・isr_queue・call_isr・enqueue_isr・search_isr_queue の型。現行ソースが正）
- FMP3 現行: `kernel/interrupt.trb:411-477`（インライン連鎖生成）、
  `kernel/interrupt.{h,c}`、`kernel/time_event.c:709-768` / `kernel/cyclic.c:518-548`
  （割込み文脈での giant lock 取得の先例）

---

## Global Constraints

1. ブランチ `feature/dynamic-creation`。pristine 改変は DIVERGENCE_MAP 記録。
2. **既存構成への影響ゼロが本方式の存在理由**: opt-in の無い構成では、生成コード・
   実行時挙動・管理された差分がすべて不変であること（恒常出力の追加は AID 系の
   最小限に留め、許容リスト検査で固定する）。
3. acre_isr/del_isr/T_CISR は dcre 標準のシグネチャ・フィールドを維持
   （isratr, exinf, intno, isr, isrpri）。**コア引数は追加しない** — 動的 ISR の実行コアは
   対象 intno のクラス affinity に従う（コア指定は「どの intno を選ぶか」で表現される）。
4. 検証 = F-1（両エンジン同時・equivalence）+ 実コンパイル + QEMU 機能テスト。
5. FIFO・訂正C（MYSTATE）等、段階1〜3で確立した規約を踏襲。
6. rc=124 単独成功判定禁止・パイプ判定禁止・QEMU 個別実行＋pgrep。

---

## 1. API 定義

### 1.1 予約 API（案B-2 — 推奨で確定。B-1 との比較は §7）

```
AID_ISR .noisr                 ← dcre と同一（グローバル CB プール予約・クラス外専用）
ENA_DYNISR .intno              ← FMP3 拡張（intno を動的 ISR 対象として opt-in）
```

- `ENA_DYNISR(intno)` は **CFG_INT(intno) と同一クラスの囲みの中**に書く
  （CRE_ISR と CFG_INT の同一クラス規則と同じ。E_RSATR で検査）。
- ENA_DYNISR された intno は、静的 CRE_ISR の有無にかかわらずランタイムキュー方式の
  ディスパッチ（§3）に切り替わる。ENA_DYNISR の無い intno は現行のインライン連鎖のまま。
- ENA_DYNISR に対応する CFG_INT が無い場合はエラー（dcre の isr_queue 生成前提と同型）。
- DEF_INH が競合する intno への ENA_DYNISR はエラー（dcre interrupt.trb:263-270 と同じ除外則）。

### 1.2 サービスコール（dcre 標準）

```c
extern ER_ID	acre_isr(const T_CISR *pk_cisr) throw();
extern ER		del_isr(ID isrid) throw();
```

T_CISR は dcre include/kernel.h の定義そのまま（isratr, exinf, intno, isr, isrpri）。
TFN_ACRE_ISR(-204)/TFN_DEL_ISR(-220) は既存（確認済み）。

### 1.3 エラー（dcre 準拠 + Codex #3 対応 + 訂正A）

**訂正A：`VALID_INTNO_CREISR(intno)` は FMP3 に存在しない。acre_isr に intno の範囲検査を置かない。**

spec §1.3 は `acre_isr` の検査として `VALID_INTNO_CREISR(intno)` を挙げるが、
**FMP3 にこのマクロは無い**（現物確認：`kernel/interrupt.c:114-128` にあるのは
`VALID_INTNO_DISINT`/`VALID_INTNO_CLRINT`/`VALID_INTNO_RASINT`/`VALID_INTNO_PRBINT` の 4 つ
だけで、いずれも **`(prcid, intno)` の 2 引数**である）。FMP3 の `VALID_INTNO` は
`arch/arm_m_gcc/common/core_kernel_impl.h:761` のように `(prcid, intno)` を取る形で
定義されており、**呼出しコアの情報を引数に要求する**。
arm_m の実装は prcid を実際には使わないが、それはターゲット実装の偶然であって
移植性の根拠にならない（Codex #3 の指摘そのものである）。

→ **`acre_isr` は intno の範囲検査を持たない。** intno の検証は
`search_isr_queue(intno)`（cfg が生成するグローバル適格 intno 表 `isr_queue_list[]` の
二分探索）**のみ**で行い、
- 範囲外の intno
- `CFG_INT` の無い intno
- `CFG_INT` はあるが `ENA_DYNISR` されていない intno
- `DEF_INH` が競合している intno

の 4 つはすべて **`E_OBJ`** になる。dcre は範囲外を `E_PAR`、表に無いものを `E_OBJ` と
区別するが、FMP3 では**コア非依存に区別する手段が存在しない**ため区別しない。
これは dcre からの**意図的な逸脱**であり、上流報告候補ではない（FMP3 固有の構造差）。

★`CHECK_VALIDATR(isratr, TARGET_ISRATR)`・`CHECK_PAR(FUNC_ALIGN(isr))`・
`CHECK_PAR(FUNC_NONNULL(isr))`・`CHECK_PAR(VALID_ISRPRI(isrpri))` は**残す**
（いずれもコア非依存である）。

- `acre_isr`: CHECK_VALIDATR(isratr, TARGET_ISRATR)、FUNC_ALIGN/NONNULL(isr)、
  VALID_ISRPRI(isrpri)、`search_isr_queue(intno)` による適格性検査（上記）。
  free-list 空は E_NOID。
- `del_isr`: E_NOEXS（TA_NOEXS）→ E_OBJ（静的 = isrid <= tmax_sisrid）→ 成功（§4 の
  quiesce 意味論）。

## 2. cfg 層

- `AID_ISR(n)`: グローバル ISRCB プール（`_kernel_aisrinib_table[n]`・予約 ISRCB・
  `_kernel_tmax_sisrid` — 段階1〜3a と同じ KernelObject 枠組み。ISR には従来
  ランタイムオブジェクトが無いため、**ISRCB/ISRINIB 型の新設**（§3）とセット）。
- `ENA_DYNISR(intno)` の効果（両エンジン同時実装）:
  1. 当該 intno の生成ディスパッチを、インライン連鎖から
     `_kernel_inthdr_<intno>(){ _kernel_call_isr(&_kernel_isr_queue_table[i]); }` へ切替
  2. `_kernel_isr_queue_table[]`（**`ISRQCB` 配列 — 訂正C。QUEUE 配列ではない**）と、
     intno 昇順ソート済みの
     `_kernel_isr_queue_list[]`（{intno, queue*} 対 — **グローバル適格 intno 表**、
     acre_isr の二分探索対象）を生成
  3. 当該 intno の静的 CRE_ISR は `_kernel_isrorder_table[]` 経由で初期化時に
     isrpri 順挿入（dcre interrupt.trb:338-346 の型。**訂正I: isrid 昇順で生成する**）
  - クラスの affinity が複数コアに跨る intno では、per-prcid の DEF_INH 生成
    （interrupt.trb:411-441 の既存機構）を維持 — 同一キューを複数コアが走査しうる
    （§5 の同期設計が前提）。
- opt-in 無し構成の生成物: AID_ISR 関連の恒常出力（TNUM_SISRID 等、段階1〜3a と同型）
  のみ許容リストに追加。ENA_DYNISR 無しなら isr_queue_* は一切生成しない。
  **ただし訂正D参照：この「同型」は規模が同型という意味ではない。**
- エラー回帰: AID_ISR in-class / ENA_DYNISR のクラス不一致 / CFG_INT 無し intno への
  ENA_DYNISR / DEF_INH 競合 / no-static（訂正E ガード）。

---

**訂正D：AID_ISR の恒常出力は段階1〜3a より大きい。受容した上で数値で記録する。**

spec §2 は「opt-in 無し構成の生成物: AID_ISR 関連の恒常出力（TNUM_SISRID 等、
段階1〜3a と同型）のみ許容リストに追加」と書くが、**「同型」ではあっても規模が違う**。

現物確認：`cfg_py/pass2.py:181-186`（Ruby は `cfg/pass2.rb:170`）は
**`kernel_api.def` に登録されている全 API について `cfgData[api] = {}` を先に作る**。
したがって `kernel.py:142` の `has_aid = self.aidapi in cfgData` は、
**`.cfg` に `AID_ISR` が 1 個も書かれていなくても `AID_ISR .noisr` を
`kernel_api.def` に足した時点で恒真になる**。

ISR には従来ランタイムオブジェクトが無かったため、この結果として
**静的 `CRE_ISR` を 1 個以上持つ全構成**に次が恒常的に加わる：

- `const ID _kernel_tmax_isrid` / `#define TNUM_SISRID` / `const ID _kernel_tmax_sisrid`
- `const ISRINIB _kernel_isrinib_table[TNUM_SISRID] = { ... }`（ROM）
- `static ISRCB _kernel_isrcb_<ISRID>;` ×静的 ISR 数（RAM）
- `ISRCB *const _kernel_p_isrcb_table[TNUM_ISRID] = { ... }`（ROM）
- `const ID _kernel_isrorder_table[TNUM_SISRID] = { ... }`（ROM）
- `TOPPERS_EMPTY_LABEL(ISRINIB, _kernel_aisrinib_table);`
- `const uint_t _kernel_tnum_isr_queue = 0;` +
  `TOPPERS_EMPTY_LABEL(const ISR_ENTRY, _kernel_isr_queue_list);` +
  `TOPPERS_EMPTY_LABEL(ISRQCB, _kernel_isr_queue_table);`
- 起動時の `_kernel_initialize_isr(p_my_pcb);` 呼出し 1 個

★**`syssvc` 経由の `CRE_ISR(ISR_SIO)`（`target/musca_b1_gcc/target_serial.cfg:11`・
`arch/riscv_gcc/polarfire_soc/chip_serial.cfg:12` ほか）があるため、
実質すべての構成が該当する**（現物確認済み）。

→ **これを受容する。** 理由は 2 つあり、どちらも真である：
1. **ディスパッチ経路は不変である。** `_kernel_inthdr_<intno>` の本体はバイト単位で
   変わらないので、Global Constraint 3 が守る「実行時挙動」と割込みレイテンシは変わらない。
   加わるのは**データと起動時の O(N) ループ 1 本**だけである。
2. 回避しようとすると、`interrupt.c` が参照するシンボル群を条件付きで生成／
   条件付きでコンパイルする必要があり、`tmax_isrid` と `TNUM_ISRID` の意味が
   構成によって食い違う（`tmax_isrid != TMIN_ISRID + TNUM_ISRID - 1` になる）
   などの不整合を招く。ALLFUNC ビルドでは `interrupt.c` の全区画が
   `initialize_interrupt` 等と同じ翻訳単位に入るため、シンボルを出さない選択肢は
   リンクエラーになる。

→ ただし**受容の代償を推測でなく数値で残す**：Task 2 Step 8 で、既存構成
（`build/musca_b1-2core` = sample1）の ELF について `size` の `text`/`data`/`bss` を
**変更前後で実測**し、差分バイト数を確認結果表と台帳に記録する。

**Global Constraint 2 のスコープの明確化（訂正D）：「既存構成への影響ゼロ」は
ディスパッチ経路・実行時挙動のスコープであり、恒常出力（生成データの規模）の
スコープではない。** 恒常出力の増加は本訂正で受容し、ROM 実測で追跡する。

---

**訂正E：`ENA_DYNISR` と `AID_ISR` の相互前提が spec に無い。ガードを 2 つ足す。**

spec §2 は 2 つの API の意味は定めているが、**片方だけ書いたときに何が起きるかを
定めていない**。汎用枠組みのガード（`kernel.py:157-161`：`AID_x > 0` なのに静的
`CRE_x` が 0 個なら E_OBJ）に加え、ISR 固有のガードが 2 つ要る：

1. **`AID_ISR(n>0)` は `ENA_DYNISR` を 1 個以上要求する（E_OBJ）。**
   適格 intno が 1 つも無ければ `acre_isr` は必ず `E_OBJ` で失敗し、
   予約した ISRCB が永久に死蔵される。cfg で弾けるものは cfg で弾く。
2. **`ENA_DYNISR` は静的 `CRE_ISR` を 1 個以上要求する（E_OBJ）。**
   汎用枠組みは `initialize_isr` の登録（`kernel.py:257-258`）を
   `len(cfgData["CRE_ISR"]) > 0` に条件づけている。静的 ISR が 0 個だと
   `initialize_isr` が登録されず、**`isr_queue_table` が未初期化のまま
   `call_isr` が走る**（`QUEUE` の `p_next`/`p_prev` が BSS の 0 のまま＝未定義動作）。

★ガード 2 は「同一 intno に静的 ISR が必要」ではなく「システム内のどこかに 1 個」でよい。
`syssvc` の `CRE_ISR(ISR_SIO)` があるため実運用では自動的に満たされる。
★ガード 1・2 とも汎用枠組み（`kernel/kernel.py`・`kernel/kernel.trb`）ではなく
**`kernel/interrupt.py`・`kernel/interrupt.trb` に書く**（Global Constraint 9）。

---

**訂正I：`isrorder_table` は「.cfg 記述順」ではなく「isrid 昇順」で生成する。**

dcre `interrupt.trb:341` は `$cfgData[:CRE_ISR].each_with_index`（＝**挿入順**）で
`isrorder_table` を生成する。しかし FMP3 のインライン連鎖生成
（`kernel/interrupt.py:375`・`interrupt.trb:416`）は `sorted(cfgData["CRE_ISR"].items())`
＝**isrid 昇順**を安定ソートの基底に使っている。

→ **`isrorder_table` も isrid 昇順で生成する**（dcre からの意図的な逸脱）。
理由（真の根拠）：**同じ .cfg で `ENA_DYNISR` を足したり外したりしても ISR の
呼出し順序が変わってはならない**。`initialize_isr` は `isrorder_table` の順に
`enqueue_isr` し、`enqueue_isr` は「自分より真に大きい isrpri の直前」に挿入する
（＝同一 isrpri 内では enqueue 順が保たれる）ので、キューの並びは
「isrid 昇順を基底とする isrpri の安定ソート」＝**インライン連鎖と完全に同じ順序**になる。
挿入順で生成すると、ID 入力ファイルで ID を明示した構成でだけ順序が食い違う。

★通常のビルドでは ID は .cfg 出現順に割り当てられる（`cfg_py/pass2.py:320-341`）ので、
両者は一致する。差が出るのは ID 入力ファイルを使う構成だけである。
この invariant（opt-in の有無で呼出し順が変わらない）は Task 6 の手順1で実測する。

## 3. カーネル層（新設ランタイムオブジェクト）

dcre interrupt.h の ISRINIB/ISRCB を移植し、FMP3 の MP 対応を加える:

```c
typedef struct isr_initialization_block {
	ATR			isratr;			/* ISR属性 */
	EXINF		exinf;			/* ISRの拡張情報 */
	ISRQCB		*p_isr_queue;	/* 登録先ISR呼出しキュー */
	ISR			isr;			/* ISRの先頭番地 */
	PRI			isrpri;			/* ISR優先度 */
} ISRINIB;

typedef struct isr_control_block {
	QUEUE		isr_queue;		/* キューエントリ（free-list と共用） */
	const ISRINIB *p_isrinib;
	uint32_t	isrseq;			/* enqueue 世代番号（Codex #1 — 走査継続キー） */
	uint_t		running;		/* 実行中コアのビットマップ（Codex #2 — quiesce 用） */
} ISRCB;
```

- キューは (isrpri, isrseq) の辞書式昇順。isrseq はキューごとの単調カウンタから
  enqueue 時に採番（del/再 acre で再利用されても新しい seq が付くため曖昧にならない）。
  **リセットしない**（当初案は「キューが空になったときカウンタを 0 に戻す」だったが、
  訂正G→Task 5 のコントローラ裁定で撤回した。詳細は訂正G参照）。
  **u32 wrap は 2^32 回の enqueue で発生**するが、システム寿命の間に1つのキューへ
  2^32 回 enqueue することは実用上到達不能であり、wrap を受容する（spec 制約として明記）。
- `free_isrcb`（グローバル・FIFO）、initialize_isr（マスタのみ、静的 ISR の isrorder 挿入
  + 動的スロット TA_NOEXS 化）。
- ISRID マクロ: 2レンジ（段階1〜3a と同型）。

---

**訂正C：`isrseq` のキューごとカウンタの置き場所が spec に無い。`ISRQCB` を新設する。**

spec §3 は「isrseq はキューごとの単調カウンタから enqueue 時に採番」「キューが空に
なったときカウンタを 0 にリセット」と書くが、**そのカウンタをどこに置くかを定めていない**。
dcre の `isr_queue_table[]` は素の `QUEUE` 配列（`interrupt.h:93`）であり、
カウンタを置く場所が無い。

→ **キュー要素の型を新設する:**

```c
/*
 *  割込みサービスルーチン呼出しキュー管理ブロック
 */
typedef struct isr_queue_control_block {
	QUEUE		isr_queue;		/* 割込みサービスルーチン呼出しキュー */
	uint32_t	isrseq;			/* 次に採番するenqueue世代番号 */
} ISRQCB;
```

`ISRINIB.p_isr_queue`・`ISR_ENTRY.p_isr_queue`・`call_isr()`・`enqueue_isr()` の型を
すべて `ISRQCB *` にする。`isr_queue` が先頭メンバなので `queue_*` 操作は
`&(p_isrq->isr_queue)` で従来どおり使え、`(ISRQCB *) p_queue` の型 punning も成立する
（ISRCB の `isr_queue` 先頭配置と同じ技法）。dcre からの**意図的な逸脱**であり、
理由は Codex #1 対応（走査の安定キー）が dcre に無い機構だからである。

---

**訂正G：`isrseq` の空キューリセットには「走査中に追加された ISR がその回は走らない」という副作用がある（Task 4 レビュー・Task 5 で「安全側」の評価を撤回し，リセット自体を撤廃）。**

spec §3 は当初「キューが空になったときカウンタを 0 にリセットして実用上の到達不能性を
保証する」と書いていたが、**副作用を書いていなかった**。

走査中に（glock を外して ISR 本体を実行している間に）キューが空になり、その後
`acre_isr` された ISR は `isrseq` が 0 から振り直される。走査側の継続キー `cur` は
削除済み ISR の `(isrpri, isrseq)` を保持しているため、**新しい ISR の
`(isrpri, 0)` は `cur` より大きくならず、その割込みでは呼ばれない**（次の割込みで呼ばれる）。

Task 1 時点ではこれを**安全側の脱落**（二重実行は起こらない）と評価し、spec §3 へ
その帰結を明記するに留めた。

**（Task 4 のレビューで判明）** しかし spec §5 は「同一割込み起動内での拾い上げ」を
保証する（§5 の実証例：ISR A 実行中に `del(A)` + `acre(C)` すると、C は同一起動内で
拾われる — `s7 > s5` で曖昧さが無いことを根拠にしている）。上のリセット由来の脱落は
まさにこの保証が破れる具体例であり、「安全側」という評価は成立しない
（§5 が要求する保証への違反であって、単なる保守的マージンではない）。

**（Task 5 のコントローラ裁定：リセットを撤廃）** `enqueue_isr` からリセット分岐を
完全に削除した。`isrseq` はキューの生存期間を通じて単調増加する（リセットなし）。
根拠：単調カウンタであれば §5 の同一起動内保証が回復する（ドレイン後の enqueue は
必ず任意の走査中 `cur` より大きい値を得る）。u32 wrap には1つのキューへシステム
寿命の間に 2^32 回 enqueue する必要があり、実用上到達不能である（リセットを
正当化していたのと同じ論法を、リセットではなく wrap の受容に転用する）。

→ spec §3 の該当記述は「リセットなし・単調カウンタ・wrap は 2^32 enqueue で実用上
到達不能として受容」に置き換えた（上記済み）。

**（Codex 外部レビュー指摘・2026-08-04 追記：保証のスコープを訂正）** 上記「§5 の
同一起動内保証が回復する」は無条件ではない。`ISR_KEY_GT` は `isrpri` が第一キーの
辞書式比較であるため、走査中の `cur=(pri_c, seq_c)` に対し、新エントリの `isrpri`
が `pri_c` より小さい（＝数値が小さく優先度が高い）位置に入る場合は、`isrseq` が
単調でもキー比較 `(isrpri, isrseq) > cur` が成立せず、本起動では実行されない
（次の割込み発生から有効になる）。単調 `isrseq` が回復するのは `isrpri >= pri_c`
の位置（cur と同じかそれより後の走査位置）に入る新エントリに対してだけであり、
これは §5 の実証例（同一 `isrpri` の `del(B)`→`acre(D)`）を含む安全上重要な範囲
（同一 `isrpri` の del→再 acre で取りこぼし・二重実行が起きないこと）を正しく
カバーしている。より高優先な新規 ISR が「次の割込みから有効」になるのはバグでは
なく、「起動開始時点で登録済みの ISR 集合を処理する」という意味論（静的インライン
連鎖と同一）の帰結であり、仕様である。コードの変更はない。

（リセットの実装点は Task 1 Step 7 で確認・Task 3 で実装済みだったが、Task 5 で
撤回・削除した。§10 参照。）

## 4. del_isr の寿命意味論（Codex #2 — quiesce 方式で確定 + 訂正B）

**保証: del_isr が E_OK を返した時点で、対象 ISR は実行中でなく、以後実行されない**
（dcre 単一コアと同等の強い意味論）。

---

**訂正B：`del_isr` は `TA_NOEXS` を quiesce の「前」に書く。spec §4 の手順順序には二重 del の競合がある。**

spec §4 は「(1) unlink →(2) quiesce →(3) `TA_NOEXS` → free-list」という順序を規定するが、
**この順序では同一 isrid に対する 2 本の `del_isr` が競合する**。quiesce ループは
glock も CPU ロックも解放して待つため、その隙に別コアのタスクが同じ isrid へ
`del_isr` を呼ぶと、`p_isrcb->p_isrinib->isratr` はまだ `TA_NOEXS` ではないので
E_NOEXS 分岐に落ちず、**既に unlink 済みのキューエントリに対して
`queue_delete()` を再実行する**。`queue_delete` は削除済みエントリの
`p_prev`/`p_next`（他の要素を指したまま）を書き換えるため、**キューが壊れる**。

→ **正しい順序は「(1) unlink →(2) `isratr = TA_NOEXS` →(3) quiesce →(4) free-list」である。**
`TA_NOEXS` を先に書けば、後続の `del_isr` は E_NOEXS を返して何もしない
（オブジェクトはその時点で論理的に削除済みなので、これは正しい意味論である）。
`acre_isr` は free-list からしか CB を取らず、CB は (4) まで free-list に入らないので
横取りされることもない。

★**この入れ替えが安全である根拠（真であることを Task 1 Step 5 で現物確認する）:**
段階3b では「属性の読み（`atr & TA_MBALLOC` 等のビット検査）は `TA_NOEXS` の
書込みより前で完了していなければならない」という不変量があった。
**ISR にはこの制約が無い。** `isratr` は
- `del_isr` の `== TA_NOEXS` 同値比較
- `initialize_isr` の `TA_NOEXS` 代入
- `acre_isr` の `isratr` 代入

にしか現れず、**ビット検査もマスク比較も 1 箇所も無い**（ISR には `TA_MBALLOC` /
`TA_MEMALLOC` / `MTX_CEILING` に相当する属性ビットが無い＝管理領域を持たない）。
`call_isr` は `isratr` を一切読まない（`isr`/`exinf`/`isrpri` だけを読む）。
したがって `TA_NOEXS`（全ビット 1）を早く書いても誤判定する式が存在しない。

★また `ISRID(p_isrcb)` は `p_isrinib` **ポインタの差分**で ID を求めるので、
`isratr` が `TA_NOEXS` になっても正しい ID を返す。quiesce 中の走査側が
`LOG_ISR_LEAVE(ISRID(p_isrcb))` を評価しても壊れない。

（Task 1 Step 5 の grep 実測に関する注記は §10 を参照。dcre の `isratr` grep 一致は
上記 3 箇所を含む計 8 行あり、うち `interrupt.c:323` の `CHECK_VALIDATR(isratr, TARGET_ISRATR)`
がビット演算を含むが、これは acre_isr のローカル変数（永続フィールドではない）に対する
ものであり、本訂正の根拠を損なわない。詳細は §10 の確認結果表に記録する。）

---

実装:
1. giant lock 下で E_NOEXS/E_OBJ 検査 → キューから unlink（以後、新たな走査は拾わない）。
2. `isratr = TA_NOEXS` を書く（quiesce の前。訂正B）。
3. `p_isrcb->running != 0`（他コアで当該 ISR 本体が実行中）なら、
   **glock と CPU ロックを解放して待機し、再取得して再確認するループ**で
   running == 0 を待つ（quiesce）。
   - デッドロック不成立の論証: running を立てるのは割込み文脈のウォーカー（§5）で、
     ISR 本体の完了により必ずクリアされる。del_isr を呼ぶタスクは待機中 CPU ロックを
     解放しているため、自コアへの割込み配送も阻害しない。同一コアで「実行中 ISR を
     del」は文脈上不可能（割込み文脈がアクティブな間、同コアのタスクは走らない）。
   - 待機時間は ISR 本体の実行時間で有界（TOPPERS の ISR 短時間規約）。
4. quiesce 完了後、free-list 返却。
5. ユーザへの規約明記: del_isr の完了後は exinf の指す資源を安全に解放できる。

## 5. ランタイムキュー走査（call_isr の MP 版 — Codex #1 対応 + 訂正F）

```
walk(queue):
  lock_cpu 状態確認; acquire_glock            ← signal_time/call_cyclic の先例に従う
  cur = (pri=-∞, seq=-∞)
  loop:
    next = queue 内で (isrpri,isrseq) > cur の最小要素   ← 安定キーによる再決定
    if 無し: break
    cur = (next->isrpri, next->isrseq)
    isr/exinf をローカルへコピー; next->running |= 自コア bit
    release_glock; unlock_cpu                  ← ISR 本体はロック外（現行連鎖と同じ）
    (*isr)(exinf)
    if sense_lock():
      force_unlock_spin(my_pcb)
    else:
      lock_cpu()
    acquire_glock()
    next->running &= ~自コア bit               ← ★unlink 済みでも ISRCB は quiesce まで
                                                  free されないため安全に書ける（§4-2/3）
  release_glock; unlock_cpu                     ← 必ず CPU ロック解除状態で戻る
```

---

**訂正F：走査後のロック状態復元を `call_cyclic` の 3 分岐に確定する。**

spec §5 の擬似コードは「`if sense_lock(): force_unlock_spin(my_pcb)`」「`lock_cpu; acquire_glock`」と
書いているが、**両方を無条件に並べると CPU ロック済みの状態でさらに `lock_cpu()` を呼ぶ**。
現物の先例は `kernel/cyclic.c:541-549`（`call_cyclic`）であり、次の形である：

```c
	if (sense_lock()) {
		force_unlock_spin(p_my_pcb);
	}
	else {
		lock_cpu();
	}
	acquire_glock();
```

すなわち **3 分岐（if / else / 共通の acquire_glock）**であって、
`sense_lock()` が真のときは `lock_cpu()` を呼ばない（既にロック状態だから）。
`force_unlock_spin(p_my_pcb)`（`kernel/spin_lock.c:162-176`）は
**スピンロックだけを解放し CPU ロック状態は維持する**ので、どちらの枝を通っても
「CPU ロック状態」で `acquire_glock()` に合流する。

→ spec §5 の擬似コードをこの 3 分岐に置き換える。

★あわせて**走査終了時の状態**を定める：`call_isr` は自分で `lock_cpu()`+`acquire_glock()` して
始まるので、**必ず `release_glock(); unlock_cpu();` で終わり、CPU ロック解除状態で戻る**。
これは現行インライン連鎖との**意図的な差**である：インライン連鎖はロック復元コードを
ISR と ISR の**間**にしか置かない（`kernel/interrupt.trb:460-466` の `if index > 0`）ため、
**最後の ISR が CPU ロックしたまま戻ると、そのロックが割り込まれたタスクへ漏れる**。
キュー方式ではこれが起きない。差が及ぶのは opt-in した intno **だけ**なので
Global Constraint 3 に抵触しない。この差を spec §5 と台帳に明記する。

---

- **取りこぼし・二重実行の不成立**（Codex #1 のシナリオで検証）:
  A(10,s5) 実行中に del(A)+acre(C,10→s7): 再取得後 cur=(10,s5) より大きい最小 =
  B(10,s6) → C(10,s7) → D(20,…)。A は再実行されず B も飛ばされない。
  再利用された旧 A のスロットが C になっても s7 > s5 で曖昧さなし。
  ★この例は新エントリ C が cur と同一 isrpri（10）の位置に入る、単調 isrseq が
  カバーする範囲（isrpri >= cur の位置）を示す代表例である。cur より高優先
  （isrpri が小さい）な位置に acre された ISR は本起動では実行されず、次回の
  割込みから有効になる（§3 訂正Gの追記・Codex 外部レビュー指摘を参照。この
  ケースはこの不成立の主張の対象外であり、そもそも保証していない）。
- 同一 intno の複数コア並行走査: 各ウォーカーが独立に (pri,seq) を進め、
  running はビットマップなので互いに上書きしない。同一 ISR が2コアで同時実行される
  ことは**ありうる**（静的インライン連鎖でも同じ — FMP3 の既存意味論を維持）。
  del_isr の quiesce は running == 0（全コアのビットが消えること）を待つ。
- ディスパッチ経路の追加コストは opt-in した intno のみに閉じる（Global Constraint 2）。

## 6. テスト（test_dcre5、musca_b1-2core、AID_ISR + ENA_DYNISR）

1. 静的のみ intno のディスパッチが不変であること（test_int2 の非退行が兼ねる）
2. acre_isr → 割込み発生 → ISR 実行（isrpri 順、静的 ISR との混在順序）→ del_isr
3. **quiesce の実証**: 長めの ISR 実行中に別コアから del_isr し、del が ISR 完了まで
   返らないこと（時間測定または順序 checkpoint）
4. 走査中 del/acre の安定キー実証（同一 isrpri 複数 ISR で B の取りこぼし・A の
   二重実行が無いこと — 決定的に組めるかは計画時に検討、無理なら変異 control で代替）
5. E_NOID/E_NOEXS/E_OBJ（未 ENA_DYNISR intno への acre）
6. カーネル変異 negative control + 非退行全テスト

---

**訂正H：quiesce の実証は musca_b1 では「PRC2 の ISR × PRC1 の del_isr」でしか組めない。同一キューの2コア同時走査は musca_b1 で到達不能である。**

spec §5 は「同一 intno の複数コア並行走査」を前提に設計しており、spec §6 は
テストを `musca_b1-2core` で行うとしているが、**musca_b1 ではこの構成が作れない**。

現物確認：`target/musca_b1_gcc/target_kernel.trb:17-24` は
`$INTNO_VALID[prcid].push((prcid << 16) | intno)` の形であり、**intno にプロセッサ ID が
符号化されている**。`INTNO_CREISR_VALID[1]` と `[2]` は**要素を 1 つも共有しない**。
したがって affinity が 2 コアに跨るクラス（`CLS_ALL_PRC1`＝`target_class.trb:29-31`）の
中に `CFG_INT` を書くと、`kernel/interrupt.trb:157-165` の検査で **E_RSATR** になる
（既存のエラー回帰 `tools/cfg_error_tests/musca_b1_e_rsatr_intno_affinity.cfg` が
まさにこれを固定している）。

一方、**PLIC/GIC のグローバル割込みを持つターゲットでは到達可能である**（現物確認）：
- `arch/riscv_gcc/polarfire_soc/chip_kernel.trb:12-19`：`$INTNO_VALID[prcid].push(intno)`
  ＝ intno 0..182 が**全プロセッサで同じ値**。
- `arch/arm_gcc/zynq7000/chip_kernel.trb:19-22` / `arch/arm_gcc/zynqmp_r5/chip_kernel.trb`：
  global intno 32..95 が**全プロセッサで同じ値**（private 0..31 のみ per-prcid 符号化）。

→ spec §6 を次のように直す：
- **quiesce の実証は可能である**：`INTNO2`（PRC2 の予備 NVIC IRQ60）の ISR を PRC2 で
  走らせ、その最中に PRC1 のタスクから `del_isr` を呼ぶ。これは「他コアで実行中の ISR を
  del する」ものであり、spec §4 の保証そのものである。
- **「同一キューの 2 コア同時走査」は musca_b1 では実証できない**。設計論証（spec §5）と
  コード検査で代替する。

**訂正H 追記（最終レビュー Important #3、`test/test_dcre_mix_ma.cfg` 実測）**：
上の「一方、PLIC/GIC のグローバル割込みを持つターゲットでは到達可能である」という
記述は，実際には**過度に楽観的だった**。実測（`build/kria_arm64-tmixma`、
`cmake --build` 実行）で判明した事実は次の通り：

- **kria_arm64（GIC）・polarfire_soc_kit（PLIC）は，intno がグローバルでも
  `CFG_INT`（したがって `ENA_DYNISR` も同一クラス）を affinityPrcList が
  2以上のクラスの中には書けない**。`arch/arm64_gcc/common/gic_kernel.py` /
  `arch/riscv_gcc/common/plic_kernel.py` の `TargetCheckCfgInt`（NGKI5184：
  「複数プロセッサでの割込み受付は動作するはずだが未テストのため現状
  サポートしない」）が，intno の符号化方式に関係なく **E_RSATR** で止める。
  これは dcre / ISR 移植とは無関係の，既存の（pristine な）アーキ層の制約
  であり，本ブランチが持ち込んだものではない。
- **kria_r5（arm_gcc/zynqmp_r5）だけがこの検査を持たない**：
  `target/kria_r5_gcc/target_kernel.py` は `chip_kernel.py` だけを
  `IncludeTrb` し，kria_arm64 側のように `chip_kernel.py` から
  `gic_kernel.py` を `IncludeTrb` していない。`arch/arm_gcc/common/`
  には `gic_kernel.trb`（pristine，未 `.py` 化）はあるが，どの `.py` からも
  参照されない。pristine の `chip_kernel.trb`（zynqmp_r5）自体にもこの
  安全網は無く，**upstream の R5 ポートが最初から持っていない非対称**である
  ことをソース突き合わせで確認した（今回の dcre 移植が生んだ抜けではない）。
- したがって，**生成レベルの実証は `kria_r5-2core` でのみ可能**であり，
  `test/test_dcre_mix_ma.cfg`（`CLS_ALL_PRC1`，`INTNO1`=40，
  affinityPrcList=[1, 2]）で実測した：生成物 `kernel_cfg.c` の
  `_kernel_inh_table_prc1[0x28]` と `_kernel_inh_table_prc2[0x28]` の
  **両方**が同一の `_kernel_inthdr_40` を指し，その `_kernel_inthdr_40` は
  同一の `_kernel_isr_queue_table[0]`（`_kernel_tnum_isr_queue == 1`）を
  呼ぶ。すなわち，per-prcid の DEF_INH 相当のエントリが affinityPrcList の
  要素数（2個）ぶん作られ，いずれも同一キュー／同一 inthdr を指すことを
  cfg 生成物から実証した。`tools/cfg_equivalence.sh` は同構成で
  `RESULT = MATCH`（Python 実装と Ruby オラクルが同一生成物を出す）。
- **runtime の同一キュー2コア並行走査は未実証のまま**（kria_r5-2core の
  QEMU 実行は本検査の対象外，コード検査では健全）。musca_b1 では構成不能
  （intno の per-prcid 符号化），kria_arm64/polarfire では上記 NGKI5184 が
  ブロックするため，runtime 実証には NGKI5184 自体を緩める（アーキ層の
  変更，本ブランチのスコープ外）か，kria_r5-2core で実際に走らせる
  QEMU 検証が必要——いずれも今回は行っていない。

この不足（および NGKI5184 の適用範囲がターゲット非依存であるという
発見）を Task 7 の申し送りに**正直に**書く。

---

## 7. 案B-1 との比較（記録）

B-1（`AID_ISR(intno, n)` — intno 別プール）は E_NOID の粒度が intno 単位になり
診断性で勝るが、①dcre の AID_ISR 意味論（グローバルプール）から逸脱する、
②適格 intno 表と CB 容量が結合し cfg 検証が複雑化する、③Codex #3 も B-2 の分離を
仕様化しやすいと評価 — により **B-2 を採用**。B-1 が必要になったら後方互換で
追加可能（ENA_DYNISR に個数引数を足す拡張余地を残す）。

## 8. 実装前確認リスト（計画 Task 1）

1. dcre ISRINIB/ISRCB/call_isr/enqueue_isr/search_isr_queue/initialize_isr の転写元
   行番号確定と、running/isrseq フィールド追加の влияние（サイズ・初期化）
2. FMP3 の inthdr 生成（interrupt.trb:411-477）の per-prcid DEF_INH 機構と
   キュー方式切替の結合点（isr_flag ガードとの整合）
3. 割込み文脈での acquire_glock の作法（signal_time の実装詳細 — ネスト・
   すでに glock 保持中の場合の扱い）
4. TARGET_ISRATR・VALID_ISRPRI・TMIN/TMAX_ISRPRI の FMP3 既存有無（**訂正J** で確定）

---

**訂正J：FMP3 に無い定義は 4 件（`TMIN_ISRID`・`TARGET_ISRATR`・`VALID_ISRID`・`VALID_ISRPRI`）。`TMIN/TMAX_ISRPRI` はある。**

spec §8-4 は「TARGET_ISRATR・VALID_ISRPRI・TMIN/TMAX_ISRPRI の FMP3 既存有無」を
確認事項としているが、計画作成時に確定した（Task 1 Step 4 で再確認する）：

| 定義 | FMP3 | dcre | 対応 |
|---|---|---|---|
| `TMIN_ISRPRI`(=1) / `TMAX_ISRPRI`(=16) | **ある**（`include/kernel.h:654-655`） | ある | そのまま使う |
| `TMIN_ISRID` | **無い** | `kernel_impl.h:127` | `kernel/kernel_impl.h` に追加（Task 2） |
| `TARGET_ISRATR`（C マクロ） | **無い** | `kernel_impl.h:155-157` | `kernel/kernel_impl.h` に追加（Task 2） |
| `VALID_ISRID` | **無い** | `check.h:64` | `kernel/check.h` に追加（Task 3） |
| `VALID_ISRPRI` | **無い** | `check.h:73-74` | `kernel/check.h` に追加（Task 3） |

★`TARGET_ISRATR` は cfg 側でも参照される（`kernel/kernel_sym.def:73` の
`TARGET_ISRATR,,,defined(TARGET_ISRATR),0`）。C マクロを追加しても
**値は 0 のままなので cfg 出力は変わらない**（`TARGET_TSKATR` が
`kernel_impl.h:209-211` に定義済みで `kernel_sym.def:69` も同じ形、という先例がある）。
★ただしこれは**推測ではなく Task 2 Step 6 の管理された差分で実証する**
（`TARGET_ISRATR` の値が 0 以外に化けたら `CRE_ISR` の `E_RSATR` 判定が変わる）。

★**5 件目（訂正J の表には無い）**：`CHECK_OBJ` も FMP3 の `kernel/check.h` に**無い**
（dcre `check.h:235-240` にはある）。Task 1 Step 8 での確認結果：
`kernel/check.h` に dcre `check.h:235-240` と同一の `CHECK_OBJ` を追加する（Task 3）。
理由：`acre_isr` 以外にも将来使われうる汎用マクロであり、`CHECK_PAR`/`CHECK_ILUSE` と
同じ形の 6 行で、段階3b の `VALID_DPRI` 追加と同じ前例に載る。直書きの `if`/`goto` にしない
（FMP3 のサービスコールは検査をマクロで書く流儀で統一されている）。

---

5. ENA_DYNISR の静的 API 文法（kernel_api.def 記法・クラス内 API の先例 = CFG_INT）
6. quiesce ループの待機プリミティブ（busy-wait + glock 再取得の既存先例 —
   spin_lock.c の型）と、待機中の sil_dly_nse 等の要否
7. u32 isrseq のキュー空リセットの実装点（queue_empty 検査の挿入箇所）
8. 走査の「元のロック状態へ復元」が必要か（inthdr 入口のロック状態 — 現行連鎖の
   sense_lock 分岐と同じ扱いでよいか）

## 9. 統治

- **7タスク構成に確定**（実装前確認 → cfg 両エンジン〔切替・表生成〕→ ISRCB/初期化 →
  walk/call_isr → acre/del（quiesce）→ test_dcre5 → 最終回帰）。
- 着手は段階3b 完了後（裁定済みの順序: 3a → 3b → ISR）。
- pristine 編集は DIVERGENCE_MAP。上流報告候補は従来4件。

## 10. 実装前確認の結果（2026-08-04 実測）

Task 1（実装前確認と spec 訂正10件反映）で Step 1〜9 を実行した実測結果。
**ゲート条件 G1〜G5 はすべて非該当（BLOCKED なし）。**

### ゲート条件の判定

| ゲート | 判定 | 根拠 |
|---|---|---|
| G1: 割込み文脈からの acquire_glock 先例 | **非該当（存在した）** | `signal_time`（`kernel/time_event.c:709-770`）が `assert(sense_context(p_my_pcb)); assert(!sense_lock());`（:718-719）→ `lock_cpu(); acquire_glock();`（:721-722）で始まり `release_glock(); unlock_cpu();`（:768-769）で終わる。実測一致。 |
| G2: call_cyclic の 3 分岐 | **非該当（3 分岐だった）** | `call_cyclic`（`kernel/cyclic.c:525-554`）が `release_glock(); unlock_cpu();`（:540-541）→ ハンドラ呼出し → `if (sense_lock()) { force_unlock_spin(p_my_pcb); } else { lock_cpu(); }`（:547-552）→ `acquire_glock();`（:553）。`sense_lock()` が真のとき `lock_cpu()` を呼んでいないことを目視確認。 |
| G3: ENA_DYNISR のクラス内 API 文法 | **未判定（Task 1 の対象外・計画どおり）** | 計画は「G3 は成立しない見込みだが、`.def` に `ENA_DYNISR` を足して `CLASS(...) { ENA_DYNISR(...); }` が構文エラーにならないことを Task 2 Step 3 で実際に確かめる」としている。Task 1 Step 8 では `.def` 文法自体（`.intno*` が符号無し整数定数式パラメータ兼登録キーになること、クラス情報は `.def` でなく `params["class"]` の有無で `.py`/`.trb` 側が検査すること）のみを現物確認した。 |
| G4: delay_for_interrupt の4アーキ存在 | **非該当（4アーキすべてに存在）** | `arch/arm_m_gcc/common/core_insn.h:192`／`arch/arm_gcc/common/core_kernel_impl.h:261`／`arch/arm64_gcc/common/core_kernel_impl.h:248`／`arch/riscv_gcc/common/core_kernel_impl.h:191`。 |
| G5: isratr のビット検査・マスク比較 | **非該当と判断（詳細は Step 5 の項を参照）** | 永続フィールド `p_isrinib->isratr` に対するビット検査・マスク比較は dcre・FMP3 のいずれにも無い。ただし dcre `interrupt.c:323` の `CHECK_VALIDATR(isratr, TARGET_ISRATR)` はローカル変数 `isratr` に対するビットマスク比較であり、計画の「4箇所だけ」という記述と実測の grep 件数が食い違う。下記 Step 5 参照。 |

### Step 1: §8-5 機能コードの既存有無

`include/kernel_fncode.h`：`#define TFN_ACRE_ISR (-204)`＝`:144`、`#define TFN_DEL_ISR (-220)`＝`:156`。
計 2 行（`grep -c` = 2）。計画の期待値と一致。**変更不要。**

### Step 2: §8-1 dcre の ISR ランタイムオブジェクト現物（転写元の行範囲）

転写元：`/home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/kernel/`

| 対象 | 行範囲 | 内容 |
|---|---|---|
| `interrupt.h` | `53-136` | ISRINIB(`:56-62`)/ISRCB(`:67-70`)/ISR_ENTRY(`:75-78`)/extern群/`ISRID`マクロ(`:126`)/宣言 |
| `interrupt.c` | `166-195` | `tnum_isr`/`tnum_sisr`/`INDEX_ISR`/`get_isrcb`/`enqueue_isr` |
| `interrupt.c` | `197-233` | `free_isrcb` / `initialize_isr` |
| `interrupt.c` | `235-262` | `call_isr` |
| `interrupt.c` | `264-293` | `search_isr_queue` |
| `interrupt.c` | `295-354` | `acre_isr` |
| `interrupt.c` | `356-394` | `del_isr` |
| `interrupt.trb` | `260-346` | queue/table 生成・inthdr 生成・`IsrObject < KernelObject`・`isrorder_table` 生成 |

実測で確認した個別事実（現物確認、計画と一致）：
1. `ISRINIB` は `isratr`/`exinf`/`p_isr_queue`/`isr`/`isrpri` の5フィールド。`ISRCB` は
   `isr_queue`/`p_isrinib` の2フィールドのみ（`isrseq`/`running` は FMP3 の新設）。
2. `ISRID(p_isrcb)`（`:126`）は `((ID)(((p_isrcb) - isrcb_table) + TMIN_ISRID))` という
   `isrcb_table` からの配列差分。FMP3 は CB がポインタ表なのでこの式は使えず、
   段階2/3a/3b と同じ「INIB ポインタ差分の2レンジ式」に置き換える（Task 3）。
3. `enqueue_isr`（`:182-195`）は `isrpri < p_entry の isrpri` で break、
   すなわち「自分より真に大きい isrpri の要素の直前」に `queue_insert_prev` する。
   等しい isrpri のときは進む（break しない）ので、同一 isrpri 内では enqueue した順に並ぶ。
   → 訂正I の invariant の根拠。
4. `initialize_isr`（`:207-231`）の順序：全キューを `queue_initialize` →
   `isrorder_table` の順に静的 ISR を `p_isrinib` 結線して `enqueue_isr` →
   `queue_initialize(&free_isrcb)` → 動的スロットに `TA_NOEXS` を書いて
   `queue_insert_prev(&free_isrcb, ...)`（末尾挿入＝FIFO）。
   `i` を静的ループから引き継いで動的ループへ渡す書き方（`for (j = 0; i < tnum_isr; i++, j++)`）
   は段階1〜3bと同じ。
5. `call_isr`（`:240-260`）は単方向走査（`p_queue = p_queue->p_next`）でロックを取らない、
   次要素があるときだけ `if (sense_lock()) { unlock_cpu(); }`。単一プロセッサ前提であり
   FMP3ではこの形は使えない（Task 4 で全面的に書き直す）。
6. `search_isr_queue`（`:267-293`）は `tnum_isr_queue == 0` で NULL、`isr_queue_list[]` を
   二分探索。型が `ISRQCB *` に変わるだけでそのまま移植できる。
7. `acre_isr`（`:303-352`）の順序は計画記載のとおり実測一致（`CHECK_TSKCTX_UNL()` →
   パラメータをローカルへ → `CHECK_VALIDATR`/`CHECK_PAR` 群 → `search_isr_queue` →
   `CHECK_OBJ(p_isr_queue != NULL)`（`:330`）→ `lock_cpu()` → `E_NOID` →
   `queue_delete_next` → INIB を埋める → `enqueue_isr` → `ercd = ISRID(p_isrcb)`）。
   `exinf` はローカルにコピーせず `pk_cisr->exinf` を直接使う（関数冒頭コメントが理由を明記）。
   `CHECK_OBJ` は FMP3 の `kernel/check.h` に無い（dcre `check.h:235-240` にはある）。
8. `del_isr`（`:361-392`）の順序は計画記載のとおり実測一致。dcre には quiesce が無い
   （単一プロセッサなので構造的に不要）。dcre は `CHECK_ID`（E_ID）であり、
   段階3a訂正D（`del_flg`）・段階3b訂正F（`del_dtq`）のような `CHECK_PAR` 不整合は無い。
9. `interrupt.trb:263-294` の適格 intno 条件（`INTNO_CREISR_VALID` に含まれ・`CFG_INT` あり・
   `DEF_INH` なし）を実測一致で確認。`:299-316` の inthdr は
   `_kernel_call_isr(&(_kernel_isr_queue_table[i]));` の1行。`:321-336` は
   `IsrObject < KernelObject`。`:338-346` は `isrorder_table`
   （`$cfgData[:CRE_ISR].each_with_index` による挿入順。`TNUM_SISRID == 0` のガードが無く
   `[0]` 配列になりうる — FMP3 では `TOPPERS_EMPTY_LABEL` でガードする、Task 2）。

### Step 3: §8-2 FMP3 の inthdr 生成機構

`kernel/interrupt.trb:411-477` / `kernel/interrupt.py:370-443` を実測確認。計画の記載と一致：
生成ループは prcid 1..TNUM_PRCID の外側ループ × `INTNO_CREISR_VALID[prcid]` の内側ループで、
`isrParamsList` が空なら何もしない。空でないとき `affinityPrcList` に prcid が無ければ
`continue`。`DEF_INH` は prcid ごとに生成するが `inthdr` 本体は `isr_flag[intnoVal]` で
1回だけ生成。本体は `PCB *p_my_pcb = get_my_pcb();`（ISR 2本以上のときのみ）+ isrpri 昇順の
直接呼出し + ISR 間の `if (_kernel_sense_lock()) { _kernel_force_unlock_spin(p_my_pcb); _kernel_unlock_cpu(); }`。
Python 版は `sorted(isrParamsList, key=lambda p: p["isrpri"].val)`（Python の `sorted` は
安定ソート保証があるため単一キーで良い）、Ruby 版は `i += 1` を第2キーに追加して stable
sort を明示（`sort_by` は Ruby では安定性が保証されないため）。

`kernel_cfg.h` への出力（`interrupt.trb:50-55`／`interrupt.py:54-61`）は `#define TNUM_ISRID <n>`・
`#define <ISRID名> <値>`・空行1個の3種類のみで、`kernelCfgH` へ書くのはこの1箇所のみ
（他に `kernelCfgH` への書込みが無いことを grep で確認）。

`target/musca_b1_gcc/target_class.trb` の `affinityPrcList`/`initPrc` 実測は Step 9 の表を参照。

結論（計画記載のとおり）：切替は「`isrParamsList.size > 0` で入る現行の枝」に opt-in intno
のための枝を並べる形にする。現行の枝には手を入れない（Global Constraint 3）。opt-in intno は
`isrParamsList` が空でも DEF_INH と inthdr を生成する必要があり、そのとき `clsid` は
`cfgData["CFG_INT"][intnoVal]["class"]` から取る。

### Step 4: §8-4 型・マクロの FMP3 既存有無（訂正J）

実測はすべて計画の期待値と一致：`TMIN_ISRPRI`(=1)/`TMAX_ISRPRI`(=16) は
`include/kernel.h:654-655` にある。`TMIN_ISRID`/`TARGET_ISRATR` は `kernel/kernel_impl.h` に
無い（`TMIN_SPNID` が `:194`、`TARGET_TSKATR` が `:209-211`）。`VALID_ISRID`/`VALID_ISRPRI`/
`CHECK_OBJ` は `kernel/check.h` に無い。`ISR` 型（`include/kernel.h:114`）と `INTNO` 型
（`:105`）はある。`kernel_sym.def:73` が `TARGET_ISRATR,,,defined(TARGET_ISRATR),0` で
`:69` の `TARGET_TSKATR` と同じ形（先例あり）。訂正J の表とおり（§8-4 参照）。

### Step 5: §8-3 割込み文脈での glock 作法と isratr のビット検査不在

G1/G2 の判定は上表のとおり非該当（先例あり）。`force_unlock_spin`（`kernel/spin_lock.c:162-176`）
はスピンロックだけを解放し CPU ロック状態には触らない（関数内コメント「ここではCPUロック
状態になっている」と一致）。

**isratr の grep 実測（G5・訂正Bの根拠）:**

FMP3（`grep -rn "isratr" kernel/ include/ | grep -v "\.py:" | grep -v "\.trb:"`）：
`kernel/kernel_api.def:13`（`CRE_ISR #isrid* { .isratr ... }` — API パラメータ名の宣言。
コードでのビット検査ではない）の1行のみ。ランタイムオブジェクトが無いので当然。

dcre（`grep -n "isratr" interrupt.c interrupt.h`）：**計画は「`interrupt.h:57`（宣言）・
`interrupt.c:339`（代入）・`:374`（同値比較）・`:383`（代入）の4箇所だけ」と書いているが、
実測はこれより多い8行だった**（計画は X、実測は Y — 差分を記録する）：
`interrupt.h:57`（`ATR isratr;` 宣言）、`interrupt.c:227`（`initialize_isr` の
`TA_NOEXS` 代入）、`:309`（`acre_isr` のローカル変数宣言 `ATR isratr;`）、`:318`
（`isratr = pk_cisr->isratr;` ローカルへのコピー）、`:323`
（`CHECK_VALIDATR(isratr, TARGET_ISRATR);`）、`:339`（`p_isrinib->isratr = isratr;` 代入）、
`:374`（`if (p_isrcb->p_isrinib->isratr == TA_NOEXS)` 同値比較）、`:383`
（`p_isrinib->isratr = TA_NOEXS;` 代入）。

このうち `:323` の `CHECK_VALIDATR(isratr, TARGET_ISRATR)` は
`#define CHECK_VALIDATR(atr, valid_atr) do { if (((atr) & ~(valid_atr)) != 0U) { ... } } while (false)`
（dcre `check.h:205-210`）であり、**`&` によるビット演算を含む**。計画の「ビット検査もマスク
比較も1箇所も無い」という記述はこの1件を数え落としている。

ただし、この `CHECK_VALIDATR` はローカル変数 `isratr`（`acre_isr` のスタック上の一時変数、
`pk_cisr->isratr` のコピー）を検査するものであり、`lock_cpu()` の**前**、かつ CB を
`free_isrcb` から取り出す（`:339`）**前**に実行される。永続フィールド `p_isrinib->isratr`
は一切参照しない。del_isr の quiesce 順序変更が問題になるのは「他コアが並行して
`p_isrinib->isratr` を読む」ケースであり、`acre_isr` 自身のパラメータ検証（呼出しコアの
スタック上でしか見えないローカル変数への検査）とは独立である。したがって、この1件の
発見は **G5 の趣旨（永続フィールドへのビット検査・マスク比較の有無）を覆さない**と判断する。
訂正Bはこの判断のもとで適用する。

### Step 6: §8-6 quiesce の待機プリミティブ

`wait_tmout`（`kernel/wait.c:110-137`、5行本体 `:127-131`）と `wait_tmout_ok`
（`:139-160`、5行本体 `:155-159`）が末尾にまったく同じ5行を持つ：
`release_glock(); unlock_cpu(); delay_for_interrupt(); lock_cpu(); acquire_glock();`。
直前のコメントは「ここで優先度の高い割込みを受け付ける．」で一致。

`delay_for_interrupt()` は4アーキすべてに存在（上表 G4 参照）。

`arch/arm_m_gcc/musca_b1/chip_kernel_impl.h:184-192` の `lock_native_spn` は
`while (try_lock(...)) { unlock_cpu(); delay_for_interrupt(); lock_cpu(); }` で、
コメント（`:170-172`）が `doc/porting.txt (6-21-3-2)`（`:1564`。同ファイル`:1457`にも
同旨の記述）の要求「取得できない場合は，一旦割込みを許可した後，再び割込みを禁止した後に
再試行する」を引用していることを確認。

→ `sil_dly_nse` は使わない（`delay_for_interrupt` のほうが先例として正しい）。

### Step 7: §8-7 isrseq の空キューリセットの実装点

`queue_empty`（`include/queue.h:151-`）は `p_queue->p_next == p_queue` で空を判定
（実測一致）。`queue_delete`（`include/queue.h:119-`）はエントリ自身のリンクを
作り直さない（削除後の `p_entry->p_next`/`p_prev` は古い値のまま）ことを実測確認
＝訂正B の二重 `queue_delete` が危険である理由の裏付け。

結論：リセットは `enqueue_isr()` の先頭、`queue_empty(&(p_isrq->isr_queue))` が真のときに
`p_isrq->isrseq = 0U;` とする（Task 3）。`del_isr` 側には置かない（削除は「空にする」以外の
経路もあり、判定点が増えるだけで利得が無い）。

### Step 8: §8-8・cfg 層の前提

`kernel_api.def` の文法：`.intno*` は「符号無し整数定数式パラメータ」（`.`）かつ
「登録キー」（`*`）になる（`cfg_py/pass1.py:96-140`、`.` の意味は`:112`、`*` の意味は`:130-131`）。
`CFG_INT .intno* { .intatr +intpri }`（`kernel/kernel_api.def:12`）が先例。
`ENA_DYNISR .intno*` と書く（`*` 付き）。クラス内 API であることは `.def` では表現せず、
`params` に `class` キーがあるかを `.trb`/`.py` 側が検査する
（`kernel/interrupt.py:129-132` 相当の `if "class" not in params:` → E_RSATR。§10 §3 の
生成物確認では実測範囲外だが、Step 3 の切替設計に同じ検査（`interrupt.py:355-358`/
`interrupt.trb:391-394`）があることを確認済み）。G3 は本 Task の対象外（Task 2 Step 3 で
実際に確かめる。上表参照）。

`KEYPAR`（`*`）を付けると `cfg_py/pass2.py:385-391` が同じ intno への2回目の登録を
E_OBJ で弾く（実測確認）。→ `ENA_DYNISR .intno*`（`*` 付き）にする。重複検査がタダで
手に入り、`cfgData["ENA_DYNISR"]` が intno 値をキーとする dict になるので後段の `in` 判定が
O(1) になる。

`cfg_py/pass2.py:181-186` と `cfg/pass2.rb:165-172` が `apiDefinition` の全 API について
`cfgData[api_sym] = {}` を先に作ることを実測確認（訂正D の根拠）。→ `kernel.py:142` の
`has_aid = self.aidapi in cfgData` は `AID_ISR` を `.def` に足した時点で恒真になる。

汎用枠組み（`kernel/kernel.py:108-277`）が `obj="isr"` から `TNUM_ISRID`/`TNUM_SISRID`/
`_kernel_tmax_isrid`/`_kernel_tmax_sisrid`/`const ISRINIB _kernel_isrinib_table[TNUM_SISRID]`/
`static ISRCB _kernel_isrcb_<ISRID>`/`static ISRCB _kernel_aisrcb_<i>`/
`ISRCB *const _kernel_p_isrcb_table[TNUM_ISRID]`/`ISRINIB _kernel_aisrinib_table[n]`/
`_kernel_initialize_isr(p_my_pcb);`（`:257-258`）を機械的に導くことを実測確認。`inibList` の
既定 `{"ISRINIB": "aisrinib_table"}` は `kernel.py:123`。

静的 `CRE_ISR` を含む `.cfg`：`grep -rln "CRE_ISR" --include=*.cfg .` の実測は
`sample/sample1.cfg`・`target/polarfire_soc_kit_gcc/softconsole/sample1/sample1.cfg`・
`target/musca_b1_gcc/target_serial.cfg`・各 `arch/*/*/chip_serial.cfg`（8ファイル）・
`test/test_int1.cfg`・`test/test_int2.cfg`・`tools/cfg_error_tests/e_par_creisr_intno_keyerror.cfg`。
`serial.cfg` 経由の `CRE_ISR(ISR_SIO)` が広く使われるため、訂正E のガード2は通常自動的に
満たされる。

`CHECK_OBJ` の扱い：`kernel/check.h` に dcre `check.h:235-240` と同一の `CHECK_OBJ` を
追加する（Task 3）。訂正J の5件目として上表に記載済み。

### Step 9: musca_b1 の割込みモデル（訂正Hの根拠）

| ターゲット | intno の符号化 | 2コア並行走査 |
|---|---|---|
| musca_b1（NVIC） | `(prcid << 16) \| intno`（`target_kernel.trb:17-24`。実測一致）。`INTNO_CREISR_VALID[1]`と`[2]`は要素を共有しない | **到達不能**（affinityが2コアに跨るクラスにCFG_INTを書くとE_RSATR。`musca_b1_e_rsatr_intno_affinity.cfg`が固定） |
| polarfire_soc（PLIC） | intno 0..182 が全プロセッサで同じ値（`chip_kernel.trb:12-19`。実測一致） | 到達可能 |
| zynq7000/zynqmp_r5（GIC） | global intno 32..95 が全プロセッサで同じ値（private 0..31のみper-prcid符号化）（`chip_kernel.trb:19-22`。実測一致） | 到達可能 |

`target_class.trb:13-34`（musca_b1、実測）：1コア時 `CLS_PRC1`=[1]・`CLS_ALL_PRC1`=[1]。
2コア時 `CLS_PRC1`=[1]・`CLS_PRC2`=[2]・`CLS_ALL_PRC1`=[1,2]・`CLS_ALL_PRC2`=[1,2]。

`target/musca_b1_gcc/target_test.h`（実測）：`INTNO1 = ((1U << 16) | (60U + 16U))`
（`:43`）、`INTNO2 = ((2U << 16) | (60U + 16U))`（`:49`、`TNUM_PRCID >= 2` 時のみ）。
`intno1_clear()`/`intno2_clear()` は空マクロ。コメントが「NVIC はコアごとに独立した
インスタンスであり、ras_int/prb_int/clr_int は自コアの NVIC_ISPR/NVIC_ICPR のみを操作する」
と明記。`test/test_int2.c` は `ras_int(INTNO1)`（`:182`）で割込みを起こす。

→ 訂正H のとおり、quiesce の実証は「PRC2 の ISR × PRC1 の del_isr」でのみ組める。
「同一キューの2コア同時走査」は musca_b1 では実証できない。

### 上流報告候補

従来4件（a〜d）から変更なし。dcre の `del_isr` は `CHECK_ID`（E_ID）であり、段階3a訂正D
（`del_flg`）・段階3b訂正F（`del_dtq`）のような `CHECK_PAR` 不整合は無いため、
上流報告候補 d は拡張しない（Step 2 item 8 で確認済み）。
