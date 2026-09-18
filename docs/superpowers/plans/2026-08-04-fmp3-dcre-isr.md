# FMP3 動的 ISR 生成（acre_isr/del_isr・案B-2 ハイブリッド）実装計画

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `feature/dynamic-creation` ブランチ上で `acre_isr`/`del_isr` と
`AID_ISR`/`ENA_DYNISR` を、cfg 両エンジン（Ruby オラクル + Python 製品）同時対応・
QEMU 回帰テスト付きで動かす。これで dcre 標準の動的生成オブジェクトが全て揃う。

**Architecture:** ★**本段階は移植ではなく新規サブシステムの構築である。**
段階1〜3b の 7 家族はいずれも「既にランタイムオブジェクト（INIB + CB + free-list）を
持つ機能に、動的スロットと 2 レンジ ID を足す」作業だった。ISR には**ランタイム
オブジェクトが存在しない**。FMP3 の ISR は cfg 生成時に intno ごとの平坦な関数
`_kernel_inthdr_<intno>` へ消し込まれ、各 ISR を isrpri 順に直接呼ぶだけである
（`kernel/interrupt.trb:411-477`）。したがって本段階では

1. **ランタイムオブジェクトそのもの**（ISRINIB / ISRCB / ISRQCB / ISR_ENTRY）を新設し、
2. **キュー走査ディスパッチ**（`call_isr`）という新しい実行経路を作り、
3. それを **opt-in した intno にだけ**接続する（`ENA_DYNISR`）

という 3 つを同時に行う。案B-2（ハイブリッド）の存在理由は**既存構成の
ディスパッチ経路を1バイトも変えないこと**であり、これが本段階を貫く最優先の制約である。

MP 対応は 2 点が新規である。(a) 走査中にユーザ ISR を呼ぶためジャイアントロックを
外す必要があり、その間に別コアの `acre_isr`/`del_isr` がキューを書き換えうる
（→ 安定キー `(isrpri, isrseq)` による走査位置の再決定。Codex #1）。
(b) `del_isr` が成功を返した時点で他コアの ISR 本体が走っていてはならない
（→ quiesce ループ。Codex #2）。いずれも段階1〜3b には対応物が無い。

**Tech Stack:** C（カーネル）、Python/Ruby（cfg テンプレート）、CMake、QEMU（musca_b1 ほか）。

---

## Global Constraints（spec から転記。実装中に再解釈しない）

1. ブランチ `feature/dynamic-creation`（段階3b の続き）。**main へはマージしない。**
   pristine への改変は `DIVERGENCE_MAP.md` に記録する（種別 `mod (dcre-port)` / `add (dcre-port)`）。
   ★本計画は**段階3b Task 7（最終回帰と台帳整理）の完了後に着手する**。計画作成時点の
   HEAD は `d337da4`（段階3b Task 5 完了）で、`test/test_dcre4.*` は未コミットだった。
   着手時に `git log --oneline -3` で 3b 完了コミットを確認し、実測値を記録すること。
2. 段階ISR = `acre_isr`/`del_isr` + `AID_ISR` + `ENA_DYNISR` + ランタイム ISR オブジェクトのみ。
   **段階4的な何か（`ref_isr` 等・dcre に無い API）を含めない。**
3. **★★既存構成への影響ゼロが本方式の存在理由である**（spec Global Constraint 2）。
   `ENA_DYNISR` の無い intno の**生成された `_kernel_inthdr_<intno>` 本体はバイト単位で
   不変**でなければならない。これは「だいたい同じ」ではなく `diff` で 0 行であることを
   Task 2 と Task 7 で実証する。恒常出力の追加は AID 系に限り、許容リストで固定する
   （訂正D — 恒常出力の**規模**が段階1〜3a より大きいことを受容した上で数値で記録する）。
4. `acre_isr`/`del_isr`/`T_CISR` は dcre 標準のシグネチャ・フィールドを維持する
   （`isratr`, `exinf`, `intno`, `isr`, `isrpri`）。**コア引数は追加しない。**
   動的 ISR の実行コアは対象 intno のクラス affinity に従う（コア指定は
   「どの intno を選ぶか」で表現される）。API 面の FMP3 拡張は `ENA_DYNISR` **1 個だけ**である。
5. 検証 = F-1：Ruby `.trb`（オラクル）にも同時移植し `tools/cfg_equivalence.sh`
   （exit 0=一致 / 1=不一致 / **2=前提未充足であり合格ではない**）を主検査に維持する。
6. CB はヒープ確保しない。予約 CB（named static + ポインタ表末尾）+ RAM inib 配列。
   free-list のリンクには **ISRCB 先頭の `QUEUE isr_queue` を直接流用**する
   （dcre `interrupt.c:337,384` と同一。段階3a/3b の `swait_queue`/`wait_queue` 直用と同型）。
7. **free-list は FIFO**（`del_isr` = `queue_insert_prev` で末尾へ / `acre_isr` =
   `queue_delete_next` で先頭から）。段階1で裁定済み。実装者・レビュアーとも再議しない。
   テストは FIFO/LIFO 不問で決定的になる形（「空きが1個だけの状態で del → 再 acre」）に組む。
8. 訂正C（段階3a）の**適用対象外**：`del_isr` は待ちタスクを1つも解除しないため
   ディスパッチ判断が不要である。`CHECK_TSKCTX_UNL()` を使い、
   `CHECK_TSKCTX_UNL_MYSTATE` は**使わない**（dcre `interrupt.c:369` と同じ）。
   ★この判断の根拠は「ISR にはオブジェクト固有の待ちキューが無い」であって
   「dcre がそう書いているから」ではない。コメントにはこの真の根拠を書く。
9. 汎用層 `CMakeLists.txt`・`cmake/fmp3_core.cmake`・`cfg_py/pass1.py`・`cfg_py/pass2.py`・
   **`kernel/kernel.py`・`kernel/kernel.trb`（cfg 共通枠組み）は変更しない**。
   `KERNEL_FCSRCS`（`kernel/Makefile.kernel:51-56` の22個）も**不変**
   （`acre_isr`/`del_isr`/`call_isr`/`initialize_isr` はすべて既存の `kernel/interrupt.c` に入る。
   **`kernel/isr.c` のような新規 `.c` を作らない** — dcre も `interrupt.c` に置いている）。
10. **★`TA_NOEXS` は `((ATR)(-1))`＝全ビットが 1 である**（`kernel/kernel_impl.h:199`）。
    段階3a 最終レビュー Important #1（`MTX_CEILING` のマスク比較が誤って真になる）と
    段階3b の中核リスク（`TA_MBALLOC` のビット検査）はこの性質に由来する。
    ★**ISR の `isratr` はビット検査にもマスク比較にも使われない**（`== TA_NOEXS` の
    同値比較のみ）。したがって段階3b の「属性の読みは TA_NOEXS の書込みより前」制約は
    **ISR には適用されない**。この事実こそが訂正B（TA_NOEXS を quiesce の**前**に書く）を
    可能にしている。**この根拠が真であることを Task 1 Step 5 で現物確認してから
    コメントに書く**（偽の根拠をコメントに書いたことが段階3a の Important 指摘だった）。
11. `rc=124` 単独を成功判定に使わない（期待出力の実在を `grep` で確認する）。
12. **`cmd | tail` / `cmd | grep` で成否判定しない。** パイプラインの `$?` は最後の要素のもの。
    ファイルへリダイレクトしてから `grep` するか、`${PIPESTATUS[0]}` を見る。
13. `tools/cfg_equivalence.sh` の **exit 2 は合格ではない**（前提未充足）。exit 0 のみ合格。
14. QEMU 実行は**プリセットごとに個別コマンド**で行い、ログを別ファイルに落とす。
    `for` ループで全構成を1コマンドに詰めると Bash ツールの 2 分タイムアウトに当たり、
    **qemu が孤児化する**（段階1 Task 7 の実害）。各実行後に `pgrep -a qemu` で残存 0 を確認する。
15. `tools/cfg_error_tests/run.sh` の呼出しは
    `run.sh <builddir> <cfg> <期待ercd> [EXTRA_CFLAGS]` の**4引数形**である。
    cfg 内で `#include "test_int2.h"` 等を使うケースは**第4引数
    `EXTRA_CFLAGS="-I<repo>/test"` が必須**で、付けないと `rc=2` になる。
    **本計画では全ケースに引数を明記する。**
16. **★コメントに書く根拠は真でなければならない。**「dcre がそうしているから」は根拠ではない。
    dcre の現物がそうであることは**事実の記録**であって、FMP3 でそうすべき**理由**とは別物である。
    両方を書く場合は区別して書く。段階3a 最終レビュー Important #1 の再発防止。
17. **★実装前確認（Task 1）の spec への文章反映は、本計画の文面を逐語で移す。**
    段階3a Task 1 と段階3b Task 1 で**2 回続けて内容の創作・すり替えが起きた**
    （3a: 訂正Hの内容が別のコントロールに置き換わる等 4 件／3b: 存在しない参照
    `dele_tsk`/`winfo.dtqid` の捏造 1 件 + 訂正F 前半の脱落 1 件）。
    レビューは「計画原文 vs spec 反映後の文面」を **`diff` で直接突き合わせる**。
    「もっともらしいか」で判断しない。
18. **★2 コア構成でしか通らない経路と、1 コア構成でしか通らない経路を取り違えない。**
    段階1で「`TNUM_PRCID == 1` でしか通らない死んだ分岐へ変異を入れて control が
    空振りした」事故が 2 回起きている。本段階の変異 control は
    `musca_b1-2core` で**必ず通る**経路に入れる。

---

## ★★本段階の中核リスク（3 件）

段階3a/3b の申し送りに加えて、本段階固有のリスクが 3 件ある。いずれも
「テストが緑でも壊れていることがありうる」型なので、それぞれに専用の検証を割り当てる。

### リスク1：既存構成のディスパッチが静かに変わる

`ENA_DYNISR` の無い intno は現行のインライン連鎖のままでなければならない。
生成ロジックに手を入れる以上、**opt-in 判定を1つ間違えれば全構成のディスパッチが
キュー方式へ倒れる**。しかもキュー方式でも「ISR は isrpri 順に呼ばれる」ので、
**テストは通ってしまう**（test_int2 も PASS する）。

→ 検証：Task 2 Step 6 で `_kernel_inthdr_*` を含む生成物を**基準と `diff` して 0 行**であることを
実証する。Task 6 Step 11 で `test_int2` の生成物に `call_isr` の文字列が**現れないこと**を
確認する。**「PASS したから大丈夫」を根拠にしない。**

### リスク2：走査の安定キーが効いていないのに通ってしまう

`(isrpri, isrseq)` による走査位置の再決定（spec §5・Codex #1）は、
**単一の ISR しか登録されていない構成では isrpri だけで決めても同じ結果になる**。
すなわち素朴なテストでは安定キーの有無を区別できない。

→ 検証：Task 6 で**同一 isrpri の ISR を 3 本**登録し、かつ ISR 本体の中から
（＝走査の途中で）`del`/`acre` 相当の状態変化を起こす形にする。決定的に組めない部分は
Task 6 Step 9 の**変異 control**（`isrseq` の比較を isrpri だけに退化させると倒れること）で
実演する。★変異 control のほうが本命であることを Task 6 に明記する。

### リスク3：quiesce がデッドロック／ライブロックする

`del_isr` は `p_isrcb->running != 0` の間、**glock と CPU ロックを解放して待つ**。
これは段階1〜3b に前例のない「サービスコールの中で他コアの完了を待つ」構造である。

→ 設計：待機ループは `kernel/wait.c:126-131,152-157`（`wait_tmout`/`wait_tmout_ok`）の
**現物と同一の 5 行**（`release_glock(); unlock_cpu(); delay_for_interrupt(); lock_cpu();
acquire_glock();`）を使う。これは汎用カーネルに既にある正統な待ち方であり、
`delay_for_interrupt()` は 4 アーキすべてに存在する（`arch/arm_m_gcc/common/core_insn.h:192`・
`arch/arm_gcc/common/core_kernel_impl.h:261`・`arch/arm64_gcc/common/core_kernel_impl.h:248`・
`arch/riscv_gcc/common/core_kernel_impl.h:191`）。
→ 検証：Task 6 Step 8 で「長い ISR の実行中に別コアから del_isr し、del が ISR の完了より
後に戻ること」を checkpoint の順序で実証する。

---

## ★spec からの訂正10件（Task 1 で spec に反映してから実装に入ること）

計画作成時に dcre 現物・FMP3 現物・cfg エンジン現物を確認した結果、spec の記述と
現物が食い違う点、および spec が「実装前確認で決める」としていて**計画作成時に
決着がついた**点が 10 件ある。いずれも **Task 1 で spec 本文を直してから**後続タスクに入る。

★★**訂正A〜J の文面は、要約せず・言い換えず・逐語で spec へ移すこと**（Global Constraint 17）。

---

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

**訂正G：`isrseq` の空キューリセットには「走査中に追加された ISR がその回は走らない」という副作用がある。**

spec §3 は「キューが空になったときカウンタを 0 にリセットして実用上の到達不能性を
保証する」と書くが、**副作用を書いていない**。

走査中に（glock を外して ISR 本体を実行している間に）キューが空になり、その後
`acre_isr` された ISR は `isrseq` が 0 から振り直される。走査側の継続キー `cur` は
削除済み ISR の `(isrpri, isrseq)` を保持しているため、**新しい ISR の
`(isrpri, 0)` は `cur` より大きくならず、その割込みでは呼ばれない**（次の割込みで呼ばれる）。

→ これは**安全側の脱落**であり、二重実行は起こらない。リセットはキューが空のときだけ
行われるので、「もとから居た ISR を飛ばす」ことは起こらない。この帰結を spec §3 に明記する。

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
  コード検査、および polarfire_soc_kit / kria_r5 でのビルド・等価性で代替する。
  この不足を Task 7 の申し送りに**正直に**書く。

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

---

## 変更ファイル一覧（全体像）

| 層 | ファイル | 種別 |
|---|---|---|
| spec | `docs/superpowers/specs/2026-08-04-fmp3-dcre-isr-design.md` | 派生・修正 |
| cfg 定義 | `kernel/kernel_api.def`（`AID_ISR` / `ENA_DYNISR`） | **pristine・台帳** |
| cfg Ruby | `kernel/interrupt.trb` | **pristine・台帳** |
| cfg Python | `kernel/interrupt.py` | 派生（`kernel/*.py` 枠） |
| API | `include/kernel.h`（`T_CISR` + 2 宣言） | **pristine・台帳** |
| 共通定義 | `kernel/kernel_impl.h`（`TMIN_ISRID`・`TARGET_ISRATR`） | **pristine・台帳** |
| 共通検査 | `kernel/check.h`（`VALID_ISRID`・`VALID_ISRPRI`） | **pristine・台帳** |
| カーネル | `kernel/interrupt.h` `kernel/interrupt.c` | **pristine・台帳** |
| 配線 | `kernel/allfunc.h` `kernel/Makefile.kernel` | **pristine・台帳** |
| rename | `kernel/kernel_rename.def` →（再生成）`kernel_rename.h` `kernel_unrename.h` | **pristine・台帳** |
| テスト | `test/test_dcre5.{c,cfg,h}` `test/test_dcre_mix.{c,cfg,h}` `test/MANIFEST` `test/testexec.rb` | **pristine・台帳** |
| エラー回帰 | `tools/cfg_error_tests/` に 10 本追加 | 派生 |
| 台帳 | `DIVERGENCE_MAP.md` | 派生 |
| 記録 | `.superpowers/sdd/progress.md` + 本段階ワークスペース | 派生 |

**READ ONLY（読むが変更しない）:**
`kernel/kernel.py` `kernel/kernel.trb`（cfg 共通枠組みは無変更＝Global Constraint 9）。
`kernel/cyclic.c`（`call_cyclic`＝訂正F の先例）。`kernel/time_event.c`（`signal_time`＝
割込み文脈 glock の先例）。`kernel/spin_lock.c`（`force_unlock_spin`）。
`kernel/wait.c`（`wait_tmout` の 5 行＝quiesce ループの先例）。
`include/kernel_fncode.h`（`TFN_ACRE_ISR`/`TFN_DEL_ISR` は既存＝Task 1 Step 1）。
`cfg_py/pass1.py` `cfg_py/pass2.py`（`.def` 文法と `cfgData` 初期化を読むだけ）。

---
### Task 1: 実装前確認（spec §8 の8項目）と spec の訂正10件反映

**推奨モデル:** 最安価（Step 1-9 の現物確認）＋**Step 10 の spec 反映は本計画の文面を
逐語コピーするだけの機械作業**。判断は本計画が既に与えている。

**★★このタスクへの特別な注意（段階3a／段階3b の実害から。2 回連続で起きている）:**

- 段階3a Task 1：最安価モデルが**現物確認と証拠収集は正確**だったにもかかわらず、
  **計画から spec への転記の段階で内容を創作・すり替えた**（4 件。レビュー1回目で全部差し戻し）。
- 段階3b Task 1：**明示禁止下で再発**した。存在しない参照（`dele_tsk` / `winfo.dtqid`）を
  **捏造**（Critical）し、訂正F の前半（`del_dtq` の `CHECK_ID`）を**転記時に脱落**させた（Important）。

→ **本タスクの転記は「本計画の訂正A〜J の文面を、要約せず・言い換えず・逐語で」spec へ移すこと。**
自分の言葉で言い直したくなったら、それは誤りの兆候である。
**新しい file:line 参照を自分で作らない。** 本計画に書かれていない行番号・関数名・
シンボル名を spec に書いてはならない。Step 1-9 の実測で本計画と違う値が出た場合は、
**「計画は X と書いているが実測は Y だった」という形で両方を書く**（黙って差し替えない）。

→ **レビュアーへ：本タスクのレビューは「計画原文 vs spec 反映後の文面」を直接
`diff` で突き合わせること。**「もっともらしいか」で判断しない。
訂正A〜J の 10 件すべてについて、計画本文の段落が spec に**過不足なく**現れていることを確認する。

**Files:**
- Modify: `docs/superpowers/specs/2026-08-04-fmp3-dcre-isr-design.md`
  （§1.3・§2・§3・§4・§5・§6・§7・§8 を訂正し、末尾に §10 を新設）
- Create: なし

**Interfaces（後続 Task が参照する記録）:**
- Produces: 本 Task の**確認結果表**（spec 末尾 `## 10. 実装前確認の結果` として追記）。
  後続 Task は「Task 1 の記録」としてこれを参照する。特に
  (a) `TFN_ACRE_ISR`/`TFN_DEL_ISR` の既存値、
  (b) dcre 転写元の行範囲 6 組、
  (c) FMP3 に無い 4 定義と追加先（訂正J の表）、
  (d) 割込み文脈での glock 取得の 2 先例（`signal_time` / `call_cyclic`）の実測行範囲、
  (e) quiesce ループの先例（`wait_tmout` の 5 行）の実測行範囲、
  (f) `cfgData` が `kernel_api.def` 登録 API のキーを常に持つこと（訂正D の根拠）、
  (g) `kernel_api.def` の `*`（KEYPAR）の意味と `ENA_DYNISR .intno*` を選ぶ理由、
  (h) `isratr` がビット検査に使われないこと（訂正B の根拠）。

**★ゲート条件（BLOCKED 判定）:** 下記のいずれかが成り立ったら、**以降のステップに進まず
発見内容を報告して停止する**（設計が無効になるため）。

- **G1: 割込み文脈から `acquire_glock()` を取る先例が存在しなかった**、または
  `signal_time`（`kernel/time_event.c`）が `lock_cpu()` + `acquire_glock()` を
  **していなかった**。→ spec §5 の走査設計の前提が崩れる。**設計を差し戻す。**
- **G2: `call_cyclic`（`kernel/cyclic.c`）が「glock+CPU ロックを外してハンドラを呼び、
  復帰後に `sense_lock()` 検査つきで取り直す」形でなかった。**
  → 訂正F の根拠が崩れる。**設計を差し戻す。**
- **G3: `ENA_DYNISR` の文法をクラス内 API として `kernel_api.def` に書けない**
  （＝`CFG_INT` のようにクラスの囲みの中に書ける静的 API を新設できない）。
  → 案B-2 が成立しない。**停止して案B-1（`AID_ISR(intno, n)`）への差し戻しを提案する。**
- **G4: `delay_for_interrupt()` が 4 アーキのいずれかに存在しなかった。**
  → quiesce ループの待機プリミティブが無い。**停止して代替（`sil_dly_nse` 等）を提案する。**
- **G5: `isratr` をビット検査またはマスク比較している箇所が dcre または FMP3 に見つかった。**
  → 訂正B（`TA_NOEXS` を quiesce の前に書く）が成立しない。**停止して報告する。**

- [ ] **Step 1: §8-5 機能コードの既存有無**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -n "TFN_ACRE_ISR\|TFN_DEL_ISR" include/kernel_fncode.h > /tmp/isr-t1-fncode.txt
cat /tmp/isr-t1-fncode.txt
grep -c . /tmp/isr-t1-fncode.txt      # 期待: 2
```
期待（計画作成時の実測）: `#define TFN_ACRE_ISR (-204)`＝`:144` /
`#define TFN_DEL_ISR (-220)`＝`:156`。
→ `include/kernel_fncode.h` は**変更不要**。2 値を確認結果表に転記する。

- [ ] **Step 2: §8-1 dcre の ISR ランタイムオブジェクト現物（本段階の転写元）**

```bash
D=/home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/kernel
sed -n '53,136p'  $D/interrupt.h      # ISRINIB/ISRCB/ISR_ENTRY/extern群/ISRID/宣言
sed -n '166,195p' $D/interrupt.c      # tnum_isr/tnum_sisr/INDEX_ISR/get_isrcb/enqueue_isr
sed -n '197,233p' $D/interrupt.c      # free_isrcb / initialize_isr
sed -n '235,262p' $D/interrupt.c      # call_isr
sed -n '264,293p' $D/interrupt.c      # search_isr_queue
sed -n '295,354p' $D/interrupt.c      # acre_isr
sed -n '356,394p' $D/interrupt.c      # del_isr
sed -n '260,346p' $D/interrupt.trb    # queue/table/inthdr/IsrObject/isrorder_table
```
確認して確認結果表へ記録すること（**食い違ったら現行ソースを正とし、実測値を書く**）：

1. `ISRINIB`（`interrupt.h:56-62`）は `isratr` / `exinf` / `QUEUE *p_isr_queue` /
   `isr` / `isrpri` の 5 フィールド。`ISRCB`（`:67-70`）は `QUEUE isr_queue` /
   `const ISRINIB *p_isrinib` の **2 フィールドのみ**（`isrseq`/`running` は FMP3 の新設）。
   `ISR_ENTRY`（`:75-78`）は `INTNO intno` / `QUEUE *p_isr_queue`。
2. `ISRID(p_isrcb)`（`:126`）は **`isrcb_table` からの配列差分**である
   （`((ID)(((p_isrcb) - isrcb_table) + TMIN_ISRID))`）。
   ★FMP3 は CB がポインタ表（named static）なのでこの式は使えない。
   段階2 の `CYCID`・段階3a の `SEMID`・段階3b の `DTQID` と同じく
   **INIB ポインタ差分の 2 レンジ式**に置き換える（Task 3）。
3. `enqueue_isr`（`interrupt.c:182-195`）は「自分より**真に大きい** isrpri の要素の直前」に
   `queue_insert_prev` する。**等しい isrpri のときは進む**ので、同一 isrpri の中では
   enqueue した順に並ぶ。→ 訂正I の invariant の根拠。
4. `initialize_isr`（`:207-231`）の順序：
   全キューを `queue_initialize` → `isrorder_table` の順に静的 ISR を
   `p_isrinib` 結線して `enqueue_isr` → `queue_initialize(&free_isrcb)` →
   動的スロットに `TA_NOEXS` を書いて `queue_insert_prev(&free_isrcb, ...)`（**FIFO**）。
   ★`i` を静的ループから引き継いで動的ループへ渡す書き方（`for (j = 0; i < tnum_isr; i++, j++)`）は
   段階1〜3b と同じである。
5. `call_isr`（`:240-260`）は**素朴な単方向の走査**である：`p_queue = p_queue->p_next` で
   進み、ロックは一切取らない。次要素があるときだけ `if (sense_lock()) { unlock_cpu(); }`。
   ★**単一プロセッサ前提**（走査と acre/del が時間的に排他）であり、
   FMP3 ではこの形は使えない。Task 4 で全面的に書き直す。
6. `search_isr_queue`（`:267-293`）は `tnum_isr_queue == 0` で NULL を返し、
   `isr_queue_list[]` を**二分探索**する。★この関数は**そのまま移植できる**
   （型が `ISRQCB *` に変わるだけ。コア非依存＝Codex #3 の解）。
7. `acre_isr`（`:303-352`）の順序：`CHECK_TSKCTX_UNL()` → パラメータをローカルへ →
   `CHECK_VALIDATR` / `CHECK_PAR(VALID_INTNO_CREISR)` / `CHECK_PAR(FUNC_ALIGN)` /
   `CHECK_PAR(FUNC_NONNULL)` / `CHECK_PAR(VALID_ISRPRI)` →
   `search_isr_queue` → **`CHECK_OBJ(p_isr_queue != NULL)`** → `lock_cpu()` →
   `E_NOID`（`tnum_isr == 0 || queue_empty`）→ `queue_delete_next` で CB を pop →
   INIB を埋める → `enqueue_isr` → `ercd = ISRID(p_isrcb)`。
   ★`exinf` だけはローカルにコピーせず `pk_cisr->exinf` を直接使う
   （`:298-299` のコメントが理由を書いている）。**この流儀を維持する。**
   ★**`CHECK_OBJ` は FMP3 の `kernel/check.h` に無い**（dcre `check.h:235-240` にはある）。
   → Task 3 で追加するか、`if`/`goto error_exit` を直書きするかを Step 8 で決める。
8. `del_isr`（`:361-392`）の順序：`CHECK_TSKCTX_UNL()` → `CHECK_ID(VALID_ISRID)` →
   `get_isrcb` → `lock_cpu()` → `E_NOEXS` → `E_OBJ`（`isrid <= tmax_sisrid`）→
   `queue_delete` → `isratr = TA_NOEXS` → `queue_insert_prev(&free_isrcb, ...)`。
   ★**dcre には quiesce が無い**（単一プロセッサなので構造的に不要）。
   ★**dcre は `CHECK_ID`（E_ID）である**。段階3a 訂正D（`del_flg`）・段階3b 訂正F（`del_dtq`）の
   ような `CHECK_PAR` の不整合は `del_isr` には**無い**。上流報告候補 d は拡張しない。
9. `interrupt.trb:263-294`：適格 intno は「`INTNO_CREISR_VALID` に含まれ、`CFG_INT` があり、
   `DEF_INH` が無い」ものすべて。**dcre は opt-in を持たない**（全部キュー化する）。
   `:299-316`：inthdr は `_kernel_call_isr(&(_kernel_isr_queue_table[i]));` の 1 行。
   `:321-336`：`IsrObject < KernelObject`。`:338-346`：`isrorder_table`。
   ★**`isrorder_table` の生成に `TNUM_SISRID == 0` のガードが無い**（`[0]` 配列になる）。
   FMP3 では他の表と同様に `TOPPERS_EMPTY_LABEL` でガードする（Task 2）。

- [ ] **Step 3: §8-2 FMP3 の inthdr 生成機構（切替の結合点）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
sed -n '411,477p' kernel/interrupt.trb
sed -n '370,443p' kernel/interrupt.py
sed -n '50,60p'   kernel/interrupt.trb    # kernel_cfg.h への TNUM_ISRID / ISRID 定義
sed -n '54,61p'   kernel/interrupt.py
grep -n "affinityPrcList\|initPrc" target/musca_b1_gcc/target_class.trb
```
確認すること（計画作成時の実測）：
- 生成ループは **prcid 1..TNUM_PRCID の外側ループ × `INTNO_CREISR_VALID[prcid]` の内側ループ**で、
  各 intno について `CRE_ISR` を集めた `isrParamsList` が空なら**何もしない**。
- `isrParamsList` が空でないとき、`clsData[clsid]["affinityPrcList"]` に prcid が
  含まれなければ `continue`（＝**割込みを受け付けるプロセッサでない場合はスキップ**）。
- `cfgData["DEF_INH"][inhnoVal]` を**prcid ごとに**生成する（＝affinity が複数コアなら
  複数の DEF_INH が出る）が、**`inthdr` の本体は `isr_flag[intnoVal]` で 1 回だけ生成**する。
- 本体は `PCB *p_my_pcb = get_my_pcb();`（ISR が 2 本以上のときだけ）+
  isrpri 昇順の直接呼出し + ISR 間の
  `if (_kernel_sense_lock()) { _kernel_force_unlock_spin(p_my_pcb); _kernel_unlock_cpu(); }`。
- ソートは `sorted(cfgData["CRE_ISR"].items())`（isrid 昇順）を基底とする
  **`isrpri` の安定ソート**（Ruby 版は `i += 1` を第 2 キーにして stable sort を明示）。
  → **訂正I の根拠**。
- `kernel_cfg.h` への出力は `#define TNUM_ISRID <n>` と `#define <ISRID名> <値>` と
  空行 1 個の**3 種類だけ**であり、`interrupt.py`/`interrupt.trb` の中で
  `kernelCfgH` に書くのは**この 1 箇所のみ**である
  （→ この出力を `IsrObject.generate()` に置き換えても `kernel_cfg.h` の
  他の内容との相対位置は変わらない。Task 2 の前提）。

★**この Step の結論を確認結果表に書く**：切替は「`isrParamsList.size > 0` で入る現行の枝」に
**opt-in intno のための枝を並べる**形にする。現行の枝には手を入れない
（Global Constraint 3）。opt-in intno は `isrParamsList` が**空でも** DEF_INH と inthdr を
生成しなければならない（静的 ISR が 0 本の動的専用 intno があり得るため）。
そのとき `clsid` は `CRE_ISR` から取れないので **`cfgData["CFG_INT"][intnoVal]["class"]`** から取る。
★これは既存経路と同値である：`CRE_ISR` と `CFG_INT` が同一クラスであることは
`interrupt.py:355-358`（`interrupt.trb:391-394`）が E_RSATR で保証している。

- [ ] **Step 4: §8-4 型・マクロの FMP3 既存有無（訂正J）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -n "TMIN_ISRPRI\|TMAX_ISRPRI" include/kernel.h
grep -n "TMIN_ISRID\|TARGET_ISRATR" kernel/kernel_impl.h include/kernel.h   # 期待: 0行
grep -n "VALID_ISRID\|VALID_ISRPRI\|CHECK_OBJ" kernel/check.h               # 期待: 0行
grep -n "typedef void	(\*ISR)" include/kernel.h
grep -n "typedef	uint_t		INTNO" include/kernel.h
grep -n "TARGET_ISRATR\|TARGET_TSKATR" kernel/kernel_sym.def
grep -n "TARGET_TSKATR" kernel/kernel_impl.h
grep -n "define TMIN_SPNID\|define TMIN_ALMID" kernel/kernel_impl.h
D=/home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/kernel
sed -n '62,66p;70,76p' $D/check.h
sed -n '153,158p' $D/kernel_impl.h
sed -n '125,129p' $D/kernel_impl.h
```
期待（実測）:
- `TMIN_ISRPRI 1` / `TMAX_ISRPRI 16` は `include/kernel.h:654-655` に**ある**。
- `TMIN_ISRID` / `TARGET_ISRATR` は `kernel/kernel_impl.h` に**無い**
  （`TMIN_SPNID` が `:194`、`TARGET_TSKATR` が `:209-211`）。
- `VALID_ISRID` / `VALID_ISRPRI` / `CHECK_OBJ` は `kernel/check.h` に**無い**。
- `ISR` 型（`include/kernel.h:114`）と `INTNO` 型（`:105`）は**ある**。
- `kernel_sym.def:73` が `TARGET_ISRATR,,,defined(TARGET_ISRATR),0` で、
  `:69` の `TARGET_TSKATR` が同じ形（かつ C 側には定義がある）＝**先例が既にある**。
→ **訂正J** の表を確認結果表へそのまま転記する。**`CHECK_OBJ` の不在も追記する**
（訂正J の表には無い 5 件目。Step 8 で扱いを決める）。

- [ ] **Step 5: §8-3 割込み文脈での glock 作法と `isratr` のビット検査不在（★ゲート G1/G2/G5）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
sed -n '706,770p' kernel/time_event.c      # signal_time
sed -n '520,552p' kernel/cyclic.c          # call_cyclic
sed -n '158,178p' kernel/spin_lock.c       # force_unlock_spin
grep -rn "isratr" kernel/ include/ | grep -v "\.py:" | grep -v "\.trb:"
D=/home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/kernel
grep -n "isratr" $D/interrupt.c $D/interrupt.h
```
確認すること：
1. **（G1）** `signal_time`（`kernel/time_event.c:709-768`）が
   `assert(sense_context(p_my_pcb)); assert(!sense_lock()); lock_cpu(); acquire_glock();`
   で始まり `release_glock(); unlock_cpu();` で終わること。
   → **割込み文脈で glock を取る作法の先例**。`call_isr` はこれに倣う。
2. **（G2）** `call_cyclic`（`kernel/cyclic.c:518-548`）が
   `release_glock(); unlock_cpu();` → ハンドラ呼出し →
   `if (sense_lock()) { force_unlock_spin(p_my_pcb); } else { lock_cpu(); } acquire_glock();`
   であること。→ **訂正F の根拠**。★`sense_lock()` が真のときに `lock_cpu()` を
   **呼んでいない**ことを目で確かめる（これが 3 分岐である理由）。
3. `force_unlock_spin`（`kernel/spin_lock.c:162-176`）が
   **スピンロックだけを解放し CPU ロックには触らない**こと
   （関数内コメント `/* ここではCPUロック状態になっている */`）。
4. **（G5・訂正B の根拠）** `isratr` が **`== TA_NOEXS` の同値比較と代入にしか
   現れない**こと。FMP3 側は現状 0 件（ランタイムオブジェクトが無いので当然）、
   dcre 側は `interrupt.h:57`（宣言）・`interrupt.c:339`（代入）・`:374`（同値比較）・
   `:383`（代入）の 4 箇所だけで、**`&` によるビット検査が 1 つも無い**こと。
   → **これが真であることを確認してから**、Task 5 の `del_isr` に
   「ISR の isratr はビット検査に使われないので TA_NOEXS を quiesce の前に書ける」という
   コメントを書く。**確認せずに書かない**（段階3a Important #1 の教訓）。

- [ ] **Step 6: §8-6 quiesce の待機プリミティブ（★ゲート G4）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
sed -n '105,162p' kernel/wait.c                 # wait_tmout / wait_tmout_ok
grep -rn "delay_for_interrupt" arch/ kernel/ | grep -v "^doc/"
sed -n '188,196p' arch/arm_m_gcc/common/core_insn.h
sed -n '178,196p' arch/arm_m_gcc/musca_b1/chip_kernel_impl.h   # lock_native_spn の再試行ループ
grep -n "delay_for_interrupt" -B4 -A4 doc/porting.txt | head -20
```
期待（実測）:
- `wait_tmout`（`kernel/wait.c:109-131`）と `wait_tmout_ok`（`:138-160`）が
  末尾に**まったく同じ 5 行**を持つ：
  `release_glock(); unlock_cpu(); delay_for_interrupt(); lock_cpu(); acquire_glock();`
  直前のコメントは `/* ここで優先度の高い割込みを受け付ける． */`。
  → **quiesce ループの本体はこの 5 行と同一にする**（汎用カーネルに既にある正統な待ち方）。
- `delay_for_interrupt()` が 4 アーキすべてに存在する
  （`arch/arm_m_gcc/common/core_insn.h:192` /
  `arch/arm_gcc/common/core_kernel_impl.h:261` /
  `arch/arm64_gcc/common/core_kernel_impl.h:248` /
  `arch/riscv_gcc/common/core_kernel_impl.h:191`）。
  → **1 つでも欠けていたら G4 で停止する。**
- `arch/arm_m_gcc/musca_b1/chip_kernel_impl.h:184-192` の `lock_native_spn` が
  `while (try_lock(...)) { unlock_cpu(); delay_for_interrupt(); lock_cpu(); }` で、
  そのコメントが「**取得できない場合は，一旦割込みを許可した後，再び割込みを禁止した後に**
  再試行する」という `doc/porting.txt (6-21-3-2)` の要求を引用していること。
  → **「ロックを外して待ち、割込みを通し、取り直す」がこの実装の正統な作法**であることの補強。
→ `sil_dly_nse` は**使わない**ことを確認結果表に書く（ターゲット依存の時間定数を
カーネル内部に持ち込む必要がなく、`delay_for_interrupt` のほうが先例として正しい）。

- [ ] **Step 7: §8-7 `isrseq` の空キューリセットの実装点**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
sed -n '70,160p' include/queue.h        # queue_initialize/insert_prev/delete/delete_next/empty
```
確認すること：
- `queue_empty(QUEUE *p_queue)`（`include/queue.h:151-`）が
  `p_queue->p_next == p_queue` で空を判定すること。
- `queue_delete(QUEUE *p_entry)`（`:119-`）が**エントリ自身のリンクを作り直さない**こと
  （削除後の `p_entry->p_next`/`p_prev` は古い値のまま＝訂正B の二重 `queue_delete` が
  危険である理由）。
→ **結論**：リセットは `enqueue_isr()` の**先頭**、`queue_empty(&(p_isrq->isr_queue))` が
真のときに `p_isrq->isrseq = 0U;` とする（Task 3）。
`del_isr` 側には置かない（削除は「空にする」以外の経路もあり、判定点が増えるだけで
利得が無い）。この判断と根拠を確認結果表に書く。

- [ ] **Step 8: §8-8・cfg 層の前提（★ゲート G3）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cat kernel/kernel_api.def
sed -n '96,140p' cfg_py/pass1.py        # .def の prefix/postfix の意味（KEYPAR/OPTIONAL）
sed -n '175,192p' cfg_py/pass2.py       # cfgData を全APIのキーで初期化する箇所
sed -n '380,396p' cfg_py/pass2.py       # KEYPAR による登録キーと重複検出
sed -n '165,175p' cfg/pass2.rb          # Ruby 側の同じ初期化
sed -n '108,145p' kernel/kernel.py      # KernelObject.__init__ / inibList
sed -n '134,200p' kernel/kernel.py      # generate（has_aid / 訂正Eガード / tmax_s / inibSizeToken）
sed -n '199,278p' kernel/kernel.py      # データ構造・予約CB・aX inib_table
grep -rn "CRE_ISR" --include=*.cfg . | grep -v "^./build" | sort
```
確認すること（計画作成時の実測）：
1. **（G3）** `kernel_api.def` の文法で `.intno*` と書けば
   「符号無し整数定数式パラメータ」かつ「登録キー」になる（`cfg_py/pass1.py:106-131`）。
   `CFG_INT .intno* { .intatr +intpri }`（`:12`）が先例である。
   → `ENA_DYNISR .intno` と書く（**`*` を付けるかは下記 2 で決める**）。
   ★**クラス内 API であることは `.def` では表現しない**。`CFG_INT` も `CRE_ISR` も
   `.def` にはクラスの情報が無く、**クラスの囲みの中にあるかどうかは
   `params` に `class` キーがあるかで `.trb`/`.py` 側が検査している**
   （`kernel/interrupt.py:129-132` の `if "class" not in params:` → E_RSATR）。
   → **G3 は成立しない見込み**だが、`.def` に `ENA_DYNISR` を足して
   `CLASS(...) { ENA_DYNISR(...); }` が構文エラーにならないことを Task 2 Step 3 で
   実際に確かめる。**確かめる前に「書けるはず」で先へ進まない。**
2. `KEYPAR` を付けると `cfg_py/pass2.py:385-391` が**同じ intno への 2 回目の
   `ENA_DYNISR` を E_OBJ で弾く**（`E_OBJ: intno \`X' is duplicated in ENA_DYNISR`）。
   → **`ENA_DYNISR .intno*`（`*` 付き）にする**。重複検査がタダで手に入り、
   `cfgData["ENA_DYNISR"]` が intno 値をキーとする dict になるので
   後段の `in` 判定が O(1) になる。この判断と根拠を確認結果表に書く。
3. **（訂正D の根拠）** `cfg_py/pass2.py:181-186` と `cfg/pass2.rb:165-172` が
   **`apiDefinition` の全 API について `cfgData[api_sym] = {}` を先に作る**こと。
   → `kernel.py:142` の `has_aid = self.aidapi in cfgData` は
   `AID_ISR` を `.def` に足した時点で**恒真**になる。
4. 汎用枠組み（`kernel.py:109-277` / `kernel.trb:115-295`）が
   `obj="isr"` から `TNUM_ISRID` / `TNUM_SISRID` / `_kernel_tmax_isrid` /
   `_kernel_tmax_sisrid` / `const ISRINIB _kernel_isrinib_table[TNUM_SISRID]` /
   `static ISRCB _kernel_isrcb_<ISRID>` / `static ISRCB _kernel_aisrcb_<i>` /
   `ISRCB *const _kernel_p_isrcb_table[TNUM_ISRID]` / `ISRINIB _kernel_aisrinib_table[n]` /
   `_kernel_initialize_isr(p_my_pcb);` を**機械的に導く**こと。
   `inibList` の既定が `{"ISRINIB": "aisrinib_table"}` であること（`kernel.py:123`）。
5. 静的 `CRE_ISR` を含む `.cfg`：`sample/sample1.cfg`（6 個）・
   `target/polarfire_soc_kit_gcc/softconsole/sample1/sample1.cfg`（4 個）・
   `target/musca_b1_gcc/target_serial.cfg`（`ISR_SIO`）・
   各 `arch/*/*/chip_serial.cfg`（`ISR_SIO`）・
   `test/test_int2.cfg`（3 個）・`tools/cfg_error_tests/e_par_creisr_intno_keyerror.cfg`。
   ★**`serial.cfg` 経由の `CRE_ISR(ISR_SIO)` が `test_common1.cfg` を INCLUDE する
   全テストに入る**（＝訂正E のガード 2 は通常自動的に満たされる。逆に
   「静的 `CRE_ISR` が 0 個」の cfg を作るには `serial.cfg` を避ける必要がある＝
   段階3a の `dcre_aid_sem_no_static.cfg` と同じ回避が要る）。
6. `CHECK_OBJ` の扱いを決める：**`kernel/check.h` に dcre `check.h:235-240` と
   同一の `CHECK_OBJ` を追加する**（Task 3）。理由：`acre_isr` 以外にも将来使われうる
   汎用マクロであり、`CHECK_PAR`/`CHECK_ILUSE` と同じ形の 6 行で、
   段階3b の `VALID_DPRI` 追加と同じ前例に載る。**直書きの `if`/`goto` にしない**
   （FMP3 のサービスコールは検査をマクロで書く流儀で統一されている）。

- [ ] **Step 9: musca_b1 の割込みモデル（★訂正H の根拠）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
sed -n '13,45p' target/musca_b1_gcc/target_kernel.trb
sed -n '13,34p' target/musca_b1_gcc/target_class.trb
cat target/musca_b1_gcc/target_test.h
sed -n '5,25p' arch/riscv_gcc/polarfire_soc/chip_kernel.trb
sed -n '5,25p' arch/arm_gcc/zynq7000/chip_kernel.trb
cat test/test_int2.cfg
grep -n "INTNO1\|ras_int\|intno1_clear" test/test_int2.c
cat tools/cfg_error_tests/musca_b1_e_rsatr_intno_affinity.cfg
```
確認すること：
- musca_b1 は `$INTNO_VALID[prcid].push((prcid << 16) | intno)`（`target_kernel.trb:21-22`）で
  **intno にプロセッサ ID が符号化される**＝`INTNO_CREISR_VALID[1]` と `[2]` は要素を共有しない。
- `clsData`（`target_class.trb:24-33`、2 コア時）：`CLS_PRC1`=[1] / `CLS_PRC2`=[2] /
  `CLS_ALL_PRC1`=[1,2] / `CLS_ALL_PRC2`=[1,2]。
  → affinity が 2 コアのクラスに `CFG_INT` を書くと E_RSATR
  （`musca_b1_e_rsatr_intno_affinity.cfg` が固定している）。
- `INTNO1 = ((1U << 16) | (60U + 16U))`（PRC1・予備 NVIC IRQ60）と
  `INTNO2 = ((2U << 16) | (60U + 16U))`（PRC2・同じ生 IRQ）が
  `target/musca_b1_gcc/target_test.h:43,49` に**ある**。`intno1_clear()` は空マクロ
  （NVIC のソフト pend はハンドラ入口で自動クリアされる）。
- `test_int2` は `ras_int(INTNO1)` で割込みを起こす（`test/test_int2.c` の `task1`）。
  ★**`ras_int` は自コアの NVIC しか操作しない**（`target_test.h` のコメントが明記）＝
  `INTNO2` を起こすには **PRC2 上のタスクから `ras_int(INTNO2)` を呼ぶ**必要がある。
- 対照的に polarfire_soc（`chip_kernel.trb:15-18`）と zynq7000/zynqmp_r5 の
  global 割込みは **intno が全プロセッサで同じ値**＝
  同一キューを複数コアが走査する構成が**作れる**。
→ **訂正H** を確認結果表へそのまま転記する。

- [ ] **Step 10: spec への反映（訂正10件 + §10 新設）**

★**本計画の訂正A〜J の文面を逐語で移す。要約・言い換え・新しい参照の創作をしない。**

  - §1.3 の `acre_isr` のエラー節を **訂正A** で置き換える（`VALID_INTNO_CREISR` は
    FMP3 に無い／範囲検査を置かない／4 種類の不適格 intno はすべて E_OBJ）。
  - §2 に **訂正D**（恒常出力の規模と受容の 2 つの理由・`size` 実測を Task 2 に課すこと）と
    **訂正E**（ガード 2 件とその理由）を追記する。
  - §2 の「`_kernel_isr_queue_table[]`（QUEUE 配列）」を **訂正C** の `ISRQCB` に置き換える。
  - §2 に **訂正I**（`isrorder_table` は isrid 昇順・理由は opt-in の有無で呼出し順が
    変わらないこと）を追記する。
  - §3 の `ISRINIB`/`ISRCB` の定義に `ISRQCB`（訂正C）を足し、
    `p_isr_queue` の型を `ISRQCB *` に直す。
  - §3 に **訂正G**（空キューリセットの副作用＝走査中に追加された ISR はその回は走らない。
    安全側の脱落であり二重実行は起こらない）を追記する。
  - §4 の実装手順 1〜3 を **訂正B** で置き換える（正しい順序は
    unlink → `TA_NOEXS` → quiesce → free-list。二重 `del_isr` の競合が理由。
    ISR の `isratr` はビット検査に使われないので早く書いてよい）。
  - §5 の擬似コードのロック復元部分を **訂正F** で置き換え（3 分岐）、
    走査終了時に必ず CPU ロック解除状態で戻ること、およびインライン連鎖との
    意図的な差（最後の ISR が残したロックが漏れない）を追記する。
  - §6 に **訂正H** を追記する（quiesce は PRC2 の ISR × PRC1 の del_isr で実証可能／
    同一キューの 2 コア同時走査は musca_b1 で到達不能／polarfire・zynq では到達可能）。
  - §8-4 を **訂正J** の表で置き換える（`CHECK_OBJ` の不在も 5 件目として書く）。
  - §7（案B-1 との比較）は**変更しない**。
  - §9（統治）の「8タスク構成想定」を **7 タスク構成に確定**させる。
  - spec 末尾に `## 10. 実装前確認の結果（2026-08-04 実測）` を新設し、
    Step 1-9 の確認結果を**表**で記録する（後続 Task はここを参照する）。
    Step 2 の dcre 行範囲 6 組・Step 4 の訂正J の表・Step 9 の割込みモデル比較は
    **そのまま**転記する。

- [ ] **Step 11: コミット**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add docs/superpowers/specs/2026-08-04-fmp3-dcre-isr-design.md
git commit -m "docs(spec): dcre動的ISRの実装前確認と訂正10件（VALID_INTNO_CREISR不在・del_isrのTA_NOEXS先行・ISRQCB新設・恒常出力の規模受容・ENA_DYNISRガード2件・ロック復元の3分岐・空キューリセットの副作用・musca_b1では2コア同時走査が到達不能・isrorder_tableはisrid昇順・欠落4定義）"
```

---
### Task 2: cfg 両エンジン — AID_ISR / ENA_DYNISR とキュー方式ディスパッチの生成

**推奨モデル:** 中位（sonnet）。**本段階で最も広い許容差分リストの厳密判定**と、
既存生成物のバイト不変性の実証がある。

**Files:**
- Modify: `kernel/kernel_api.def`（2 行追加・pristine）
- Modify: `kernel/interrupt.trb`（Ruby オラクル・pristine）
- Modify: `kernel/interrupt.py`（Python 製品・派生）
- Modify: `include/kernel.h`（`T_CISR` + 2 宣言・pristine）
- Modify: `kernel/kernel_impl.h`（`TMIN_ISRID`・`TARGET_ISRATR`・pristine）
- Modify: `test/test_dcre_mix.{c,cfg,h}`（8 家族目の混在・pristine）
- Create: `tools/cfg_error_tests/` に 10 本
- Modify: `DIVERGENCE_MAP.md`

**★この Task で `kernel/kernel.py` `kernel/kernel.trb` を編集してはならない**
（Global Constraint 9）。編集したくなったら、それは共通枠組みの理解が誤っている合図である。

**Interfaces（後続 Task が依存する生成物）:**
- Consumes: Task 1 の確認結果表（訂正A/C/D/E/I/J、`kernel_api.def` の文法、
  `cfgData` の全 API キー初期化、musca_b1 の割込みモデル）。
- Produces（`kernel_cfg.h`）: `#define TNUM_ISRID <総数>`（意味変更：静的+AID）、
  `#define <ISRID名> <値>`（従来どおり）。
- Produces（`kernel_cfg.c`・恒常）:
  `const ID _kernel_tmax_isrid` / `#define TNUM_SISRID <静的個数>` /
  `const ID _kernel_tmax_sisrid` /
  `const ISRINIB _kernel_isrinib_table[TNUM_SISRID]`（0 個なら `TOPPERS_EMPTY_LABEL`）/
  `static ISRCB _kernel_isrcb_<ISRID>;` / `static ISRCB _kernel_aisrcb_<i>;` /
  `ISRCB *const _kernel_p_isrcb_table[TNUM_ISRID]` /
  `ISRINIB _kernel_aisrinib_table[n]`（0 なら `TOPPERS_EMPTY_LABEL`）/
  `const ID _kernel_isrorder_table[TNUM_SISRID]`（0 なら `TOPPERS_EMPTY_LABEL`）/
  `const uint_t _kernel_tnum_isr_queue = <K>;` /
  `const ISR_ENTRY _kernel_isr_queue_list[K]`（0 なら `TOPPERS_EMPTY_LABEL`）/
  `ISRQCB _kernel_isr_queue_table[K]`（0 なら `TOPPERS_EMPTY_LABEL`）/
  `_kernel_initialize_isr(p_my_pcb);` を初期化関数列に追加。
- Produces（`kernel_cfg.c`・opt-in した intno のみ）:
  `void _kernel_inthdr_<intno>(void) { _kernel_call_isr(&(_kernel_isr_queue_table[i])); }`
  と、そのクラスの affinity に含まれる**全 prcid ぶんの `DEF_INH` エントリ**。
- Produces（`include/kernel.h`）: `T_CISR` と `acre_isr`/`del_isr` の `extern` 宣言。
- Produces（`kernel/kernel_impl.h`）: `TMIN_ISRID`（Task 3 が `ISRID`/`INDEX_ISR` で使う）、
  `TARGET_ISRATR`（Task 5 が `CHECK_VALIDATR` で使う）。
- Produces（Python 内部・Task 3/4/5 が型を合わせる相手）: `ISRQCB` / `ISR_ENTRY` /
  `ISRINIB` / `ISRCB` という**型名そのもの**。★Task 3 がこれらを `kernel/interrupt.h` に
  定義する。**cfg 側が出力する初期化子の要素順と、Task 3 の構造体のフィールド順が
  一致していなければコンパイルエラーになる**（`ISRINIB` = `isratr`, `exinf`,
  `p_isr_queue`, `isr`, `isrpri` の順）。

- [ ] **Step 1: 基準生成物の保存（管理された差分の比較元）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core > /tmp/isr-t2-base-build.log 2>&1; echo "rc=$?"
rm -rf /tmp/isr-base-generated
cp -r build/musca_b1-2core/generated /tmp/isr-base-generated
size build/musca_b1-2core/*.elf > /tmp/isr-t2-base-size.txt 2>&1 || \
  find build/musca_b1-2core -name '*.elf' -exec size {} \; > /tmp/isr-t2-base-size.txt
cat /tmp/isr-t2-base-size.txt

cmake --build build/musca_b1-2core-tint2 > /tmp/isr-t2-base-int2.log 2>&1; echo "rc=$?"
rm -rf /tmp/isr-base-int2-generated
cp -r build/musca_b1-2core-tint2/generated /tmp/isr-base-int2-generated

cmake --build build/musca_b1-2core-tmix > /tmp/isr-t2-base-tmix.log 2>&1; echo "rc=$?"
rm -rf /tmp/isr-base-tmix-generated
cp -r build/musca_b1-2core-tmix/generated /tmp/isr-base-tmix-generated
```
（ビルドディレクトリが無ければ Task 6 Step 12 のコマンド形で作る。無かった事実を記録すること。）
★`build/musca_b1-2core-tint2` は**インライン連鎖のバイト不変性を測る本命の比較元**である。
必ず保存すること。

- [ ] **Step 2: `kernel/kernel_api.def` に 2 行追加**

`AID_MPF .nompf`（末尾）の次に：

```
AID_ISR .noisr
ENA_DYNISR .intno*
```

- `AID_ISR .noisr`：`AID_TSK .notsk` 〜 `AID_MPF .nompf` と同じ形。共通枠組みが
  `self.noobj = "no" + obj` で参照するため、パラメータ名は **`noisr` でなければならない**
  （`kernel/kernel.py:121`）。
- `ENA_DYNISR .intno*`：`*`（KEYPAR）を付ける。理由は Task 1 Step 8-2 のとおり
  ——同一 intno への 2 回目の `ENA_DYNISR` が `cfg_py/pass2.py:385-391` で
  自動的に E_OBJ になり、`cfgData["ENA_DYNISR"]` が intno 値をキーとする dict になる。
  `CFG_INT .intno* { ... }`（`:12`）と同じ流儀である。
★`kernel_api.def` は **Ruby / Python 両エンジンの共通入力**なので、
この 1 ファイルの変更で「両エンジン同時変更」（Constraint 5）の一部を満たす。

- [ ] **Step 3: ★`ENA_DYNISR` がクラスの囲みの中に書けることの即時確認（ゲート G3 の実測）**

先へ進む前に、**最小の cfg で構文が通ることだけ**を確かめる。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cat > /tmp/isr-t3-syntax.cfg <<'EOF'
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CFG_INT(INTNO1, { INTNO1_INTATR, INTNO1_INTPRI });
	CRE_ISR(SYNTAX_ISR, { TA_NULL, 1, INTNO1, isr1, 1 });
	ENA_DYNISR(INTNO1);
}
EOF
cp /tmp/isr-t3-syntax.cfg tools/cfg_error_tests/isr_syntax_probe.cfg
tools/cfg_error_tests/run.sh build/musca_b1-2core \
    tools/cfg_error_tests/isr_syntax_probe.cfg "" "-I$PWD/test" \
    > /tmp/isr-t2-syntax.log 2>&1; echo "rc=$?"
grep -n "E_\|error\|Error\|syntax" /tmp/isr-t2-syntax.log | head -20
rm -f tools/cfg_error_tests/isr_syntax_probe.cfg
```
期待: **`ENA_DYNISR` を構文として受理する**こと（`ENA_DYNISR` が未知の静的 API である、
という種類のエラーが出ないこと）。この時点では `interrupt.py`/`.trb` に処理が無いので
`ENA_DYNISR` は無視され、エラーは出ないか、既存の理由によるエラーのみのはずである。
- ★**`ENA_DYNISR` が構文エラーになったら、ゲート G3 が成立した**＝案B-2 が
  この cfg エンジンでは実装できない。**先へ進まず停止し、案B-1 への差し戻しを提案する。**
- rc=2 は前提未充足（第4引数の付け忘れ等）。rc の値そのものではなく
  **ログの中身**を見て判断すること（Constraint 12）。
★探り用 cfg は必ず削除する（回帰列に混ざると Task 7 の本数が合わなくなる）。

- [ ] **Step 4: `kernel/kernel_impl.h` に `TMIN_ISRID` と `TARGET_ISRATR` を追加（訂正J）**

`#define TMIN_SPNID		1		/* スピンロックIDの最小値 */`（`:194`）の**直後**に 1 行：

```c
#define TMIN_ISRID		1		/* 割込みサービスルーチンIDの最小値 */
```

（★dcre は `TMIN_ALMID` の直後に置く（`dcre kernel_impl.h:127`）が、FMP3 は
`TMIN_ALMID` の後ろに `TMIN_SPNID` がある。**既存行の間に割り込ませず末尾に足す**ことで
差分を 1 行に閉じる。この判断を台帳に書く。）

`TARGET_TSKATR` のブロック（`:209-211`）の**直後**に、同じ形で追加する：

```c
#ifndef TARGET_ISRATR
#define TARGET_ISRATR		0U		/* ターゲット定義のISR属性 */
#endif /* TARGET_ISRATR */
```

（dcre `kernel_impl.h:155-157` と同一。★`kernel/kernel_sym.def:73` の
`TARGET_ISRATR,,,defined(TARGET_ISRATR),0` により cfg 側も同じ 0 を得るので、
**生成物は 1 バイトも変わらないはずである** — Step 6 の管理された差分がそれを実証する。）

- [ ] **Step 5: `include/kernel.h` に `T_CISR` と 2 宣言を追加（★dcre からバイト転写）**

`typedef struct t_rmpf {`（`include/kernel.h:295` 付近）と同じ流儀で、
**`extern ER dis_int(INTNO intno) throw();` を含む割込み管理機能の宣言ブロックの直前**に
`T_CISR` を置く。**行番号ではなく文字列を目印にする**（段階3b までで行番号は動いている）。

dcre `include/kernel.h:322-328` からそのまま転記する
（**インデントはタブ。下記の空白はタブに置き換えること**）：

```c
typedef struct t_cisr {
	ATR			isratr;		/* 割込みサービスルーチン属性 */
	EXINF		exinf;		/* 割込みサービスルーチンの拡張情報 */
	INTNO		intno;		/* 割込みサービスルーチンを登録する割込み番号 */
	ISR			isr;		/* 割込みサービスルーチンの先頭番地 */
	PRI			isrpri;		/* 割込みサービスルーチン優先度 */
} T_CISR;
```

**検証（転写が正しいこと）:**
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
D=/home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre
diff <(sed -n '/^typedef struct t_cisr {/,/^} T_CISR;/p' $D/include/kernel.h) \
     <(sed -n '/^typedef struct t_cisr {/,/^} T_CISR;/p' include/kernel.h); echo "cisr diff rc=$?"
```
期待: **rc=0（バイト一致）**。1 行でも差が出たら転写ミスである。

続いて `extern ER		dis_int(INTNO intno) throw();`（`:494` 付近）の**直前**に：

```c
extern ER_ID	acre_isr(const T_CISR *pk_cisr) throw();
extern ER		del_isr(ID isrid) throw();
```

（dcre `include/kernel.h:481-482` と同一で、dcre 自身が `ER_ID` を使っている＝
段階1〜3b のような `ER_UINT`→`ER_ID` の逸脱は**本段階では発生しない**。
定義側（`interrupt.c`）も `ER_ID` にする。この事実を台帳に 1 文書く。）

機能コードは Task 1 Step 1 の記録どおり 2 件とも既存＝`include/kernel_fncode.h` は**変更しない**。

- [ ] **Step 6: ★★管理された差分の検査（第一段階：カーネル側の定義追加だけで生成物が変わらないこと）**

Step 4-5 は C ヘッダの変更だけであり、**cfg の生成物を 1 バイトも変えてはならない**。
`TARGET_ISRATR` を定義したことが `CRE_ISR` の `E_RSATR` 判定に影響していないことを、
ここで**先に**確かめる（後段の大きな差分に紛れさせない）。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core       > /tmp/isr-t2-s6-a.log 2>&1; echo "a rc=$?"
cmake --build build/musca_b1-2core-tint2 > /tmp/isr-t2-s6-b.log 2>&1; echo "b rc=$?"
diff -r /tmp/isr-base-generated       build/musca_b1-2core/generated;       echo "gen diff rc=$?"
diff -r /tmp/isr-base-int2-generated  build/musca_b1-2core-tint2/generated; echo "int2 diff rc=$?"
```
期待: **両方とも rc=0（差分ゼロ）**。
差分が出たら `TARGET_ISRATR` の値が 0 以外になっている（＝`kernel_sym.def` の
評価文脈に `kernel_impl.h` が入っている）ので、**追加を取り消して Task 1 の
確認結果表を訂正し、報告する**。

- [ ] **Step 7: `kernel/interrupt.py` — ENA_DYNISR のエラーチェックとキュー表の生成**

★**編集は 4 箇所**である。(a) 冒頭の `kernel_cfg.h` 出力の削除、
(b) `CRE_ISR` 検査ループの直後に `ENA_DYNISR` 検査とキュー表生成を挿入、
(c) inthdr 生成ループに opt-in の枝を**先頭に**追加、
(d) inthdr 生成ループの直後に `IsrObject` と `isrorder_table` を追加。
**既存の枝（(c) の `if len(isrParamsList) > 0:` 以下）は 1 文字も変えない。**

**(a)** `kernel/interrupt.py:54-60` の

```python
#
#  kernel_cfg.hの生成
#
kernelCfgH.add(f"#define TNUM_ISRID\t{len(cfgData['CRE_ISR'])}")

for _, params in sorted(cfgData["CRE_ISR"].items()):
    kernelCfgH.add(f"#define {params['isrid']}\t{params['isrid'].val}")
kernelCfgH.add()
```

を**削除**し、次のコメントだけを残す：

```python
#
#  kernel_cfg.hの生成
#
#  ★TNUM_ISRID と各ISRIDのマクロ定義は，本ファイル末尾の IsrObject().generate()
#  が共通枠組み（kernel/kernel.py:168-175）として出力する．ISRがランタイム
#  オブジェクトになったため，他のオブジェクト種別と同じ枠組みに載せた．
#  本ファイルが kernelCfgH へ書くのはこの1箇所だけなので，出力位置が末尾へ
#  移動しても kernel_cfg.h の内容は変わらない（Task 2 Step 12 で実証する）．
#
```

**(b)** `TargetCheckCreIsr` の呼出しで終わる `CRE_ISR` 検査ループ（`:366-368`）の
**直後**、`isr_flag = {}`（`:370`）の**直前**に、次を挿入する：

```python
#
#  動的ISR生成の対象とする割込み番号（ENA_DYNISR）に関するエラーチェック
#
#  ★このループは，下のインライン連鎖生成ループより前に置かなければならない．
#  生成ループは cfgData["DEF_INH"] へ「生成した割込みハンドラのDEF_INH相当」を
#  追加するので，後に置くと自分が生成したDEF_INHを競合とみなしてしまう
#  （CRE_ISRの同じ検査が :340-345 で生成ループより前に置かれているのと同じ理由）．
#
dynIsrList = []
for _, params in cfgData["ENA_DYNISR"].items():
    # クラスの囲みの中に記述されていない場合（E_RSATR）
    #
    #  ENA_DYNISRはCFG_INT・CRE_ISRと同じくクラス内APIである（AID_ISRだけが
    #  クラス外専用）．対象の割込み要求ラインが属するクラスを指定させることで，
    #  動的ISRを実行するプロセッサ集合がCFG_INTと一致することを保証する．
    if "class" not in params:
        error_ercd("E_RSATR", params, "%apiname must be within a class")
        params["class"] = TCLS_ERROR
        continue

    # intnoが有効範囲外の場合（E_PAR）
    if params["intno"] not in INTNO_CREISR_VALID_ALL:
        error_illegal("E_PAR", params, "intno")
        continue

    # intnoに対するCFG_INTがない場合（E_OBJ）
    if params["intno"] not in cfgData["CFG_INT"]:
        error_ercd("E_OBJ", params,
                   "%%intno in %apiname is not configured with CFG_INT")
        continue

    intnoParams = cfgData["CFG_INT"][params["intno"]]

    # CFG_INTとENA_DYNISRが異なるクラスの囲みの中にある場合（E_RSATR）
    if params["class"] != intnoParams["class"]:
        error_ercd("E_RSATR", params,
                   "%%intno in %apiname "
                   "does not belong to the same class with CFG_INT")
        continue

    # intnoでカーネル管理外の割込みを指定した場合（E_OBJ）
    if intnoParams["intpri"] < TMIN_INTPRI:
        error_ercd("E_OBJ", params,
                   "interrupt service routine cannot handle "
                   "non-kernel interrupt in %apiname of %%intno")
        continue

    # intnoに対応するinhnoに対してDEF_INHがある場合（E_OBJ）
    conflict = False
    for prcid in clsData[params["class"]]["affinityPrcList"]:
        inhnoVal = toInhnoVal[prcid].get(params["intno"].val)
        if inhnoVal in cfgData["DEF_INH"]:
            error_ercd("E_OBJ", params,
                       f"%%intno in %apiname is duplicated "
                       f"with inhno {cfgData['DEF_INH'][inhnoVal]['inhno']}")
            conflict = True
    if conflict:
        continue

    dynIsrList.append(params["intno"].val)

dynIsrList.sort()

# ENA_DYNISRが1個以上あるのに静的なCRE_ISRが0個の構成は，共通枠組みが
# initialize_isrの登録を len(cfgData["CRE_ISR"]) > 0 に条件づけているため
# （kernel/kernel.py:200,257-258），isr_queue_tableが未初期化のまま
# call_isrが走ることになる．cfgエラーで弾く．
if len(dynIsrList) > 0 and len(cfgData["CRE_ISR"]) == 0:
    for _, params in cfgData["ENA_DYNISR"].items():
        error_ercd("E_OBJ", params,
                   "%apiname requires at least one CRE_ISR in the system")

# AID_ISRが1個以上あるのにENA_DYNISRが0個の構成は，適格なintnoが1つも無い
# ためacre_isrが必ずE_OBJで失敗し，予約したISRCBが死蔵される．cfgエラーで弾く．
numAutoIsrid = 0
for _, params in cfgData["AID_ISR"].items():
    numAutoIsrid += int(params["noisr"])
if numAutoIsrid > 0 and len(dynIsrList) == 0:
    for _, params in cfgData["AID_ISR"].items():
        error_ercd("E_OBJ", params,
                   "AID_ISR requires at least one ENA_DYNISR in the system")

#
#  割込みサービスルーチン呼出しキューのデータ構造
#
#  ★dcre（interrupt.trb:263-294）は「CFG_INTがありDEF_INHが競合しない全intno」に
#  キューを作るが，FMP3はENA_DYNISRで明示されたintnoにだけ作る（案B-2）．
#  これにより，ENA_DYNISRの無い構成ではキュー表が空になり，割込みハンドラの
#  生成も従来のインライン連鎖のままになる．
#
isrQueueHeader = {}
for index, intnoVal in enumerate(dynIsrList):
    isrQueueHeader[intnoVal] = f"&(_kernel_isr_queue_table[{index}])"

kernelCfgC.add2(f"const uint_t _kernel_tnum_isr_queue = {len(dynIsrList)};")

if len(dynIsrList) > 0:
    kernelCfgC.add(f"const ISR_ENTRY _kernel_isr_queue_list"
                   f"[{len(dynIsrList)}] = {{")
    for index, intnoVal in enumerate(dynIsrList):
        if index > 0:
            kernelCfgC.add(",")
        kernelCfgC.append(f"\t{{ {intnoVal}, {isrQueueHeader[intnoVal]} }}")
    kernelCfgC.add()
    kernelCfgC.add2("};")
    kernelCfgC.add2(f"ISRQCB _kernel_isr_queue_table[{len(dynIsrList)}];")
else:
    kernelCfgC.add("TOPPERS_EMPTY_LABEL(const ISR_ENTRY, "
                   "_kernel_isr_queue_list);")
    kernelCfgC.add2("TOPPERS_EMPTY_LABEL(ISRQCB, _kernel_isr_queue_table);")
```

**(c)** inthdr 生成ループの内側、`isrParamsList` を作り終えた直後
（`if len(isrParamsList) > 0:` の**直前**）に、次を挿入する：

```python
        # ★動的ISR生成の対象（ENA_DYNISR）とされた割込み番号
        #
        #  静的なCRE_ISRが1本も無くてもキュー方式の割込みハンドラを生成する
        #  （動的生成専用の割込み番号がありうるため）．クラスはCFG_INTから
        #  取る．静的ISRがある場合も同じ値になる（CRE_ISRとCFG_INTが同一
        #  クラスであることは :354-358 のE_RSATR検査が保証している）．
        #
        #  DEF_INHはaffinityPrcListに含まれる全プロセッサぶん生成する
        #  （既存のインライン連鎖と同じ機構）．inthdr本体はisr_flagで
        #  1回だけ生成し，全プロセッサのベクタで共有する．
        if intnoVal in isrQueueHeader:
            clsid = cfgData["CFG_INT"][intnoVal]["class"]

            # 割込みを受け付けるプロセッサでない場合はスキップ
            if prcid not in clsData[clsid]["affinityPrcList"]:
                continue

            inhnoVal = toInhnoVal[prcid][intnoVal]
            cfgData["DEF_INH"][inhnoVal] = {
                "inhno": NumStr(inhnoVal),
                "inhatr": NumStr(TA_NULL, "TA_NULL"),
                "inthdr": f"_kernel_inthdr_{intnoVal}",
                "class": clsid
            }

            if not isr_flag.get(intnoVal, False):
                kernelCfgC.add("void")
                kernelCfgC.add(f"_kernel_inthdr_{intnoVal}(void)")
                kernelCfgC.add("{")
                kernelCfgC.add(f"\t_kernel_call_isr({isrQueueHeader[intnoVal]});")
                kernelCfgC.add2("}")
                isr_flag[intnoVal] = True
            continue
```

★**`continue` を忘れないこと。** 忘れると opt-in した intno に静的 ISR がある場合に
インライン連鎖まで二重生成され、同じ関数名が 2 回定義される（コンパイルエラー）。

**(d)** inthdr 生成ループの直後（`#  割込みハンドラのための標準的な初期化情報の生成`
のコメントの**直前**）に、次を挿入する：

```python
#
#  割込みサービスルーチンに関する一般的な情報の生成
#
class IsrObject(KernelObject):
    def __init__(self):
        super().__init__("isr", "isr")

    def prepare(self, key, params):
        # エラーチェックは実施済みなので，ここでの処理は不要
        pass

    def generateInib(self, key, params):
        # ENA_DYNISRされていない割込み番号のISRはキューに登録されない．
        # インライン連鎖から直接呼ばれるため，p_isr_queueはNULLでよい
        # （initialize_isrがNULLを見てenqueueを省く）．
        p_isr_queue = isrQueueHeader.get(params["intno"].val, "NULL")
        return (f"({params['isratr']}), (EXINF)({params['exinf']}), "
                f"({p_isr_queue}), "
                f"(ISR)({params['isr']}), ({params['isrpri']})")


IsrObject().generate()

#
#  割込みサービスルーチン生成順序テーブルの生成
#
#  ★dcre（interrupt.trb:338-346）は挿入順（.cfgの記述順）で生成し，
#  TNUM_SISRIDが0のときのガードも持たない．FMP3では2点を変える．
#
#  (1) isrid昇順にする．initialize_isrはこの順にenqueue_isrし，enqueue_isrは
#      「自分より真に大きいisrpriの直前」に挿入するので，キューの並びは
#      「isrid昇順を基底とするisrpriの安定ソート」になる．これはインライン
#      連鎖の呼出し順序（本ファイルのisrParamsListの構築とsorted()）と
#      完全に同じである．ENA_DYNISRを足したり外したりしても同じ.cfgの
#      呼出し順序が変わらないことを保証するために，こちらを合わせる．
#  (2) 静的ISRが0個のときはTOPPERS_EMPTY_LABELにする（[0]配列を作らない．
#      他の表と同じ流儀）．
#
if len(cfgData["CRE_ISR"]) > 0:
    kernelCfgC.add("const ID _kernel_isrorder_table[TNUM_SISRID] = { ")
    kernelCfgC.append("\t")
    for index, (_, params) in enumerate(sorted(cfgData["CRE_ISR"].items())):
        if index > 0:
            kernelCfgC.append(", ")
        kernelCfgC.append(f"{params['isrid']}")
    kernelCfgC.add()
    kernelCfgC.add2("};")
else:
    kernelCfgC.add2("TOPPERS_EMPTY_LABEL(const ID, _kernel_isrorder_table);")
```

- [ ] **Step 8: `kernel/interrupt.trb` — 同じ 4 箇所を Ruby で（オラクル側）**

★**Python 版と 1 対 1 に対応させる。** 出力文字列は**完全に同じ**でなければならない
（`cfg_equivalence.sh` がバイト比較する）。

**(a)** `interrupt.trb:47-55` の `kernel_cfg.h` 出力 3 文を削除し、
Python 版 (a) と同じ趣旨のコメントに置き換える（Ruby のコメント記法 `#` で書く）。

**(b)** `interrupt.trb:405-409`（`TargetCheckCreIsr` の呼出しで終わる `CRE_ISR` 検査ループ）の
直後、`isr_flag = {}`（`:411`）の直前に挿入：

```ruby
#
#  動的ISR生成の対象とする割込み番号（ENA_DYNISR）に関するエラーチェック
#
#  ★このループは，下のインライン連鎖生成ループより前に置かなければならない．
#  生成ループは$cfgData[:DEF_INH]へ「生成した割込みハンドラのDEF_INH相当」を
#  追加するので，後に置くと自分が生成したDEF_INHを競合とみなしてしまう
#  （CRE_ISRの同じ検査が:374-381で生成ループより前に置かれているのと同じ理由）．
#
dynIsrList = []
$cfgData[:ENA_DYNISR].each do |_, params|
  # クラスの囲みの中に記述されていない場合（E_RSATR）
  #
  #  ENA_DYNISRはCFG_INT・CRE_ISRと同じくクラス内APIである（AID_ISRだけが
  #  クラス外専用）．対象の割込み要求ラインが属するクラスを指定させることで，
  #  動的ISRを実行するプロセッサ集合がCFG_INTと一致することを保証する．
  if !params.has_key?(:class)
    error_ercd("E_RSATR", params, "%apiname must be within a class")
    params[:class] = $TCLS_ERROR
    next
  end

  # intnoが有効範囲外の場合（E_PAR）
  if !$INTNO_CREISR_VALID_ALL.include?(params[:intno])
    error_illegal("E_PAR", params, "intno")
    next
  end

  # intnoに対するCFG_INTがない場合（E_OBJ）
  if !$cfgData[:CFG_INT].has_key?(params[:intno])
    error_ercd("E_OBJ", params, "%%intno in %apiname " \
                                    "is not configured with CFG_INT")
    next
  end

  intnoParams = $cfgData[:CFG_INT][params[:intno]]

  # CFG_INTとENA_DYNISRが異なるクラスの囲みの中にある場合（E_RSATR）
  if params[:class] != intnoParams[:class]
    error_ercd("E_RSATR", params, "%%intno in %apiname " \
                            "does not belong to the same class with CFG_INT")
    next
  end

  # intnoでカーネル管理外の割込みを指定した場合（E_OBJ）
  if intnoParams[:intpri] < $TMIN_INTPRI
    error_ercd("E_OBJ", params, "interrupt service routine cannot handle " \
                                "non-kernel interrupt in %apiname of %%intno")
    next
  end

  # intnoに対応するinhnoに対してDEF_INHがある場合（E_OBJ）
  conflict = false
  $clsData[params[:class]][:affinityPrcList].each do |prcid|
    inhnoVal = $toInhnoVal[prcid][params[:intno].val]
    if $cfgData[:DEF_INH].has_key?(inhnoVal)
      error_ercd("E_OBJ", params, "%%intno in %apiname is duplicated " \
                        "with inhno #{$cfgData[:DEF_INH][inhnoVal][:inhno]}")
      conflict = true
    end
  end
  next if conflict

  dynIsrList.push(params[:intno].val)
end

dynIsrList.sort!

# ENA_DYNISRが1個以上あるのに静的なCRE_ISRが0個の構成は，共通枠組みが
# initialize_isrの登録を$cfgData[@api].size > 0に条件づけているため
# （kernel/kernel.trb:213,274-275），isr_queue_tableが未初期化のまま
# call_isrが走ることになる．cfgエラーで弾く．
if dynIsrList.size > 0 && $cfgData[:CRE_ISR].size == 0
  $cfgData[:ENA_DYNISR].each do |_, params|
    error_ercd("E_OBJ", params,
               "%apiname requires at least one CRE_ISR in the system")
  end
end

# AID_ISRが1個以上あるのにENA_DYNISRが0個の構成は，適格なintnoが1つも無い
# ためacre_isrが必ずE_OBJで失敗し，予約したISRCBが死蔵される．cfgエラーで弾く．
numAutoIsrid = 0
$cfgData[:AID_ISR].each do |_, params|
  numAutoIsrid += params[:noisr]
end
if numAutoIsrid > 0 && dynIsrList.size == 0
  $cfgData[:AID_ISR].each do |_, params|
    error_ercd("E_OBJ", params,
               "AID_ISR requires at least one ENA_DYNISR in the system")
  end
end

#
#  割込みサービスルーチン呼出しキューのデータ構造
#
#  ★dcre（interrupt.trb:263-294）は「CFG_INTがありDEF_INHが競合しない全intno」に
#  キューを作るが，FMP3はENA_DYNISRで明示されたintnoにだけ作る（案B-2）．
#  これにより，ENA_DYNISRの無い構成ではキュー表が空になり，割込みハンドラの
#  生成も従来のインライン連鎖のままになる．
#
$isrQueueHeader = {}
dynIsrList.each_with_index do |intnoVal, index|
  $isrQueueHeader[intnoVal] = "&(_kernel_isr_queue_table[#{index}])"
end

$kernelCfgC.add2("const uint_t _kernel_tnum_isr_queue = #{dynIsrList.size};")

if dynIsrList.size > 0
  $kernelCfgC.add("const ISR_ENTRY _kernel_isr_queue_list" \
                                        "[#{dynIsrList.size}] = {")
  dynIsrList.each_with_index do |intnoVal, index|
    $kernelCfgC.add(",") if index > 0
    $kernelCfgC.append("\t{ #{intnoVal}, #{$isrQueueHeader[intnoVal]} }")
  end
  $kernelCfgC.add
  $kernelCfgC.add2("};")
  $kernelCfgC.add2("ISRQCB _kernel_isr_queue_table[#{dynIsrList.size}];")
else
  $kernelCfgC.add("TOPPERS_EMPTY_LABEL(const ISR_ENTRY, " \
                                        "_kernel_isr_queue_list);")
  $kernelCfgC.add2("TOPPERS_EMPTY_LABEL(ISRQCB, _kernel_isr_queue_table);")
end
```

★Ruby 側は `$isrQueueHeader`（グローバル）にする。`IsrObject#generateInib` から
参照するため、ローカル変数だとスコープ外になる（dcre `interrupt.trb:273` も
`$isrQueueHeader` としている）。**Python 側は同一ファイル内のモジュール変数で足りるが、
名前は `isrQueueHeader` で揃える。**

**(c)** `interrupt.trb` の inthdr 生成ループの内側、
`if isrParamsList.size > 0` の**直前**に挿入：

```ruby
    # ★動的ISR生成の対象（ENA_DYNISR）とされた割込み番号
    #
    #  静的なCRE_ISRが1本も無くてもキュー方式の割込みハンドラを生成する
    #  （動的生成専用の割込み番号がありうるため）．クラスはCFG_INTから
    #  取る．静的ISRがある場合も同じ値になる（CRE_ISRとCFG_INTが同一
    #  クラスであることは:390-394のE_RSATR検査が保証している）．
    if $isrQueueHeader.has_key?(intnoVal)
      clsid = $cfgData[:CFG_INT][intnoVal][:class]

      # 割込みを受け付けるプロセッサでない場合はスキップ
      next if !$clsData[clsid][:affinityPrcList].include?(prcid)

      inhnoVal = $toInhnoVal[prcid][intnoVal]
      $cfgData[:DEF_INH][inhnoVal] = {
        inhno: NumStr.new(inhnoVal),
        inhatr: NumStr.new($TA_NULL, "TA_NULL"),
        inthdr: "_kernel_inthdr_#{intnoVal}",
        class: clsid
      }

      if (!isr_flag[intnoVal])
        $kernelCfgC.add("void")
        $kernelCfgC.add("_kernel_inthdr_#{intnoVal}(void)")
        $kernelCfgC.add("{")
        $kernelCfgC.add("\t_kernel_call_isr(#{$isrQueueHeader[intnoVal]});")
        $kernelCfgC.add2("}")
        isr_flag[intnoVal] = true
      end
      next
    end
```

★Ruby の `next` は `$INTNO_CREISR_VALID[prcid].each do |intnoVal|` ブロックの次の
繰返しへ進む（Python の内側 `for` の `continue` と同義）。**入れ子のブロックを
間違えないこと**（`1.upto($TNUM_PRCID) do |prcid|` の外側ブロックではない）。

**(d)** inthdr 生成ループの直後に：

```ruby
#
#  割込みサービスルーチンに関する一般的な情報の生成
#
class IsrObject < KernelObject
  def initialize()
    super("isr", "isr")
  end

  def prepare(key, params)
    # エラーチェックは実施済みなので，ここでの処理は不要
  end

  def generateInib(key, params)
    # ENA_DYNISRされていない割込み番号のISRはキューに登録されない．
    # インライン連鎖から直接呼ばれるため，p_isr_queueはNULLでよい
    # （initialize_isrがNULLを見てenqueueを省く）．
    p_isr_queue = $isrQueueHeader.fetch(params[:intno].val, "NULL")
    return("(#{params[:isratr]}), (EXINF)(#{params[:exinf]}), " \
                        "(#{p_isr_queue}), " \
                        "(ISR)(#{params[:isr]}), (#{params[:isrpri]})")
  end
end
IsrObject.new.generate()

#
#  割込みサービスルーチン生成順序テーブルの生成
#
#  ★dcre（interrupt.trb:338-346）は挿入順（.cfgの記述順）で生成し，
#  TNUM_SISRIDが0のときのガードも持たない．FMP3では2点を変える．
#
#  (1) isrid昇順にする．initialize_isrはこの順にenqueue_isrし，enqueue_isrは
#      「自分より真に大きいisrpriの直前」に挿入するので，キューの並びは
#      「isrid昇順を基底とするisrpriの安定ソート」になる．これはインライン
#      連鎖の呼出し順序と完全に同じである．ENA_DYNISRを足したり外したりしても
#      同じ.cfgの呼出し順序が変わらないことを保証するために，こちらを合わせる．
#  (2) 静的ISRが0個のときはTOPPERS_EMPTY_LABELにする（[0]配列を作らない）．
#
if $cfgData[:CRE_ISR].size > 0
  $kernelCfgC.add("const ID _kernel_isrorder_table[TNUM_SISRID] = { ")
  $kernelCfgC.append("\t")
  $cfgData[:CRE_ISR].sort.each_with_index do |(_, params), index|
    $kernelCfgC.append(", ") if index > 0
    $kernelCfgC.append("#{params[:isrid]}")
  end
  $kernelCfgC.add
  $kernelCfgC.add2("};")
else
  $kernelCfgC.add2("TOPPERS_EMPTY_LABEL(const ID, _kernel_isrorder_table);")
end
```

- [ ] **Step 9: 全8構成のビルドと等価性（この時点ではまだカーネルが無いのでリンクは通らない）**

★**注意：この Step の時点では `kernel/interrupt.h`/`interrupt.c` に
`ISRQCB`/`ISR_ENTRY`/`ISRINIB`/`ISRCB` の定義が無いため、`kernel_cfg.c` の
コンパイルは必ず失敗する。** これは想定どおりであり、**Task 3 で解消する**。
ここで見るのは「**cfg の生成が最後まで走り、両エンジンの出力がバイト一致すること**」だけである。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --build build/$p > /tmp/isr-t2-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/isr-t2-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
```
期待:
- `build rc` は**コンパイルエラーで非 0**（`ISRQCB` 等が未定義）。
  **ログを見て、失敗が「未定義の型」であることを確かめる**（cfg 自体のエラーではないこと）。
  ★`grep -n "error:" /tmp/isr-t2-build-musca_b1-2core.log | head` で
  `unknown type name 'ISRQCB'` 系だけであることを確認する。
- `eq rc=0`（両エンジンがバイト一致）。**ここが本 Step の合格条件である。**
  `cfg_equivalence.sh` は生成物を比較するだけでコンパイルしないので、
  カーネルが未整備でも rc=0 になるはずである。**rc=2 は不合格**（前提未充足）。

- [ ] **Step 10: ★★管理された差分の検査（第二段階：恒常出力の許容リスト）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
diff -u /tmp/isr-base-generated/kernel_cfg.h build/musca_b1-2core/generated/kernel_cfg.h
echo "h diff rc=$?"
diff -u /tmp/isr-base-generated/kernel_cfg.c build/musca_b1-2core/generated/kernel_cfg.c \
     > /tmp/isr-t2-managed-diff.txt; cat /tmp/isr-t2-managed-diff.txt
```
期待: **`kernel_cfg.h` は差分なし（rc=0）**（訂正D／Step 7(a) の主張の実証）。
`kernel_cfg.c` の差分が次の**許容リストと完全一致**すること（**1 件でも余分な差分があれば不合格**）：

1. `const uint_t _kernel_tnum_isr_queue = 0;` の追加
2. `TOPPERS_EMPTY_LABEL(const ISR_ENTRY, _kernel_isr_queue_list);` の追加
3. `TOPPERS_EMPTY_LABEL(ISRQCB, _kernel_isr_queue_table);` の追加
4. `const ID _kernel_tmax_isrid = (TMIN_ISRID + TNUM_ISRID - 1);` の追加
5. `#define TNUM_SISRID	<静的ISR個数>` の追加
6. `const ID _kernel_tmax_sisrid = (TMIN_ISRID + TNUM_SISRID - 1);` の追加
7. `const ISRINIB _kernel_isrinib_table[TNUM_SISRID] = { ... };` の追加
   （各要素は `{ (TA_NULL), (EXINF)(<exinf>), (NULL), (ISR)(<isr>), (<isrpri>) }`。
   ★**`p_isr_queue` が全要素 `NULL`** であること＝opt-in が 1 つも無い証拠）
8. `static ISRCB _kernel_isrcb_<ISRID名>;` ×静的 ISR 個数 の追加
9. `ISRCB	*const _kernel_p_isrcb_table[TNUM_ISRID] = { ... };` の追加
10. `TOPPERS_EMPTY_LABEL(ISRINIB, _kernel_aisrinib_table);` の追加
11. `const ID _kernel_isrorder_table[TNUM_SISRID] = { ... };` の追加
12. 初期化関数列への `_kernel_initialize_isr(p_my_pcb);` の追加
    （`_kernel_initialize_interrupt(p_my_pcb);` の**前**に来ること）

**★次のものが差分に出たら不合格である**（設計の誤りを示す）：
- **`_kernel_inthdr_<intno>` の本体の変化（1 行でも）** ← 最重要
- `INTHDR_ENTRY(...)` 行・`_kernel_inhinib_table` の内容の変化
- `_kernel_intinib_table` の内容の変化
- task / cyclic / alarm / semaphore / eventflag / mutex / dataqueue / pridataq / mempfix
  ブロックの変化（本段階は ISR にしか触っていない）
- `_kernel_p_isrcb_table` の**サイズトークン**が `TNUM_SISRID` になっていること
  （`TNUM_ISRID`＝総数であるべき。共通枠組みの誤読）

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -c "call_isr" build/musca_b1-2core/generated/kernel_cfg.c        # 期待: 0
grep -n "_kernel_initialize_isr\|_kernel_initialize_interrupt" \
     build/musca_b1-2core/generated/kernel_cfg.c
```

- [ ] **Step 11: ★★インライン連鎖のバイト不変性（本段階の headline check）**

`test_int2` は**同一 intno に 3 本の ISR を持つ唯一の構成**であり、
インライン連鎖の一番複雑な形（`p_my_pcb` 宣言 + ISR 間のロック復元 ×2）を生成する。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
sed -n '/^_kernel_inthdr_65612(void)$/,/^}$/p' \
    /tmp/isr-base-int2-generated/kernel_cfg.c > /tmp/isr-t2-inthdr-before.txt
sed -n '/^_kernel_inthdr_65612(void)$/,/^}$/p' \
    build/musca_b1-2core-tint2/generated/kernel_cfg.c > /tmp/isr-t2-inthdr-after.txt
diff /tmp/isr-t2-inthdr-before.txt /tmp/isr-t2-inthdr-after.txt; echo "inthdr diff rc=$?"
grep -c . /tmp/isr-t2-inthdr-after.txt        # 期待: 0 でないこと（空を比較していない証拠）
grep -c "call_isr" build/musca_b1-2core-tint2/generated/kernel_cfg.c   # 期待: 0
```
期待: **`diff` rc=0** かつ **抽出した行数が 0 でない**（＝空同士を比べて満足していない）。
★`grep -c .` の確認を省略しない。段階3a/3b の教訓（「全部一致」は比較が壊れていても得られる）。
★`_kernel_inthdr_65612` の番号は musca_b1 の `INTNO1 = (1<<16)|76 = 65612` である。
**実際の生成物を見て番号を確認してから使うこと**（ターゲットや設定で変わりうる）。

- [ ] **Step 12: negative control（比較が空虚でないことの実演）**

「差分ゼロ」は**比較が壊れていても得られる**。差が出るはずのケースで実際に差が出ることを実演する。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
# (1) Python側だけを壊す → equivalence が MISMATCH になること
#     kernel/interrupt.py の tnum_isr_queue 出力を一時的に「+ 1」する
cmake --build build/musca_b1-2core > /dev/null 2>&1
tools/cfg_equivalence.sh build/musca_b1-2core > /tmp/isr-t2-neg1.log 2>&1; echo "rc=$?"
grep -n "MISMATCH\|RESULT" /tmp/isr-t2-neg1.log
```
`kernel/interrupt.py` の
`kernelCfgC.add2(f"const uint_t _kernel_tnum_isr_queue = {len(dynIsrList)};")` を
一時的に `{len(dynIsrList) + 1}` に書き換えて上記を実行する。
期待: **rc=1（MISMATCH）**。rc=0 なら等価性検査が空虚である。**rc=2 も不合格。**
確認後、書き換えを**復元**して rc=0 に戻ることを再確認する（`git diff kernel/interrupt.py` の
該当行が消えること）。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
# (2) inthdr 比較が空虚でないことの実演：わざと opt-in させて差が出ることを見る
#     test_int2.cfg に一時的に ENA_DYNISR(INTNO1); を足して生成し，diff が非0になること
cp test/test_int2.cfg /tmp/isr-t2-int2.cfg.bak
```
`test/test_int2.cfg` の `CLASS(CLS_PRC1) { ... }` の中（`CRE_ISR` 3 本の後）に
一時的に `ENA_DYNISR(INTNO1);` を足し、`AID_ISR(1);` をクラス外に足して：
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tint2 > /tmp/isr-t2-neg2-build.log 2>&1
sed -n '/^_kernel_inthdr_65612(void)$/,/^}$/p' \
    build/musca_b1-2core-tint2/generated/kernel_cfg.c > /tmp/isr-t2-inthdr-optin.txt
cat /tmp/isr-t2-inthdr-optin.txt
diff /tmp/isr-t2-inthdr-before.txt /tmp/isr-t2-inthdr-optin.txt; echo "diff rc=$?"
grep -c "call_isr" build/musca_b1-2core-tint2/generated/kernel_cfg.c   # 期待: 1
tools/cfg_equivalence.sh build/musca_b1-2core-tint2 > /tmp/isr-t2-neg2-eq.log 2>&1; echo "eq rc=$?"
```
期待:
- `_kernel_inthdr_65612` の本体が **`\t_kernel_call_isr(&(_kernel_isr_queue_table[0]));` の
  1 行だけ**になっている（`p_my_pcb` 宣言も `sense_lock` も消える）。
- `diff rc=1`（＝Step 11 の比較は本当に差を検出できる）。
- `grep -c "call_isr"` が **1**。
- `eq rc=0`（opt-in ありでも両エンジン一致）。
確認後、`cp /tmp/isr-t2-int2.cfg.bak test/test_int2.cfg` で**復元**し、
再ビルドして Step 11 の `diff rc=0` に戻ることまで確認する。
★**この 2 つ目の control を省略しないこと。** Step 11 の「差分ゼロ」が
「そもそも比較対象を取れていない」ではないことの唯一の証拠である。

- [ ] **Step 13: `test/test_dcre_mix` を 8 家族目（ISR）まで拡張**

`test/test_dcre_mix.h` に（`#include "target_test.h"` が無ければ足した上で）追記：

```c
/*  混在構成に載せる静的ISRとENA_DYNISRの対象（INTNO1はtarget_test.hが定義）  */
#ifndef TOPPERS_MACRO_ONLY
extern void	mix_isr1(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */
```

`test/test_dcre_mix.c` に：

```c
/*
 *  混在構成のリンク検査用の割込みサービスルーチン
 *
 *  本テストはビルドと等価性検査のみで実行しない（DIVERGENCE_MAP 記載）ため，
 *  中身は空でよい．
 */
void
mix_isr1(EXINF exinf)
{
}
```

`test/test_dcre_mix.cfg` の `CLASS(CLS_PRC1)` ブロック末尾（`CRE_MPF(MPF1, ...)` の次）に：

```c
	/*
	 *  ISR は8家族目．CFG_INT・CRE_ISR・ENA_DYNISR はいずれもクラス内API
	 *  であり，同一クラスの囲みの中に書かなければならない（E_RSATR）．
	 */
	CFG_INT(INTNO1, { INTNO1_INTATR, INTNO1_INTPRI });
	CRE_ISR(MIX_ISR1, { TA_NULL, 1, INTNO1, mix_isr1, 1 });
	ENA_DYNISR(INTNO1);
```

ファイル末尾（`AID_MPF(1);` の次）に：

```c
AID_ISR(2);
```

混在コメントを次に置き換える：

```c
/*
 *  ★混在の実体：AID_TSK / AID_CYC / AID_SEM / AID_MTX / AID_DTQ / AID_PDQ /
 *  AID_MPF / AID_ISR は書き，AID_ALM と AID_FLG は書かない．同一構成の中に
 *  has_aid が真のオブジェクトと偽のオブジェクトが共存することを両エンジンで
 *  検査する（段階2 最終レビュー Minor 3 の hardening）．
 *  段階3b では加えて，この cfg が dtq/pdq/mpf の acre_*/del_* を実際に
 *  リンクさせる唯一の非テスト構成として働く（sample1.cfg は静的 CRE_PDQ/
 *  CRE_MPF を持たないため AID_PDQ/AID_MPF を足せない）．
 *  ISR段階では加えて，ENA_DYNISR された intno と ENA_DYNISR されていない
 *  intno（serial.cfg の ISR_SIO）が同一構成に共存する形になっている
 *  ＝キュー方式とインライン連鎖の共存を1つの生成物の中で検査できる．
 */
```

★この cfg は **ISR 段階の positive control の本命**である：
同じ `kernel_cfg.c` の中に
- `_kernel_inthdr_<INTNO1>` ＝ `call_isr` 1 行（キュー方式）
- `_kernel_inthdr_<INTNO_SIO_PRC1>` ＝ 直接呼出し（インライン連鎖）

が**同居する**ことを目で確認する。

- [ ] **Step 14: positive control — AID/ENA 有り構成の生成物と等価性**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tmix > /tmp/isr-t2-mix-build.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tmix > /tmp/isr-t2-mix-eq.log 2>&1; echo "eq rc=$?"
G=build/musca_b1-2core-tmix/generated/kernel_cfg.c
grep -n "_kernel_tnum_isr_queue\|_kernel_isr_queue_list\|_kernel_isr_queue_table" $G
grep -n "TNUM_SISRID\|_kernel_aisrinib_table\|_kernel_aisrcb_\|_kernel_isrorder_table" $G
grep -n "_kernel_call_isr" $G
grep -n "define TNUM_ISRID" build/musca_b1-2core-tmix/generated/kernel_cfg.h
grep -n "EMPTY_LABEL(FLGINIB, _kernel_aflginib_table)\|EMPTY_LABEL(ALMINIB, _kernel_aalminib_table)" $G
sed -n '/^const ISRINIB _kernel_isrinib_table/,/^};/p' $G
```
期待（build rc は Task 3 まで非 0 のまま。**eq rc=0 が合格条件**）:
- `const uint_t _kernel_tnum_isr_queue = 1;`
- `const ISR_ENTRY _kernel_isr_queue_list[1] = { { 65612, &(_kernel_isr_queue_table[0]) } };`
- `ISRQCB _kernel_isr_queue_table[1];`
- `ISRINIB _kernel_aisrinib_table[2];`（`TOPPERS_EMPTY_LABEL` ではなく**実体**）
- `static ISRCB _kernel_aisrcb_1;` `_kernel_aisrcb_2;` と
  `_kernel_p_isrcb_table` 末尾への `&_kernel_aisrcb_1` `&_kernel_aisrcb_2` の追加
- `TNUM_ISRID` = 静的（`ISR_SIO` + `MIX_ISR1` = 2）+ 2 = **4**
- `_kernel_isrinib_table` の 2 要素のうち、`MIX_ISR1` の `p_isr_queue` が
  `&(_kernel_isr_queue_table[0])`、`ISR_SIO` のそれが **`NULL`**
- `_kernel_call_isr` が **1 箇所だけ**（`_kernel_inthdr_65612` の中）
- `EMPTY_LABEL(FLGINIB, ...)` と `EMPTY_LABEL(ALMINIB, ...)` が同じファイルに同居
- `cfg_equivalence.sh` **rc=0**（Ruby/Python がバイト一致）。**rc=2 は不合格。**

- [ ] **Step 15: エラー回帰ケース 10 件の追加**

★`run.sh` は 4 引数形。`#include "test_int2.h"` を含む cfg は
**第4引数 `EXTRA_CFLAGS="-I<repo>/test"` が必須**。

★★`dcre_aid_isr_no_static.cfg` は **`serial.cfg` を避けなければならない**
（`target/*/target_serial.cfg` と `arch/*/*/chip_serial.cfg` が `CRE_ISR(ISR_SIO)` を
必ず生成するため、`test_common1.cfg` をそのまま INCLUDE すると「静的 `CRE_ISR` が 0 個」の
前提が崩れる）。段階3a の `dcre_aid_sem_no_static.cfg` と**同じ回避**（`syslog.cfg` と
`banner.cfg` だけを個別 INCLUDE）を使う。★ただし `CFG_INT`/`ENA_DYNISR` も
使えなくなるので、このケースは **`AID_ISR` 単独**で書く（ガード 1 の E_OBJ が先に出るか
ガード 2 の E_OBJ が先に出るかは実測で確かめ、実測に合わせて期待文字列を決める）。

**1. `tools/cfg_error_tests/dcre_aid_isr_in_class.cfg`** → `E_RSATR`
```c
/*
 *  AID_ISR をクラスの囲みの中に書くと E_RSATR（クラス外専用 API）
 *
 *  dcre_aid_in_class.cfg と同じ理由（cfg_py/cfg.py:search_file_path が
 *  cwd相対でしか解決できず、run.shは常にスクラッチディレクトリへcdする）
 *  で、「test/test_common1.cfg」と書き、実行時に EXTRA_CFLAGS として
 *  「-I<repo>/test」（第4引数）を渡す。
 */
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CFG_INT(INTNO1, { INTNO1_INTATR, INTNO1_INTPRI });
	CRE_ISR(ISR1, { TA_NULL, 1, INTNO1, isr1, 1 });
	ENA_DYNISR(INTNO1);
	AID_ISR(1);
}
```

**2. `tools/cfg_error_tests/dcre_aid_isr_no_static.cfg`** → `E_OBJ`
```c
/*
 *  静的な CRE_ISR が1個も無いのに AID_ISR を書くと E_OBJ
 *
 *  ★段階3a の dcre_aid_sem_no_static.cfg と同じ回避が必要である。
 *  TOPPERS_OMIT_TECS ビルドでは syssvc/serial.cfg が
 *  target/musca_b1_gcc/target_serial.cfg を引き込み、そこに
 *  CRE_ISR(ISR_SIO) が必ずある。したがって test_common1.cfg を
 *  そのまま INCLUDE すると「静的 CRE_ISR が0個」という前提が崩れる。
 *  syslog.cfg / banner.cfg だけを個別 INCLUDE して serial.cfg を避ける。
 *
 *  実行時に EXTRA_CFLAGS として「-I<repo>/test」（第4引数）を渡すこと。
 */
INCLUDE("syssvc/syslog.cfg");
INCLUDE("syssvc/banner.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
}
AID_ISR(2);
```

**3. `tools/cfg_error_tests/dcre_aid_isr_no_dynisr.cfg`** → `E_OBJ`
```c
/*
 *  ENA_DYNISR が1個も無いのに AID_ISR を書くと E_OBJ
 *
 *  適格な intno が1つも無いので acre_isr は必ず E_OBJ で失敗し、
 *  予約した ISRCB が死蔵される。cfg で弾く（ISR固有ガード1）。
 *
 *  実行時に EXTRA_CFLAGS として「-I<repo>/test」（第4引数）を渡すこと。
 */
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CFG_INT(INTNO1, { INTNO1_INTATR, INTNO1_INTPRI });
	CRE_ISR(ISR1, { TA_NULL, 1, INTNO1, isr1, 1 });
}
AID_ISR(2);
```

**4. `tools/cfg_error_tests/dcre_dynisr_out_of_class.cfg`** → `E_RSATR`
```c
/*
 *  ENA_DYNISR をクラスの囲みの外に書くと E_RSATR
 *
 *  ENA_DYNISR は CFG_INT・CRE_ISR と同じくクラス内 API である
 *  （AID_ISR だけがクラス外専用）。
 *
 *  実行時に EXTRA_CFLAGS として「-I<repo>/test」（第4引数）を渡すこと。
 */
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CFG_INT(INTNO1, { INTNO1_INTATR, INTNO1_INTPRI });
	CRE_ISR(ISR1, { TA_NULL, 1, INTNO1, isr1, 1 });
}
ENA_DYNISR(INTNO1);
AID_ISR(1);
```

**5. `tools/cfg_error_tests/dcre_dynisr_class_mismatch.cfg`** → `E_RSATR`
```c
/*
 *  ENA_DYNISR を CFG_INT と異なるクラスの囲みの中に書くと E_RSATR
 *
 *  CRE_ISR と CFG_INT の同一クラス規則（NGKI5142）と同じ検査である。
 *
 *  実行時に EXTRA_CFLAGS として「-I<repo>/test」（第4引数）を渡すこと。
 */
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CFG_INT(INTNO1, { INTNO1_INTATR, INTNO1_INTPRI });
	CRE_ISR(ISR1, { TA_NULL, 1, INTNO1, isr1, 1 });
}
CLASS(CLS_PRC2) {
	ENA_DYNISR(INTNO1);
}
AID_ISR(1);
```

**6. `tools/cfg_error_tests/dcre_dynisr_no_cfgint.cfg`** → `E_OBJ`
```c
/*
 *  CFG_INT の無い intno に ENA_DYNISR を書くと E_OBJ
 *
 *  キューの生成前提（dcre interrupt.trb:263-270 の除外則）と同型。
 *
 *  実行時に EXTRA_CFLAGS として「-I<repo>/test」（第4引数）を渡すこと。
 */
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CFG_INT(INTNO1, { INTNO1_INTATR, INTNO1_INTPRI });
	CRE_ISR(ISR1, { TA_NULL, 1, INTNO1, isr1, 1 });
	/*  INTNO1 + 1（予備NVIC IRQ61）には CFG_INT が無い  */
	ENA_DYNISR(INTNO1 + 1);
}
AID_ISR(1);
```

**7. `tools/cfg_error_tests/dcre_dynisr_definh_conflict.cfg`** → `E_OBJ`
```c
/*
 *  DEF_INH が競合する intno に ENA_DYNISR を書くと E_OBJ
 *
 *  INTNO1 に対応する inhno に DEF_INH があると、その割込みハンドラは
 *  ユーザ定義のものになり、キュー方式の inthdr を生成できない
 *  （dcre interrupt.trb:263-270 の除外則と同じ）。
 *
 *  実行時に EXTRA_CFLAGS として「-I<repo>/test」（第4引数）を渡すこと。
 */
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CFG_INT(INTNO1, { INTNO1_INTATR, INTNO1_INTPRI });
	CRE_ISR(ISR1, { TA_NULL, 1, INTNO1, isr1, 1 });
	DEF_INH(INTNO1, { TA_NULL, isr1 });
	ENA_DYNISR(INTNO1);
}
AID_ISR(1);
```
★**注意：`CRE_ISR(ISR1, ...)` 自身も DEF_INH と競合するので E_OBJ が 2 回出る**。
`run.sh` は期待文字列の**含有**を見るので `E_OBJ` で通るはずだが、
**実測でどのメッセージが出るかをログで確かめ、記録すること**。
2 つ出るのが紛らわしければ `CRE_ISR` を別の intno に移してもよい
（その場合 `CFG_INT` も足す）。**どちらにしたかを記録する。**

**8. `tools/cfg_error_tests/dcre_dynisr_no_static.cfg`** → `E_OBJ`
```c
/*
 *  静的な CRE_ISR が1個も無いのに ENA_DYNISR を書くと E_OBJ（ISR固有ガード2）
 *
 *  共通枠組みは initialize_isr の登録を「静的オブジェクトが1個以上」に
 *  条件づけている（kernel/kernel.py:200,257-258）ので、静的 CRE_ISR が
 *  0個だと isr_queue_table が未初期化のまま call_isr が走る。
 *
 *  ★serial.cfg が CRE_ISR(ISR_SIO) を必ず作るため、
 *  dcre_aid_isr_no_static.cfg と同じく syslog.cfg/banner.cfg のみを
 *  INCLUDE して serial.cfg を避ける。CFG_INT は自前で書く。
 *
 *  実行時に EXTRA_CFLAGS として「-I<repo>/test」（第4引数）を渡すこと。
 */
INCLUDE("syssvc/syslog.cfg");
INCLUDE("syssvc/banner.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CFG_INT(INTNO1, { INTNO1_INTATR, INTNO1_INTPRI });
	ENA_DYNISR(INTNO1);
}
```

**9. `tools/cfg_error_tests/dcre_dynisr_bad_intno.cfg`** → `E_PAR`
```c
/*
 *  有効範囲外の intno に ENA_DYNISR を書くと E_PAR
 *
 *  99999 は musca_b1 の INTNO_CREISR_VALID に含まれない
 *  （e_par_creisr_intno_keyerror.cfg と同じ値を使う）。
 *
 *  実行時に EXTRA_CFLAGS として「-I<repo>/test」（第4引数）を渡すこと。
 */
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CFG_INT(INTNO1, { INTNO1_INTATR, INTNO1_INTPRI });
	CRE_ISR(ISR1, { TA_NULL, 1, INTNO1, isr1, 1 });
	ENA_DYNISR(99999);
}
AID_ISR(1);
```

**10. `tools/cfg_error_tests/dcre_dynisr_duplicated.cfg`** → `E_OBJ`
```c
/*
 *  同じ intno に ENA_DYNISR を2回書くと E_OBJ
 *
 *  kernel_api.def の「ENA_DYNISR .intno*」（KEYPAR）により、
 *  cfg エンジンの共通部（cfg_py/pass2.py:385-391）が登録キーの重複として
 *  検出する。ENA_DYNISR 固有のコードは1行も要らない。
 *
 *  実行時に EXTRA_CFLAGS として「-I<repo>/test」（第4引数）を渡すこと。
 */
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CFG_INT(INTNO1, { INTNO1_INTATR, INTNO1_INTPRI });
	CRE_ISR(ISR1, { TA_NULL, 1, INTNO1, isr1, 1 });
	ENA_DYNISR(INTNO1);
	ENA_DYNISR(INTNO1);
}
AID_ISR(1);
```

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
X="-I$PWD/test"
M=build/musca_b1-2core
$R $M $T/dcre_aid_isr_in_class.cfg       E_RSATR "$X"; echo "1:$?"
$R $M $T/dcre_aid_isr_no_static.cfg      E_OBJ   "$X"; echo "2:$?"
$R $M $T/dcre_aid_isr_no_dynisr.cfg      E_OBJ   "$X"; echo "3:$?"
$R $M $T/dcre_dynisr_out_of_class.cfg    E_RSATR "$X"; echo "4:$?"
$R $M $T/dcre_dynisr_class_mismatch.cfg  E_RSATR "$X"; echo "5:$?"
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
X="-I$PWD/test"
M=build/musca_b1-2core
$R $M $T/dcre_dynisr_no_cfgint.cfg       E_OBJ   "$X"; echo "6:$?"
$R $M $T/dcre_dynisr_definh_conflict.cfg E_OBJ   "$X"; echo "7:$?"
$R $M $T/dcre_dynisr_no_static.cfg       E_OBJ   "$X"; echo "8:$?"
$R $M $T/dcre_dynisr_bad_intno.cfg       E_PAR   "$X"; echo "9:$?"
$R $M $T/dcre_dynisr_duplicated.cfg      E_OBJ   "$X"; echo "10:$?"
```
期待: **10 件とも rc=0**（両エンジンが同じ ercd を同じ文言で検出）。
- **rc=2** は**前提未充足**＝第4引数の付け忘れか、cfg が別のエラーで先に落ちている。
- **rc=1** は両エンジンの食い違い＝`.py` と `.trb` のメッセージ文字列が違う。
  `grep -n "E_" /tmp/...` でログを読み、**Ruby と Python の文言を一致させる**。
- ケース 2 と 8 で **E_OBJ が出ない**（素通りする）なら、`serial.cfg` の回避が
  効いていない。`grep -rn "CRE_ISR" <スクラッチの展開後cfg>` で確かめ、
  **切り替えた事実を記録する**。

- [ ] **Step 16: ★恒常出力のコストを数値で記録する（訂正D）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
find build/musca_b1-2core -name '*.elf' -exec size {} \; > /tmp/isr-t2-after-size.txt 2>&1
echo "--- before ---"; cat /tmp/isr-t2-base-size.txt
echo "--- after  ---"; cat /tmp/isr-t2-after-size.txt
```
★この Step は **Task 3 完了後でないと `.elf` が作れない**（リンクが通らない）。
**Task 3 Step 9 で実行し、結果を本 Task の台帳記述へ反映する**こと。
実行順の都合でここに置けないことを台帳に明記する。
期待: `text`/`data`/`bss` の増分が、静的 ISR 数（sample1 の musca_b1-2core は 2 個）×
`sizeof(ISRINIB)` + 2 × `sizeof(ISRCB)` + ポインタ表 + `isrorder_table` + 数十バイトの
初期化コード、の**オーダーであること**。桁が違ったら設計の誤りである。
**実測値を確認結果表と `DIVERGENCE_MAP.md` に書く（推測値ではなく実測値）。**

- [ ] **Step 17: 台帳とコミット**

`DIVERGENCE_MAP.md` に次を追記する：
- `kernel/kernel_api.def` の理由欄に「ISR段階で `AID_ISR .noisr` と
  `ENA_DYNISR .intno*` の 2 行を追加。`ENA_DYNISR` は FMP3 独自の静的 API
  （dcre には無い）で、案B-2 ハイブリッドの opt-in 印である。`*`（KEYPAR）により
  同一 intno への重複記述が共通部で E_OBJ になる」
- `kernel/interrupt.trb` を新規行として追加（種別 `mod (dcre-port)`）。理由：
  「ISR段階で (a) `kernel_cfg.h` への `TNUM_ISRID`/ISRID 定義出力を
  `IsrObject.new.generate()`（共通枠組み）へ移動、(b) `ENA_DYNISR` のエラーチェックと
  `isr_queue_list`/`isr_queue_table` の生成を追加、(c) 割込みハンドラ生成ループに
  opt-in intno の枝（`_kernel_call_isr` 1 行）を追加、(d) `IsrObject` と
  `isrorder_table` を追加。**既存のインライン連鎖生成の枝は 1 文字も変えていない**
  （`test_int2` の生成物がバイト一致することで実証済み）。dcre からの意図的な逸脱 3 件：
  ①キューを作るのは `ENA_DYNISR` された intno だけ（dcre は全 intno）、
  ②`isrorder_table` は isrid 昇順（dcre は挿入順。理由は opt-in の有無で呼出し順が
  変わらないようにするため）、③`isrorder_table` に `TNUM_SISRID == 0` の
  `TOPPERS_EMPTY_LABEL` ガードを追加（dcre は `[0]` 配列になる）」
- `kernel/interrupt.py` は `kernel/*.py（15個）` の既存行の理由欄に
  「ISR段階で `.trb` と同じ 4 箇所を移植」を追記
- `include/kernel.h` の理由欄に「ISR段階で `T_CISR`（dcre `include/kernel.h:322-328` と
  バイト一致）と `acre_isr`/`del_isr` の 2 宣言を追加。返値型は dcre 自身が `ER_ID` を
  使っているため逸脱なし（段階1〜3b の `ER_UINT`→`ER_ID` とは事情が異なる）」
- `kernel/kernel_impl.h` の理由欄に「ISR段階で `TMIN_ISRID`（`TMIN_SPNID` の直後。
  dcre は `TMIN_ALMID` の直後だが FMP3 は `TMIN_SPNID` が後ろにあるため末尾に足した）と
  `TARGET_ISRATR`（dcre `kernel_impl.h:155-157` と同一）を追加。`kernel_sym.def:73` が
  `defined(TARGET_ISRATR),0` なので cfg 出力は不変（実測で確認済み）」
- `test/test_dcre_mix.{c,cfg,h}` の理由欄に「ISR段階で `CFG_INT`/`CRE_ISR`/`ENA_DYNISR`/
  `AID_ISR(2)` と `mix_isr1` を追記。キュー方式（`MIX_ISR1`）とインライン連鎖
  （`ISR_SIO`）が同一生成物に共存する唯一の非テスト構成になった」
- **★恒常出力の受容**を独立した段落で書く：「ISR にはランタイムオブジェクトが
  無かったため、`AID_ISR` を `kernel_api.def` に登録した時点で（`cfgData` が
  登録済み API のキーを常に持つ＝`cfg_py/pass2.py:181-186`）、静的 `CRE_ISR` を
  1 個以上持つ全構成に ISRINIB 表・ISRCB 実体・`p_isrcb_table`・`isrorder_table`・
  `tmax_isrid`/`tmax_sisrid`/`TNUM_SISRID`・`initialize_isr` の起動時呼出しが恒常的に
  加わる。**ディスパッチ経路（`_kernel_inthdr_*` 本体）はバイト不変**であり、
  加わるのはデータと起動時の O(N) ループ 1 本だけである。実測増分は
  text=<実測> / data=<実測> / bss=<実測> バイト（`build/musca_b1-2core`）」

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add -A && git commit -m "feat(cfg): AID_ISR/ENA_DYNISR とキュー方式ディスパッチを両エンジンへ追加（opt-in無し構成のinthdrはバイト不変・dcre動的ISR）"
```

---
### Task 3: ランタイム ISR オブジェクトの新設（ISRQCB/ISRINIB/ISRCB・free_isrcb・initialize_isr・2レンジ ISRID）

**推奨モデル:** 中位（sonnet）

**★置き場所の判断（決定済み。再議しない）:** 新規ファイル `kernel/isr.h`/`kernel/isr.c` は
**作らない**。dcre と同じく `kernel/interrupt.h` / `kernel/interrupt.c` に置く。
理由は 2 つとも真である：(1) dcre の現物がそうである（`interrupt.h:53-136`・
`interrupt.c:166-394`）ので転写の対応が 1 対 1 になる、(2) `KERNEL_FCSRCS`
（`kernel/Makefile.kernel:51-56` の 22 個）を変えずに済む（Global Constraint 9）。

**Files:**
- Modify: `kernel/interrupt.h`（型 4 個 / extern 群 / `tnum_isr`・`tnum_sisr` / `ISRID` / 宣言 2 個）
- Modify: `kernel/interrupt.c`（`INDEX_ISR`/`get_isrcb`/`ISR_KEY_GT`/`enqueue_isr`/
  `search_isr_queue`/`free_isrcb`/`initialize_isr`）
- Modify: `kernel/check.h`（`VALID_ISRID`・`VALID_ISRPRI`・`CHECK_OBJ`）
- Modify: `kernel/allfunc.h`（`/* interrupt.c */` 節に 2 行）
- Modify: `kernel/Makefile.kernel`（`interrupt =` 行に `.o` 2 個）
- Modify: `kernel/kernel_rename.def` →（再生成）`kernel_rename.h` `kernel_unrename.h`
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Consumes: Task 2 の `_kernel_tmax_isrid` / `_kernel_tmax_sisrid` / `TNUM_SISRID` /
  `_kernel_isrinib_table` / `_kernel_aisrinib_table` / `_kernel_p_isrcb_table` /
  `_kernel_isrorder_table` / `_kernel_tnum_isr_queue` / `_kernel_isr_queue_list` /
  `_kernel_isr_queue_table` / `TMIN_ISRID` / `TARGET_ISRATR`。
  Task 1 の確認結果表（dcre 転写元行範囲、訂正C/I/J）。
- Produces（Task 4 が使う）: `ISRQCB` / `ISRCB` / `ISRINIB` / `ISR_ENTRY` の型定義、
  `tnum_isr` / `tnum_sisr` / `ISRID(p_isrcb)` / `ISR_KEY_GT(...)` /
  `extern void call_isr(ISRQCB *p_isr_queue);` の宣言。
- Produces（Task 5 が使う）: `QUEUE free_isrcb` / `enqueue_isr(ISRQCB *, ISRCB *)` /
  `search_isr_queue(INTNO)` / `INDEX_ISR(isrid)` / `get_isrcb(isrid)` /
  `VALID_ISRID` / `VALID_ISRPRI` / `CHECK_OBJ`。
- Produces: `initialize_isr(PCB *p_my_pcb)`（Task 2 が生成した初期化関数列から呼ばれる）。

**★この Task の完了時点でリンクが通る**（Task 2 の生成物がコンパイルできるようになる）。
`call_isr` は Task 4、`acre_isr`/`del_isr` は Task 5 なので、
**この Task では `call_isr` の実体を「空のスタブ」として置かない**
——`allfunc.h` に `TOPPERS_isrcal` を書くのは Task 4 であり、
Task 3 の時点では `_kernel_call_isr` が未定義のままでよい
（opt-in を持つ構成＝`test_dcre_mix` だけがリンクエラーになる）。
★**ただしそれでは Task 3 の検証ができない**ので、**Task 3 で `call_isr` の
dcre 相当の素朴な実装まで入れ、Task 4 で MP 版へ全面的に書き換える**。
この 2 段構えを採る理由：Task 3 の時点で「型と表と初期化が正しい」ことを
リンクとビルドで独立に検証できるようにするため。**Task 4 の最初のステップで
この素朴版を捨てることを Task 3 のコメントに明記する。**

- [ ] **Step 1: `kernel/check.h` に `VALID_ISRID`・`VALID_ISRPRI`・`CHECK_OBJ` を追加（訂正J）**

`#define VALID_SPNID(spnid)	(TMIN_SPNID <= (spnid) && (spnid) <= tmax_spnid)`
（`kernel/check.h:65` 付近）の**直後**に 1 行：

```c
#define VALID_ISRID(isrid)	(TMIN_ISRID <= (isrid) && (isrid) <= tmax_isrid)
```

`#define VALID_DPRI(dpri)	(TMIN_DPRI <= (dpri) && (dpri) <= TMAX_DPRI)` のブロック
（段階3b が追加した `:74-76` 付近）の**直後**に：

```c
/*
 *  割込みサービスルーチン優先度の範囲の判定
 */
#define VALID_ISRPRI(isrpri) \
				(TMIN_ISRPRI <= (isrpri) && (isrpri) <= TMAX_ISRPRI)
```

`CHECK_ILUSE` のブロック（`:305-311` 付近）の**直後**に：

```c
/*
 *  オブジェクト状態エラーのチェック（E_OBJ）
 */
#define CHECK_OBJ(exp) do {									\
	if (!(exp)) {											\
		ercd = E_OBJ;										\
		goto error_exit;									\
	}														\
} while (false)
```

（3 つとも dcre `check.h:64` / `:73-74` / `:235-240` と同一。
`VALID_DPRI` を段階3b で追加したのと同じ前例に載る。**タブ位置を既存行に揃えること**。）

**検証:**
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
D=/home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/kernel
diff <(grep -A1 "define VALID_ISRPRI" $D/check.h) \
     <(grep -A1 "define VALID_ISRPRI" kernel/check.h); echo "isrpri diff rc=$?"
diff <(sed -n '/^#define CHECK_OBJ/,/^} while (false)/p' $D/check.h) \
     <(sed -n '/^#define CHECK_OBJ/,/^} while (false)/p' kernel/check.h); echo "obj diff rc=$?"
grep -n "define VALID_ISRID" kernel/check.h
```
期待: 2 つの `diff` とも rc=0、`VALID_ISRID` が 1 行実在。

- [ ] **Step 2: `kernel/interrupt.h` — 型 4 個と extern 群と `ISRID`**

`#include "kernel_impl.h"`（`kernel/interrupt.h:50`）の直後に：

```c
#include <queue.h>
```

（dcre `interrupt.h:51` と同じ。FMP3 の現行 `interrupt.h` には無い。）

続けて、`#if !defined(OMIT_INITIALIZE_INTERRUPT) || defined(USE_INHINIB_TABLE)`
（`:52`）の**直前**に、次のブロックをまるごと挿入する
（★`#if` ガードの**外**であること。ISR のランタイムオブジェクトは
`OMIT_INITIALIZE_INTERRUPT` の有無に関係なく必要である）：

```c
/*
 *  割込みサービスルーチン呼出しキュー管理ブロック
 *
 *  ★dcreのisr_queue_table[]は素のQUEUE配列（dcre interrupt.h:93）だが，
 *  FMP3はキューごとにenqueue世代番号（isrseq）のカウンタを持つ必要がある
 *  ため専用の型を新設した．isr_queueが先頭メンバなので，(ISRQCB *)への
 *  キャストとqueue_*操作はISRCBと同じ技法で書ける．
 *
 *  isrseqは「次にenqueueするISRCBへ与える世代番号」である．キューが空に
 *  なったときenqueue_isrが0へ戻すので，u32のラップは実用上到達しない
 *  （空にならないまま2^32回enqueueすることは非現実的である）．
 */
typedef struct isr_queue_control_block {
	QUEUE		isr_queue;		/* 割込みサービスルーチン呼出しキュー */
	uint32_t	isrseq;			/* 次に採番するenqueue世代番号 */
} ISRQCB;

/*
 *  割込みサービスルーチン初期化ブロック
 */
typedef struct isr_initialization_block {
	ATR			isratr;			/* 割込みサービスルーチン属性 */
	EXINF		exinf;			/* 割込みサービスルーチンの拡張情報 */
	ISRQCB		*p_isr_queue;	/* 登録先割込みサービスルーチン呼出しキュー */
	ISR			isr;			/* 割込みサービスルーチンの先頭番地 */
	PRI			isrpri;			/* 割込みサービスルーチン優先度 */
} ISRINIB;

/*
 *  割込みサービスルーチン管理ブロック
 *
 *  isr_queueは，キューへの登録エントリと，使用していないISRCBのリスト
 *  （free_isrcb）のリンクを兼ねる（dcre interrupt.c:337,384と同一）．
 *
 *  isrseqとrunningはFMP3の追加である．
 *  ・isrseq: enqueueされたときにISRQCBのカウンタから採番される世代番号．
 *    call_isrの走査が「次に呼ぶISR」を(isrpri, isrseq)の辞書式順序で
 *    再決定するための安定キーである．同一isrpriのISRが複数あるとき，
 *    isrpriだけでは走査の再開位置を決められない（削除と再生成で
 *    スロットが再利用されるとポインタやIDでも決められない）．
 *  ・running: 当該ISRの本体を実行中のプロセッサのビットマップ．
 *    del_isrは，キューから外した後この値が0になるまで待つ（quiesce）．
 *    ビットマップなので複数コアが同時に実行していても互いに壊さない．
 */
typedef struct isr_control_block {
	QUEUE		isr_queue;		/* 割込みサービスルーチン呼出しキュー */
	const ISRINIB *p_isrinib;	/* 初期化ブロックへのポインタ */
	uint32_t	isrseq;			/* enqueue世代番号 */
	uint_t		running;		/* 実行中プロセッサのビットマップ */
} ISRCB;

/*
 *  割込みサービスルーチン呼出しキューを検索するためのデータ構造
 *
 *  cfgが生成するこの表が，動的ISR生成の対象として適格な割込み番号の
 *  グローバルな一覧である（ENA_DYNISRされたintnoだけが載る）．intnoの
 *  昇順にソートされており，acre_isrが二分探索で引く．per-coreのビット
 *  マップ（check_intno_cfg）を使わないので，判定が呼出しコアに依存しない．
 */
typedef struct {
	INTNO		intno;			/* 割込み番号 */
	ISRQCB		*p_isr_queue;	/* 割込みサービスルーチン呼出しキュー */
} ISR_ENTRY;

/*
 *  割込みサービスルーチン呼出しキューのエントリ数（kernel_cfg.c）
 */
extern const uint_t	tnum_isr_queue;

/*
 *  割込みサービスルーチン呼出しキューリスト（kernel_cfg.c）
 */
extern const ISR_ENTRY	isr_queue_list[];

/*
 *  割込みサービスルーチン呼出しキューのエリア（kernel_cfg.c）
 */
extern ISRQCB	isr_queue_table[];

/*
 *  使用していない割込みサービスルーチン管理ブロックのリスト（interrupt.c）
 */
extern QUEUE	free_isrcb;

/*
 *  割込みサービスルーチンIDの最大値（kernel_cfg.c）
 */
extern const ID	tmax_isrid;
extern const ID	tmax_sisrid;	/* 静的生成ISRのID番号の最大値 */

/*
 *  割込みサービスルーチン初期化ブロックのエリア（kernel_cfg.c）
 */
extern const ISRINIB	isrinib_table[];

/*
 *  動的生成割込みサービスルーチンの初期化ブロックのエリア（kernel_cfg.c・RAM）
 */
extern ISRINIB			aisrinib_table[];

/*
 *  割込みサービスルーチン生成順序テーブル（kernel_cfg.c）
 */
extern const ID	isrorder_table[];

/*
 *  割込みサービスルーチン管理ブロックのエリアへのポインタテーブル（kernel_cfg.c）
 */
extern ISRCB *const	p_isrcb_table[];

/*
 *  割込みサービスルーチンの数
 */
#define tnum_isr	((uint_t)(tmax_isrid - TMIN_ISRID + 1))
#define tnum_sisr	((uint_t)(tmax_sisrid - TMIN_ISRID + 1))

/*
 *  割込みサービスルーチン管理ブロックから割込みサービスルーチンIDを取り
 *  出すためのマクロ
 *
 *  dcre（interrupt.h:126）はISRCBの実体配列からの差分で求めるが，FMP3の
 *  ISRCBはポインタ表（p_isrcb_table）経由で参照される個別のnamed static
 *  であり，ISRCB自身の配列位置から番号を引けない．そのためISRINIBへの
 *  ポインタ差分で求め，動的生成ISR（p_isrinibがaisrinib_tableを指す）と
 *  静的生成ISR（p_isrinibがisrinib_tableを指す）の2レンジに分ける
 *  （段階2のCYCID・段階3aのSEMID・段階3bのDTQIDと同型）．AID_ISRが無い
 *  構成ではtnum_isr == tnum_sisrとなり第1項が常に偽＝静的レンジの式に落ちる．
 */
#define	ISRID(p_isrcb) \
	((((p_isrcb)->p_isrinib >= aisrinib_table) \
		&& ((p_isrcb)->p_isrinib < &aisrinib_table[tnum_isr - tnum_sisr])) \
	  ? ((ID)(((p_isrcb)->p_isrinib - aisrinib_table) + TMIN_ISRID + tnum_sisr)) \
	  : ((ID)(((p_isrcb)->p_isrinib - isrinib_table) + TMIN_ISRID)))

/*
 *  割込みサービスルーチン機能の初期化
 */
extern void	initialize_isr(PCB *p_my_pcb);

/*
 *  割込みサービスルーチンの呼出し
 */
extern void	call_isr(ISRQCB *p_isr_queue);
```

★`initialize_isr` の引数が `PCB *p_my_pcb` である理由（dcre は `void`）：
FMP3 の共通枠組みが `_kernel_initialize_<object>(p_my_pcb);` を生成する
（`kernel/kernel.py:257-258` / `kernel.trb:274-275`）ため。段階1〜3b と同じ。

- [ ] **Step 3: `kernel/interrupt.c` — 索引マクロ・キー比較・enqueue_isr・search_isr_queue**

`VALID_INTPRI_CHGIPM` のブロック（`kernel/interrupt.c:130-136`）の**直後**、
`#ifdef TOPPERS_intini`（`:141`）の**直前**に挿入する：

```c
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
 *  キューが空のときisrseqのカウンタを0へ戻す．これによりu32のラップは
 *  実用上到達しない．★副作用として，走査中にキューが空になった後で
 *  acre_isrされたISRは，走査側の継続キーより小さい世代番号を持つため
 *  その割込みでは呼ばれない（次の割込みで呼ばれる）．安全側の脱落であり，
 *  二重実行は起こらない．
 *
 *  ジャイアントロックを取得した状態で呼び出すこと．
 */
Inline void
enqueue_isr(ISRQCB *p_isr_queue, ISRCB *p_isrcb)
{
	QUEUE	*p_entry;
	PRI		isrpri = p_isrcb->p_isrinib->isrpri;

	if (queue_empty(&(p_isr_queue->isr_queue))) {
		p_isr_queue->isrseq = 0U;
	}
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
```

★`Inline` は FMP3 の他の内部関数と同じキーワードである（`kernel/dataqueue.c` 等を参照）。
★`enqueue_isr`/`search_isr_queue` は `#ifdef TOPPERS_*` ブロックの**外**に置く
（`Inline` なので各区画から使われる。dcre も同じ位置）。

- [ ] **Step 4: `kernel/interrupt.c` — `free_isrcb` と `initialize_isr`**

Step 3 のブロックの直後、`#ifdef TOPPERS_intini` の**直前**に挿入する：

```c
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
```

★**書いてはいけないもの**：`p_isrinib->iprcid = ...` / `->affinity = ...` /
`p_isrcb->p_pcb = ...`。ISRINIB にも ISRCB にもこれらのフィールドは無い
（段階2 Constraint 4 の類推適用は本段階でも**禁止**である）。
★`aisrinib_table[j]` の `exinf`/`p_isr_queue`/`isr`/`isrpri` は**設定しない**
（`acre_isr` が設定する。dcre も同じ）。BSS のゼロのまま残るが、
`isratr == TA_NOEXS` のゲートを通らない限り参照されない。

- [ ] **Step 5: `kernel/interrupt.c` — `call_isr` の暫定版（★Task 4 で捨てる）**

Step 4 のブロックの直後に挿入する：

```c
/*
 *  割込みサービスルーチンの呼出し
 */
#ifdef TOPPERS_isrcal

/*
 *  ★★これはTask 3の暫定実装である．Task 4でMP対応版へ全面的に置き換える．
 *
 *  この版はdcre interrupt.c:240-260の素朴な単方向走査であり，
 *  「走査とacre_isr/del_isrが時間的に排他である」という単一プロセッサの
 *  前提に依存している．FMP3では成立しないため，このままでは使えない．
 *  型（ISRQCB *）と表の結線が正しいことをTask 3の時点で独立に検証する
 *  ためだけに置いている．
 */
void
call_isr(ISRQCB *p_isr_queue)
{
	QUEUE	*p_queue;
	ISRCB	*p_isrcb;

	for (p_queue = p_isr_queue->isr_queue.p_next;
							p_queue != &(p_isr_queue->isr_queue);
							p_queue = p_queue->p_next) {
		p_isrcb = (ISRCB *) p_queue;
		LOG_ISR_ENTER(ISRID(p_isrcb));
		(*(p_isrcb->p_isrinib->isr))(p_isrcb->p_isrinib->exinf);
		LOG_ISR_LEAVE(ISRID(p_isrcb));

		if (p_queue->p_next != &(p_isr_queue->isr_queue)) {
			/* ISRの呼出し前の状態に戻す */
			if (sense_lock()) {
				unlock_cpu();
			}
		}
	}
}

#endif /* TOPPERS_isrcal */
```

`LOG_ISR_ENTER`/`LOG_ISR_LEAVE` のデフォルト定義を、`kernel/interrupt.c` の
トレースログマクロ群の先頭（`#ifndef LOG_DIS_INT_ENTER` の**直前**、`:54` 付近）に置く
（dcre `interrupt.c:55-61` と同じ）：

```c
#ifndef LOG_ISR_ENTER
#define LOG_ISR_ENTER(isrid)
#endif /* LOG_ISR_ENTER */

#ifndef LOG_ISR_LEAVE
#define LOG_ISR_LEAVE(isrid)
#endif /* LOG_ISR_LEAVE */
```

★`kernel_cfg.c` も同名のデフォルト定義を出す（`kernel/interrupt.py:70-78`）が、
どちらも `#ifndef` ガード付きなので衝突しない。**cfg 側の出力は変更しない。**

- [ ] **Step 6: 配線（allfunc.h / Makefile.kernel）**

`kernel/allfunc.h` の `/* interrupt.c */` 節（`:279-287`）の
`#define TOPPERS_intini`（`:280`）の**直前**に 2 行：

```c
#define TOPPERS_isrini
#define TOPPERS_isrcal
```

（`acre_isr`/`del_isr` の 2 行は Task 5 で足す。ALLFUNC ビルド＝`CMakeLists.txt:562` が
全区画を有効化するため、**ここに書かないと関数が 1 つもコンパイルされない**。）

`kernel/Makefile.kernel:122-123` を次に変更（上流形式の維持。CMake は参照しない）：
```
interrupt = isrini.o isrcal.o \
		intini.o dis_int.o ena_int.o clr_int.o ras_int.o prb_int.o \
		chg_ipm.o get_ipm.o
```
**`KERNEL_FCSRCS`（`Makefile.kernel:51-56`）は触らない**（22 個のまま）。

- [ ] **Step 7: rename 追加・再生成**

`kernel/kernel_rename.def` の `# interrupt.c` 節（`:140-141`。★段階3a/3b で他の節が
伸びているので**行番号ではなく `# interrupt.c` の文字列を目印にする**）を次に置換する：

```
# interrupt.c
initialize_interrupt
initialize_isr
call_isr
free_isrcb

```

`# kernel_cfg.c` 節の `p_almcb_table` の**直後**（`tnum_def_inhno` の直前）に 9 行：

```
tmax_isrid
tmax_sisrid
isrinib_table
aisrinib_table
p_isrcb_table
isrorder_table
tnum_isr_queue
isr_queue_list
isr_queue_table
```

（`acre_isr`/`del_isr` は**公開名なのでリネームしない**＝段階1〜3b の `acre_tsk`/`acre_dtq` と同じ。
★**`call_isr` のリネームは必須**である：cfg が生成する inthdr は
`_kernel_call_isr(...)` と書くため（`kernel/interrupt.py` の Step 7(c)）。
dcre `kernel_rename.def:116` も同じ。）

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core/kernel && ruby ../utils/genrename.rb kernel && cd ..
cd /home/honda/TOPPERS/FMP3/fmp3_core
git diff --stat kernel/kernel_rename.h kernel/kernel_unrename.h   # 期待: 各 +12 行・削除0
grep -n "_kernel_call_isr\|_kernel_initialize_isr\|_kernel_free_isrcb" kernel/kernel_rename.h
grep -n "_kernel_isr_queue_table\|_kernel_isrorder_table\|_kernel_tmax_sisrid" kernel/kernel_rename.h
```
期待: 12 個すべてが `kernel_rename.h` に実在。
**削除行が 1 行でもあれば不合格**（genrename の実行位置ミス等を疑う）。

- [ ] **Step 8: 全8構成ビルド + 等価性（★ここで初めてリンクが通る）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --build build/$p > /tmp/isr-t3-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/isr-t3-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for d in musca_b1-2core-tmix musca_b1-2core-tint2; do
  cmake --build build/$d > /tmp/isr-t3-build-$d.log 2>&1; echo "$d build rc=$?"
  tools/cfg_equivalence.sh build/$d > /tmp/isr-t3-eq-$d.log 2>&1; echo "$d eq rc=$?"
done
```
期待: **全て rc=0**。**exit=2 は不合格**。
★`build/musca_b1-2core-tmix` は `ENA_DYNISR(INTNO1)` + `AID_ISR(2)` を含むので、
ここで `_kernel_call_isr` / `_kernel_isr_queue_table` / `_kernel_aisrcb_*` が
**実際にリンクされる**ことを同時に検査している。
★`build/musca_b1-2core`（sample1）と `build/musca_b1-2core-tint2` は
opt-in を持たないので、**`initialize_isr` の静的ループが `p_isr_queue == NULL` の枝を
必ず通る**（＝Step 4 の NULL ガードが生きた経路である）。

- [ ] **Step 9: ★恒常出力のコスト実測（Task 2 Step 16 の実行）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
find build/musca_b1-2core -name '*.elf' -exec size {} \; > /tmp/isr-t3-after-size.txt 2>&1
echo "--- before (Task 2 Step 1) ---"; cat /tmp/isr-t2-base-size.txt
echo "--- after  (Task 3 Step 8)  ---"; cat /tmp/isr-t3-after-size.txt
```
`text`/`data`/`bss` の増分を計算し、**実測値**を確認結果表と `DIVERGENCE_MAP.md` に書く。
期待のオーダー：静的 ISR 2 個（sample1/musca_b1-2core は `INTNO1_ISR`・`INTNO2_ISR`・
`ISR_SIO` — **実際の個数は生成物の `_kernel_isrinib_table` を数えて確かめる**）×
`sizeof(ISRINIB)`（32bit で 20 バイト）+ 同数 × `sizeof(ISRCB)`（32bit で 20 バイト）+
ポインタ表 + `isrorder_table` + `initialize_isr` のコード。
★**桁が違ったら設計の誤り**である（例：`isr_queue_table` が空でないなど）。
その場合は生成物を見て原因を特定してから先へ進む。

- [ ] **Step 10: QEMU 起動（非退行）— musca_b1-2core と kria_arm64**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/isr-t3-run-musca.log 2>&1
grep -c 'Processor [12] start\.' /tmp/isr-t3-run-musca.log     # 期待: 2
grep -c 'Sample program starts' /tmp/isr-t3-run-musca.log      # 参考: サンプル走行の証拠
pgrep -a qemu                                                  # 期待: 何も出ない
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_arm64 --target run > /tmp/isr-t3-run-arm64.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/isr-t3-run-arm64.log    # 期待: 4
pgrep -a qemu                                                  # 期待: 何も出ない
```
★`initialize_isr` は**すべての構成で起動時に走る**（訂正D）。起動が壊れていないことは
この段階で必ず確かめる。kria_arm64 は 64bit ＝ ポインタ差分による 2 レンジ `ISRID` が
最も壊れやすい構成なので、必ず両方走らせる。

- [ ] **Step 11: `test_int2` の実行（インライン連鎖の非退行）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tint2 --target run > /tmp/isr-t3-int2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t3-int2.log
grep -c 'Check point' /tmp/isr-t3-int2.log
grep -n 'Check point' /tmp/isr-t3-int2.log
pgrep -a qemu
```
期待: `TTSP_RESULT: PASS` が実在し、`Check point 1〜6` が順に出る
（`test_int2` は 6 個 = 1..5 + `check_finish(6)` 自身）。
★**行数の期待値は実測を正とする**。段階3a では見積りを 3 回続けて外している。
最初の実行で実際の本数を数え、以後はそれを期待値にする。

- [ ] **Step 12: 台帳とコミット**

`DIVERGENCE_MAP.md` に次の行を追記する（種別はすべて `mod (dcre-port)`）：
- `kernel/interrupt.h（dcre動的ISR Task 3）` — 理由：
  「ISR のランタイムオブジェクトを新設した（FMP3 には従来存在しなかった）。
  `#include <queue.h>` を追加し、`ISRQCB`（★dcre に無い FMP3 の新設型。
  キューごとの enqueue 世代番号 `isrseq` を持つ）・`ISRINIB`・`ISRCB`（★dcre の
  2 フィールドに `isrseq` と `running` を追加）・`ISR_ENTRY` の 4 型、
  `tnum_isr_queue`/`isr_queue_list`/`isr_queue_table`/`free_isrcb`/`tmax_isrid`/
  `tmax_sisrid`/`isrinib_table`/`aisrinib_table`/`isrorder_table`/`p_isrcb_table` の
  extern、`tnum_isr`/`tnum_sisr`、2 レンジ `ISRID`、`initialize_isr`/`call_isr` の宣言を追加。
  `ISRID` は dcre の配列差分式（`interrupt.h:126`）ではなく段階2 `CYCID`・段階3a `SEMID`・
  段階3b `DTQID` と同型の INIB ポインタ差分の 2 レンジ式にした（FMP3 の CB は
  ポインタ表経由の named static であるため）」
- `kernel/interrupt.c（dcre動的ISR Task 3）` — 理由：
  「`LOG_ISR_ENTER`/`LOG_ISR_LEAVE` のデフォルト定義、`INDEX_ISR`/`get_isrcb`、
  `ISR_KEY_GT`（★dcre に無い。走査の安定キー比較）、`enqueue_isr`（dcre
  `interrupt.c:182-195` + 空キューでの `isrseq` リセット）、`search_isr_queue`
  （dcre `:267-293` の転写。型が `ISRQCB *` に変わるだけ）、`QUEUE free_isrcb;`、
  `initialize_isr`（dcre `:207-231` + マスタ限定化 + `p_isr_queue == NULL` の
  スキップ）、`call_isr` の暫定版（Task 4 で置換）を追加。
  **ISR は非親和オブジェクトであり `iprcid`/`affinity`/`p_pcb` の充填は 1 行も
  書いていない**。`initialize_isr` がマスタ限定なのは共有データの初期化だからで、
  他プロセッサへの可視性は呼出し後の `barrier_sync` が保証する」
- `kernel/check.h（dcre動的ISR Task 3）`（既存行の理由欄に「`VALID_ISRID`・
  `VALID_ISRPRI`・`CHECK_OBJ` を追加。3 つとも dcre `check.h:64,73-74,235-240` と同一。
  段階3b の `VALID_DPRI` 追加と同じ前例」を追記）
- `kernel/allfunc.h（dcre動的ISR Task 3）`（同、「`/* interrupt.c */` 節に
  `TOPPERS_isrini`/`TOPPERS_isrcal` を追加」）
- `kernel/Makefile.kernel（dcre動的ISR Task 3）`（同、「`interrupt =` 行に
  `isrini.o`/`isrcal.o` を追加。`KERNEL_FCSRCS` は不変」）
- `kernel/kernel_rename.def（dcre動的ISR Task 3）` — 理由：「`# interrupt.c` 節に
  `initialize_isr`/`call_isr`/`free_isrcb`、`# kernel_cfg.c` 節に `tmax_isrid`/
  `tmax_sisrid`/`isrinib_table`/`aisrinib_table`/`p_isrcb_table`/`isrorder_table`/
  `tnum_isr_queue`/`isr_queue_list`/`isr_queue_table` を追加（計 12）。
  ★`call_isr` のリネームは必須である（cfg が生成する inthdr が
  `_kernel_call_isr(...)` を呼ぶため）」
- `kernel/kernel_rename.h・kernel_unrename.h（dcre動的ISR Task 3）` — 種別
  `mod (dcre-port, 生成物)`、理由：「`utils/genrename.rb kernel` で再生成（手編集ではない）。
  各 +12 行・削除 0 行」

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add -A && git commit -m "feat(kernel): ISRのランタイムオブジェクトを新設（ISRQCB/ISRINIB/ISRCB・free_isrcb・initialize_isr・2レンジISRID・VALID_ISRID/ISRPRI/CHECK_OBJ・dcre動的ISR）"
```

---
### Task 4: ★最難関(1) — call_isr のキュー走査（MP 版・安定キー付き）

**推奨モデル:** 上位（opus 相当）。**本段階で最も novel な部分**であり、
dcre からの転写では済まない。段階1〜3b に対応物が無い。

**Files:**
- Modify: `kernel/interrupt.c`（`call_isr` を Task 3 の暫定版から全面的に書き換える）
- Modify: `test/test_int2.cfg`（★**一時的に**編集して実行検証したのち必ず復元する）
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Consumes: Task 3 の `ISRQCB` / `ISRCB` / `ISRINIB` / `ISR_KEY_GT` / `ISRID` /
  `LOG_ISR_ENTER` / `LOG_ISR_LEAVE`。Task 2 が生成した
  `_kernel_inthdr_<intno>() { _kernel_call_isr(&(_kernel_isr_queue_table[i])); }`。
  Task 1 の確認結果表（`signal_time`・`call_cyclic`・`force_unlock_spin` の実測、訂正F）。
- Produces: `call_isr` の MP 版。Task 5 の `del_isr`（quiesce）が
  **`running` ビットを立てる／落とすのはこの関数だけである**という前提に依存する。

**★この Task が満たすべき性質（Task 6 で実証する）:**
1. ISR は `(isrpri, isrseq)` の辞書式昇順に、各回 1 回だけ呼ばれる。
2. ISR 本体はジャイアントロック・CPU ロックのどちらも保持しない状態で呼ばれる。
3. ISR 本体がロックしたまま戻っても、次の ISR はロック解除状態で呼ばれる。
4. 走査中に別コアが `del_isr`/`acre_isr` を行っても、取りこぼしも二重実行も起きない。
5. `call_isr` から戻るとき、CPU ロックは必ず解除されている。
6. **`ENA_DYNISR` の無い intno は 1 命令たりとも影響を受けない**
   （この関数はそもそも呼ばれない）。

- [ ] **Step 1: 前提の現物確認（コメントに書く根拠が真であることを確かめる）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
sed -n '706,725p' kernel/time_event.c        # signal_time の入口（assert 2本 + lock_cpu + acquire_glock）
sed -n '536,552p' kernel/cyclic.c            # call_cyclic の3分岐（訂正F）
sed -n '158,178p' kernel/spin_lock.c         # force_unlock_spin
grep -n "get_my_prcidx" kernel/*.c kernel/*.h arch/*/common/*.h | head
grep -n "sense_context\|define assert" kernel/kernel_impl.h include/t_stddef.h | head
sed -n '452,458p' kernel/interrupt.trb       # 「割込みハンドラはマイグレートしない」のコメント
grep -n "割込み優先度\|IPM\|intpri" doc/porting.txt | grep -in "handler\|ハンドラ" | head -10
```
確認すること：
- `signal_time`（`kernel/time_event.c:709-722`）が
  `PCB *p_my_pcb = get_my_pcb();` を**CPU ロック前**に取り、
  `assert(sense_context(p_my_pcb)); assert(!sense_lock());` の 2 本を置いてから
  `lock_cpu(); acquire_glock();` していること。
- `call_cyclic`（`kernel/cyclic.c:541-549`）の 3 分岐（訂正F）。
- `get_my_prcidx()` が使えること（`kernel/time_event.c:150` の
  `if (TOPPERS_TEPP_PRC & (1 << get_my_prcidx()))` が既存の用例）。
  ★**0 始まりのインデックス**であり `prcid - 1` に相当する。
- `kernel/interrupt.trb:452-456`（`interrupt.py:404` 付近）のコメント
  「**割込みハンドラは，他のプロセッサへマイグレートすることはないため，
  CPU ロック状態にせずに自プロセッサの PCB にアクセスしてよい**」
  → `call_isr` の冒頭で `get_my_pcb()` をロック前に呼んでよい根拠。
- **★「同一 intno の割込みハンドラが同一コアで多重に走ることはない」**ことの根拠を探す。
  カーネル管理の割込みハンドラの実行中は当該割込み優先度以下の割込みがマスクされる
  （割込み優先度マスクの設定）ので、同じ割込み要求ラインが自分自身に割り込むことはない。
  ★**根拠が見つかったら file:line で記録し、コメントに書く。見つからなければ
  「未確認」と書き、コメントには断定を書かない**（Global Constraint 16）。
  この性質は `running` を**ビットマップ**（カウンタでない）で実装してよい根拠である。

- [ ] **Step 2: `call_isr` の MP 版（Task 3 の暫定版を丸ごと置換）**

`kernel/interrupt.c` の `#ifdef TOPPERS_isrcal` ブロックの中身を次で置き換える：

```c
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
 *      （time_event.c:709-768）に先例がある．
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
 *  (4) ISR本体からの復帰後のロック状態の復元はcall_cyclic（cyclic.c:541-549）
 *      と同じ3分岐である．sense_lock()が真のときはCPUロック状態のままなので
 *      lock_cpu()を呼んではならない．force_unlock_spinはスピンロックだけを
 *      解放し，CPUロック状態には触らない（spin_lock.c:162-176）．
 *
 *  (5) ★走査の終了時は必ずCPUロック解除状態で戻る．インライン連鎖は
 *      ロック復元コードをISRとISRの間にしか置かない（interrupt.trb:460-466の
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
	 *  状態にする前に自プロセッサのPCBを取得してよい（interrupt.trb:452-456
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
```

★**書き間違えやすい点（レビュー時に必ず見る）:**
1. `p_entry != &(p_isr_queue->isr_queue)` の `&` を落とすと無限ループになる。
2. `first ||` を落とすと最初の要素が呼ばれない（`cur` が (0,0) なので
   `isrpri >= 1` の要素は通るが、`isrpri` が 0 以下になりうるターゲットでは破綻する。
   `first` フラグで明示するのが正しい）。
3. `p_next->running &= ~my_bit;` を**ロック再取得の前**に置くと、
   quiesce 側と競合して読み書きが壊れる。**必ず `acquire_glock()` の後**。
4. `LOG_ISR_LEAVE` は ISR 本体の直後（ロック外）に置く。`ISRID(p_next)` は
   `p_isrinib` **ポインタ**の差分なので、ロック外でも壊れない
   （`p_isrcb->p_isrinib` を書き換えるのは `initialize_isr` だけである）。
5. `my_bit` は `get_my_prcidx()`（**0 始まり**）を使う。`prcid`（1 始まり）を
   そのまま使うとビット位置が 1 つずれる。

- [ ] **Step 3: ビルドと等価性（全構成 + 混在）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --build build/$p > /tmp/isr-t4-build-$p.log 2>&1; echo "$p build rc=$?"
done
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for d in musca_b1-2core-tmix musca_b1-2core-tint2; do
  cmake --build build/$d > /tmp/isr-t4-build-$d.log 2>&1; echo "$d build rc=$?"
  tools/cfg_equivalence.sh build/$d > /tmp/isr-t4-eq-$d.log 2>&1; echo "$d eq rc=$?"
done
```
期待: 全て rc=0。**cfg の生成物は Task 3 から 1 バイトも変わらない**
（本 Task は C のみ）。念のため：
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
diff -r /tmp/isr-base-generated build/musca_b1-2core/generated > /tmp/isr-t4-gen.txt
grep -c . /tmp/isr-t4-gen.txt    # Task 2 Step 10 の許容リストと同じ差分のみ
```

- [ ] **Step 4: ★★実行検証 — `test_int2` を一時的に opt-in させて同じ結果になること**

**これが本 Task の中心的な検証である。** `test_int2` は
- 同一 intno に 3 本の ISR（isrpri が 1,1,2）
- 記述順と isrpri 順を意図的に食い違わせている
- ISR がロックしたまま戻ったときの復元（C-1）と、戻り先タスクのロック状態（C-2）

を検査する。**キュー方式が正しければ、`test_int2` は 1 文字も変えずに PASS するはずである**
（訂正I の invariant：opt-in の有無で呼出し順序は変わらない）。

`test/test_int2.cfg` を**一時的に**編集する：`CLASS(CLS_PRC1) { ... }` の中、
`CRE_ISR(INTNO1_ISR_SECOND, ...)` の次の行に `ENA_DYNISR(INTNO1);` を足し、
ファイル末尾（クラスの外）に `AID_ISR(1);` を足す。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cp test/test_int2.cfg /tmp/isr-t4-int2.cfg.bak
# ここで上記の2行を追加する
grep -n "ENA_DYNISR\|AID_ISR" test/test_int2.cfg
cmake --build build/musca_b1-2core-tint2 > /tmp/isr-t4-int2-build.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tint2 > /tmp/isr-t4-int2-eq.log 2>&1; echo "eq rc=$?"
sed -n '/^_kernel_inthdr_65612(void)$/,/^}$/p' \
    build/musca_b1-2core-tint2/generated/kernel_cfg.c
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tint2 --target run \
    > /tmp/isr-t4-int2-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t4-int2-run.log
grep -n 'Check point' /tmp/isr-t4-int2-run.log
grep -n 'Unexpected\|Assertion failed\|## ' /tmp/isr-t4-int2-run.log | head
pgrep -a qemu
```
期待:
- 生成された `_kernel_inthdr_65612` が
  **`\t_kernel_call_isr(&(_kernel_isr_queue_table[0]));` の 1 行だけ**。
- `TTSP_RESULT: PASS` が実在。
- `Check point 1〜6` が**順に**出る。特に
  `Check point 2`（isr1・exinf=1）→ `Check point 3`（isr2・exinf=2）→
  `Check point 4`（isr3・exinf=3）の順序が**インライン連鎖のときと同じ**であること
  ＝**訂正I の invariant の実証**。
- `Assertion failed` が出ない。とくに isr2/isr3 の `check_assert(!isns_loc())`（C-1）と
  task1 の `check_assert(!sns_loc())`（C-2）が通ること
  ＝**ロック復元の 3 分岐（訂正F）と走査終了時のロック解除の実証**。
- `pgrep` の出力なし。

★**失敗したときの読み方:**
- `Check point 2` の直後で止まる → isr1 の `iloc_cpu()` の後、走査が
  ロックを取り直せていない（`sense_lock()` の枝で `lock_cpu()` を呼んでいる＝二重ロック）。
- `Check point 3` が出ずに `4` が出る → 走査キーの比較が誤っていて同一 isrpri の
  2 本目を飛ばしている（`ISR_KEY_GT` の `>` と `>=` の取り違え）。
- 何も出ない／ハングする → 内側ループの終端判定（`&` の落とし）を疑う。

- [ ] **Step 5: ★復元と再検証（省略禁止）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cp /tmp/isr-t4-int2.cfg.bak test/test_int2.cfg
git diff --stat test/test_int2.cfg       # 期待: 出力なし（差分ゼロ）
cmake --build build/musca_b1-2core-tint2 > /tmp/isr-t4-int2-rebuild.log 2>&1; echo "rc=$?"
diff /tmp/isr-t2-inthdr-before.txt \
     <(sed -n '/^_kernel_inthdr_65612(void)$/,/^}$/p' \
       build/musca_b1-2core-tint2/generated/kernel_cfg.c); echo "inthdr diff rc=$?"
grep -c "call_isr" build/musca_b1-2core-tint2/generated/kernel_cfg.c   # 期待: 0
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tint2 --target run \
    > /tmp/isr-t4-int2-restored.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t4-int2-restored.log ; pgrep -a qemu
```
期待: `git diff` が空、inthdr の `diff rc=0`（インライン連鎖に戻っている）、
`call_isr` が 0 個、`TTSP_RESULT: PASS`。
★**`test/test_int2.cfg` の一時編集をコミットに含めてはならない。**
`git status` で確認してからコミットする。

- [ ] **Step 6: QEMU 起動（非退行）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/isr-t4-run-musca.log 2>&1
grep -c 'Processor [12] start\.' /tmp/isr-t4-run-musca.log     # 期待: 2
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/polarfire_soc_kit-qemu --target run > /tmp/isr-t4-run-pf.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/isr-t4-run-pf.log       # 期待: 4
pgrep -a qemu
```
★polarfire_soc_kit-qemu は **intno がコア間で共有される唯一の QEMU 実行可能構成**
（訂正H）。opt-in は無いのでキュー方式は走らないが、`initialize_isr` と
恒常出力が 4 コア構成で壊れていないことを確認する。

- [ ] **Step 7: 台帳とコミット**

`DIVERGENCE_MAP.md` の `kernel/interrupt.c（dcre動的ISR Task 3）` の理由欄に追記する：
「Task 4 で `call_isr` を MP 対応版へ全面的に書き換えた。dcre `interrupt.c:240-260` の
素朴な単方向走査は『走査と acre/del が時間的に排他』という単一プロセッサの性質に
依存しており FMP3 では成立しないため、**転写ではなく新規設計**である。
(1) キュー参照はジャイアントロック下、(2) ISR 本体はロック外、
(3) 次の ISR は `(isrpri, isrseq)` の辞書式順序でロック再取得後に**再決定**
（ポインタ／ISRID はスロット再利用で曖昧になるため使えない。Codex 指摘 #1）、
(4) 実行中は `p_isrcb->running` に自コアのビットを立てる（`del_isr` の quiesce 用。
Codex 指摘 #2）、(5) ロック復元は `call_cyclic`（`cyclic.c:541-549`）と同じ 3 分岐、
(6) 走査終了時は必ず CPU ロック解除状態で戻る（インライン連鎖は最後の ISR が
残したロックを解除しないという既存の差があるが、キュー方式ではこれを解消している。
差が及ぶのは `ENA_DYNISR` された intno のみ）。
`test_int2` を一時的に opt-in させて実行し、チェックポイントの順序・exinf・
ロック復元（C-1/C-2）がインライン連鎖のときと同一であることを実測で確認した
（確認後 `test_int2.cfg` は復元済み）」

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git status --short           # test/test_int2.cfg が出ないこと
git add -A && git commit -m "feat(kernel): call_isr のキュー走査をMP対応版へ（(isrpri,isrseq)の安定キーで再決定・runningビットマップ・call_cyclic流のロック復元・dcre動的ISR）"
```

---
### Task 5: ★最難関(2) — acre_isr / del_isr（quiesce 付き）

**推奨モデル:** 上位（opus 相当）。**quiesce ループは段階1〜3b に前例が無い**
（サービスコールの中で他コアの完了を待つ）。デッドロック不成立の論証を
コメントに書くが、**その論証が真であることを自分で確かめてから書く**（Global Constraint 16）。

**Files:**
- Modify: `kernel/interrupt.c`（`acre_isr` / `del_isr` を追加）
- Modify: `kernel/allfunc.h`（`/* interrupt.c */` 節に 2 行）
- Modify: `kernel/Makefile.kernel`（`interrupt =` 行に `.o` 2 個）
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Consumes: Task 2 の `T_CISR` / `acre_isr`・`del_isr` の宣言 / `TARGET_ISRATR` / `TMIN_ISRID`。
  Task 3 の `free_isrcb` / `enqueue_isr` / `search_isr_queue` / `get_isrcb` / `ISRID` /
  `tnum_isr` / `tnum_sisr` / `VALID_ISRID` / `VALID_ISRPRI` / `CHECK_OBJ`。
  Task 4 の `call_isr`（`running` ビットを立てる／落とすのはこの関数だけである、という前提）。
  Task 1 の確認結果表（訂正A/B、`wait_tmout` の 5 行）。
- Produces: `ER_ID acre_isr(const T_CISR *)` / `ER del_isr(ID)`。Task 6 が使う。

- [ ] **Step 1: `kernel/interrupt.c` に `acre_isr` を追加**

`#endif /* TOPPERS_isrcal */` の**直後**、`#ifdef TOPPERS_intini` の**直前**に置く
（dcre の配置＝`interrupt.c:295-354` と同じ相対位置）。

```c
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
```

dcre（`interrupt.c:303-352`）からの適応点は**4 つ**：
1. `lock_cpu()` の直後に `acquire_glock()`／末尾に `release_glock()`（FMP3 の giant lock 規約）。
2. 空判定を `tnum_isr == 0` → `tnum_isr == tnum_sisr`（FMP3 は静的分が別レンジのため）。
3. `CHECK_PAR(VALID_INTNO_CREISR(intno))` を**削除**（訂正A）。
4. `p_isr_queue` の型が `ISRQCB *`（訂正C）、`p_isrcb->running = 0U;` の明示。

**★書いてはいけないもの**（段階2 Constraint 4 の類推禁止）：
`p_isrinib->iprcid = ...` / `->affinity = ...` / `p_isrcb->p_pcb = ...`。
ISRINIB にも ISRCB にもこれらのフィールドは無い。
**★T_CISR にコアを指定するフィールドを足さない**（Global Constraint 4）。

- [ ] **Step 2: `kernel/interrupt.c` に `del_isr` を追加（★quiesce）**

`acre_isr` の直後に置く。

```c
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
```

dcre（`interrupt.c:361-392`）からの適応点は**4 つ**：
1. `lock_cpu()` の直後に `acquire_glock()`／末尾に `release_glock()`。
2. ★**quiesce ループの新設**（dcre には無い。単一プロセッサでは不要だった）。
3. ★**`TA_NOEXS` の書込みを `queue_delete` の直後（quiesce の前）へ**（訂正B。
   dcre は quiesce が無いので順序の問題自体が存在しない）。
4. `p_isrinib` の取得を `TA_NOEXS` 書込みの直前に移動（順序の明示）。

★**dcre からの逸脱ではない点**：`CHECK_ID(VALID_ISRID(isrid))`（E_ID）は
dcre もそうである。段階3a 訂正D（`del_flg`）・段階3b 訂正F（`del_dtq`）のような
`CHECK_PAR` の不整合は `del_isr` には**無い**。**上流報告候補 d を拡張しない。**

- [ ] **Step 3: 配線（allfunc.h / Makefile.kernel）**

`kernel/allfunc.h` の `/* interrupt.c */` 節、Task 3 で足した `TOPPERS_isrcal` の直後に 2 行：

```c
#define TOPPERS_acre_isr
#define TOPPERS_del_isr
```

`kernel/Makefile.kernel` の `interrupt =` 行を次に変更：
```
interrupt = isrini.o isrcal.o acre_isr.o del_isr.o \
		intini.o dis_int.o ena_int.o clr_int.o ras_int.o prb_int.o \
		chg_ipm.o get_ipm.o
```
**`KERNEL_FCSRCS`（`Makefile.kernel:51-56`）は触らない**（22 個のまま）。
★`acre_isr`/`del_isr` は**公開名なのでリネームしない**（`kernel_rename.def` は
Task 3 で確定済み。**ここで足さない**）。

- [ ] **Step 4: 全8構成ビルド + 等価性 + 混在**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --build build/$p > /tmp/isr-t5-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/isr-t5-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for d in musca_b1-2core-tmix kria_arm64-tmix musca_b1-2core-tint2; do
  cmake --build build/$d > /tmp/isr-t5-build-$d.log 2>&1; echo "$d build rc=$?"
  tools/cfg_equivalence.sh build/$d > /tmp/isr-t5-eq-$d.log 2>&1; echo "$d eq rc=$?"
done
```
期待: 全て rc=0。**exit=2 は不合格**。
★`build/kria_arm64-tmix` が無ければ次で作る（64bit での `ISRID` 2 レンジ式と
`ISRQCB`/`ISRINIB` のアラインの検査になる）：
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --preset kria_arm64 -B build/kria_arm64-tmix \
  -DFMP3_APPLNAME=test_dcre_mix -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/isr-t5-a64mix-conf.log 2>&1
echo "conf rc=$?"
```

- [ ] **Step 5: compile-through control（リンクされていることの実証）**

`cfg_equivalence.sh` は生成文字列を比較するだけでコンパイルしない。
`acre_isr`/`del_isr` が**実際にオブジェクトに入っている**ことを別途確かめる。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
find build/musca_b1-2core-tmix -name '*.elf' | head -1
E=$(find build/musca_b1-2core-tmix -name '*.elf' | head -1)
nm "$E" > /tmp/isr-t5-nm.txt 2>&1
grep -n " T acre_isr$\| T del_isr$\| T _kernel_call_isr$\| T _kernel_initialize_isr$" /tmp/isr-t5-nm.txt
grep -n " B _kernel_free_isrcb$\| b _kernel_free_isrcb$" /tmp/isr-t5-nm.txt
grep -n "_kernel_isr_queue_table\|_kernel_aisrcb_" /tmp/isr-t5-nm.txt | head
```
期待: `acre_isr` / `del_isr` / `_kernel_call_isr` / `_kernel_initialize_isr` が
**テキストシンボルとして実在**し、`_kernel_free_isrcb` / `_kernel_isr_queue_table` /
`_kernel_aisrcb_1` / `_kernel_aisrcb_2` がデータシンボルとして実在すること。
★`nm` の記号名はツールチェーンで異なることがある。**出力を見て判断する**
（`grep` が空なら `grep -i "acre_isr" /tmp/isr-t5-nm.txt` で実際の形を確かめる）。

- [ ] **Step 6: QEMU 起動（非退行）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/isr-t5-run-musca.log 2>&1
grep -c 'Processor [12] start\.' /tmp/isr-t5-run-musca.log     # 期待: 2
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tint2 --target run > /tmp/isr-t5-int2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t5-int2.log ; pgrep -a qemu
```
期待: 2 コア起動と `test_int2` の PASS。
★この時点で**動的 ISR の実行時検証はまだ 1 つも無い**（Task 6 が行う）。
**「ビルドが通ったから動く」と書かないこと。** Task 5 の報告には
「実行時の検証は Task 6 へ持ち越し」と正直に書く（段階3b Task 5 と同じ扱い）。

- [ ] **Step 7: 台帳とコミット**

`DIVERGENCE_MAP.md` の `kernel/interrupt.c（dcre動的ISR Task 3）` の理由欄に追記する：
「Task 5 で `acre_isr`（dcre `interrupt.c:303-352` の転写）と `del_isr`
（同 `:361-392` の転写 + quiesce の新設）を追加。
dcre からの意図的な逸脱 5 件：
(1) glock の対化、
(2) 空判定 `tnum_isr == 0` → `tnum_isr == tnum_sisr`、
(3) ★`acre_isr` から `CHECK_PAR(VALID_INTNO_CREISR(intno))` を**削除**した
（FMP3 の `VALID_INTNO` は `(prcid, intno)` の 2 引数で呼出しコアに依存しうるため。
intno の検証は cfg が生成するグローバル適格 intno 表の二分探索のみで行い、
範囲外・CFG_INT 無し・未 ENA_DYNISR・DEF_INH 競合はすべて E_OBJ になる。
dcre は範囲外を E_PAR にするが、FMP3 ではコア非依存に区別する手段が存在しない。
Codex 指摘 #3 への対応）、
(4) ★`del_isr` に **quiesce ループ**を新設した（`running != 0` の間、glock と
CPU ロックを解放し `delay_for_interrupt` を挟んで取り直す。`wait.c:126-131` と
同一の 5 行）。これにより「`del_isr` が E_OK を返した時点で対象 ISR は実行中でなく
以後実行されない」という dcre 単一コアと同等の意味論を MP でも保証する。
Codex 指摘 #2 への対応、
(5) ★`del_isr` の `isratr = TA_NOEXS` を **quiesce の前**に置いた（quiesce 中は
ロックを解放するため、後に置くと同一 isrid への 2 本目の `del_isr` が
削除済みエントリに `queue_delete` を再実行してキューを壊す）。
段階3b の『属性の読みは TA_NOEXS の書込みより前』という不変量は ISR には
適用されない — ISR の `isratr` はビット検査にもマスク比較にも使われず、
`== TA_NOEXS` の同値比較と代入だけだからである（現物確認済み）。
★`del_isr` の `CHECK_ID`（E_ID）は dcre もそうであり逸脱ではない。
上流報告候補 d（`del_flg`/`del_dtq` の `CHECK_PAR`）は**拡張しない**」

- `kernel/allfunc.h` の理由欄に「`TOPPERS_acre_isr`/`TOPPERS_del_isr` を追加」を追記
- `kernel/Makefile.kernel` の理由欄に「`interrupt =` 行に `acre_isr.o`/`del_isr.o` を追加」を追記

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add -A && git commit -m "feat(kernel): acre_isr/del_isr（グローバル適格intno表の二分探索とquiesce）・TA_NOEXSをquiesce前に書く（dcre動的ISR）"
```

---
### Task 6: QEMU 回帰テスト test_dcre5

**推奨モデル:** 中位（sonnet）。ただし**手順4・手順5 のハンドシェイク設計は繊細**なので、
うまく動かないときは値を調整するのではなく**まず設計の前提（どちらのコアで何が走るか）を
紙に書いて確かめる**こと。

**Files:**
- Create: `test/test_dcre5.c` `test/test_dcre5.cfg` `test/test_dcre5.h`
- Modify: `test/MANIFEST`（`test_dcre4.h` の直後に 3 行）・
  `test/testexec.rb`（`"dcre4"` の直後に 1 行）
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:** Consumes Task 2〜5 の全成果。`syssvc/test_svc.h` の
`test_start` / `check_point`（= `check_point_prc(count, 0)`）/ `check_point_prc` /
`check_ercd` / `check_assert` / `check_finish`。
`target/musca_b1_gcc/target_test.h` の `INTNO1` / `INTNO1_INTATR` / `INTNO1_INTPRI` /
`intno1_clear()` / `INTNO2` / `INTNO2_INTATR` / `INTNO2_INTPRI` / `intno2_clear()`。

**★check_point の意味論（現物確認済み。段階3a で数え間違いが 3 回続いた箇所）:**
`syssvc/test_svc.c:110-190` より：
- `check_point_prc(count, prcid)` は `0 < prcid && prcid <= TNUM_PRCID` のとき
  `check_count[prcid - 1]`、それ以外（`prcid == 0`）のとき `check_count[0]` を使う。
  すなわち **PRC1 の `check_point()` と PRC2 の `check_point_prc(n,2)` は独立したカウンタ**。
- 出力は `prcid > 0` のとき `"Check point %d-%d passed."`＝**`prcid` が先**。
  `check_point_prc(1, 2)` は **`Check point 2-1 passed.`**。`prcid == 0` は `"Check point %d passed."`。
- **`check_finish(count)` は内部で `check_point_prc(count, 0)` を呼ぶ**ので、
  **`check_finish` 自身が "Check point <count> passed." を 1 本出す**。

**★本テストの設計原則（守ること）:**
1. **ISR の中では syslog を伴う API（`check_point`/`check_ercd`/`check_assert`）を
   呼ばない。** volatile なグローバルへ記録するだけにし、判定はタスクで行う。
   （`test_int2` は ISR 内で `check_point` を呼んでいるが、本テストの ISR は
   別コアのタスクと同時に走るので、出力の順序が非決定的になるのを避ける。
   ★この判断と理由をテストのヘッダコメントに書く。）
2. **すべての待ちループは上限つき**にする。上限に達したら `timeout` フラグを立てて
   抜け、タスク側で `check_assert` に落とす。**QEMU をハングさせない。**
3. 割込みの発生（`ras_int`）は**その割込みを受け付けるコアのタスクから**呼ぶ。
   `INTNO1` は PRC1、`INTNO2` は PRC2（`target_test.h` の符号化。訂正H）。

- [ ] **Step 1: 前提の現物確認（存在するものだけで書く）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
sed -n '55,120p' syssvc/test_svc.h
sed -n '105,195p' syssvc/test_svc.c
cat target/musca_b1_gcc/target_test.h
grep -n "^CRE_ISR\|^CFG_INT\|^AID_ISR\|^ENA_DYNISR\|^CRE_TSK" kernel/kernel_api.def
grep -n "define TMIN_ISRPRI\|define TMAX_ISRPRI\|define TSK_NONE" include/kernel.h
grep -n "CHECK_FUNC_ALIGN\|CHECK_FUNC_NONNULL" kernel/check.h arch/arm_m_gcc/common/*.h \
     target/musca_b1_gcc/*.h
grep -n "ext_tsk\|slp_tsk\|wup_tsk" test/test_dcre3.c test/test_dcre4.c | head
grep -rn "CLS_PRC2" test/test_mmutex1.cfg | head
grep -n "check_point" test/test_int2.c
```
確認すること：
- `test_start` / `check_point` / `check_point_prc` / `check_finish` / `check_assert` /
  `check_ercd` が存在する（無いものは使わない）。
- `CRE_ISR #isrid* { .isratr &exinf .intno &isr +isrpri }`（`kernel_api.def:13`）、
  `CFG_INT .intno* { .intatr +intpri }`（`:12`）、
  `AID_ISR .noisr`・`ENA_DYNISR .intno*`（Task 2 で追加した 2 行）。
- `TMIN_ISRPRI = 1` / `TMAX_ISRPRI = 16`（`include/kernel.h:654-655`）。
- ★**`CHECK_FUNC_NONNULL` / `CHECK_FUNC_ALIGN` が定義されているか**。
  定義されていなければ `FUNC_NONNULL(func)` は `true` に展開される
  （`kernel/check.h:401-432`）ので、**`isr == NULL` の E_PAR はテストできない**。
  → **定義されていなければそのケースを書かない**。書かなかった事実を記録する。
- `ext_tsk();`・`slp_tsk()`・`wup_tsk()` の使い方（既存テストに合わせる）。
- `CLS_PRC2` が `test_common1.cfg` 経由で使えること。
★食い違ったら**現物に合わせてテストを直し、直した事実を記録する**。

- [ ] **Step 2: `test/test_dcre5.h`**

```c
/*
 *		動的生成API（acre_isr/del_isr）のテスト
 */

#include <kernel.h>
#include "target_test.h"

#define HIGH_PRIORITY	9
#define MID_PRIORITY	10
#define LOW_PRIORITY	11

#ifndef STACK_SIZE
#define	STACK_SIZE		4096
#endif /* STACK_SIZE */

/*
 *  ENA_DYNISR されていない有効な割込み番号（acre_isr の E_OBJ 検査用）
 *
 *  INTNO1 は musca_b1 では (1 << 16) | (60 + 16) ＝ PRC1 の予備 NVIC IRQ60
 *  である．+1 は同じプロセッサの予備 IRQ61 で，CFG_INT もされていない．
 *  「有効範囲内だが適格 intno 表に載っていない」ケースを作るために使う．
 */
#define INTNO_UNOPTED	(INTNO1 + 1)

/*
 *  有効範囲外の割込み番号（acre_isr の E_OBJ 検査用）
 *
 *  ★dcre は範囲外を E_PAR にするが，FMP3 の acre_isr は範囲検査を持たない
 *  （FMP3 の VALID_INTNO が (prcid, intno) の2引数で呼出しコアに依存しうる
 *  ため．ISR段階の訂正A）．したがって範囲外も E_OBJ になる．
 *  値は tools/cfg_error_tests/e_par_creisr_intno_keyerror.cfg と同じ 99999．
 */
#define INTNO_BAD		99999

/*  ISR の呼出し順序を記録するログ  */
#define ISR_LOG_SIZE	16

/*  待ちループの上限（QEMU をハングさせないための保険）  */
#define SPIN_LIMIT		100000000U

/*
 *  quiesce 実証用の長い ISR の空回し回数
 *
 *  ★この値は変異 control の成立条件でもある．del_isr の quiesce ループを
 *  落とすと，del_isr が ISR 本体の完了を待たずに戻り，直後の
 *  check_assert(long_finished) が失敗する．そのためには「別コアのタスクが
 *  long_started を観測してから del_isr のジャイアントロック取得までに
 *  到達する時間」より，この空回しが十分に長い必要がある．
 *  足りない場合は増やす（減らさない）．調整したら最終値を記録すること．
 */
#define LONG_ISR_SPIN	2000000U

/*  PRC2 のタスクへの指令  */
#define CMD_NONE		0
#define CMD_HANDSHAKE	1		/*  走査中の del/acre（手順4）  */
#define CMD_FIRE_LONG	2		/*  quiesce 実証の割込み発生（手順5）  */
#define CMD_QUIT		3

#ifndef TOPPERS_MACRO_ONLY
extern void	task1(EXINF exinf);
extern void	task3(EXINF exinf);
extern void	static_isr(EXINF exinf);
extern void	dyn_isr(EXINF exinf);
extern void	long_isr(EXINF exinf);
extern void	ctx_isr(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */
```

- [ ] **Step 3: `test/test_dcre5.cfg`**

```c
/*
 *		動的生成API（acre_isr/del_isr）のテストの
 *		システムコンフィギュレーションファイル
 *
 *  $Id$
 */
INCLUDE("test_common1.cfg");

#include "test_dcre5.h"

CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });

	/*
	 *  INTNO1（PRC1）：静的 ISR 2本 ＋ ENA_DYNISR
	 *
	 *  静的 ISR の isrpri を 2 と 4 にして，動的 ISR（isrpri 1/3/5）と
	 *  交互に並ぶようにしてある．exinf はログに記録する文字である．
	 *  CFG_INT・CRE_ISR・ENA_DYNISR は同一クラスの囲みの中に書く．
	 */
	CFG_INT(INTNO1, { INTNO1_INTATR, INTNO1_INTPRI });
	CRE_ISR(ISR_S4, { TA_NULL, '4', INTNO1, static_isr, 4 });
	CRE_ISR(ISR_S2, { TA_NULL, '2', INTNO1, static_isr, 2 });
	ENA_DYNISR(INTNO1);
}

CLASS(CLS_PRC2) {
	/*  TASK3: PRC2 側の手先（割込み発生と，走査中の del/acre）  */
	CRE_TSK(TASK3, { TA_ACT, 3, task3, HIGH_PRIORITY, STACK_SIZE, NULL });

	/*
	 *  INTNO2（PRC2）：静的 ISR を持たない動的専用の割込み番号．
	 *  quiesce の実証に使う（PRC2 で ISR が走っている間に PRC1 から del）．
	 */
	CFG_INT(INTNO2, { INTNO2_INTATR, INTNO2_INTPRI });
	ENA_DYNISR(INTNO2);
}

/*  AID_ISR はクラス外専用（Task 2 の E_RSATR 検査対象）  */
AID_ISR(4);
```

★`CRE_ISR` の記述順を `ISR_S4` → `ISR_S2` にしてあるのは意図的である
（記述順と isrpri 順を食い違わせて、**isrpri 順に呼ばれること**を検出できるようにする。
`test_int2` と同じ流儀）。
★`AID_ISR(4)` は手順6 の E_NOID を出すために**ちょうど 4 個**にしてある。
本数を変えると手順6 の期待値が変わる。

- [ ] **Step 4: `test/test_dcre5.c`**（著作権ヘッダは `test/test_dcre4.c` と同形式で付ける）

```c
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
 *	TASK3: 高優先度タスク，TA_ACT属性（静的・PRC2．手先）
 *	INTNO1（PRC1）: 静的 ISR_S4(isrpri 4)・ISR_S2(isrpri 2) ＋ ENA_DYNISR
 *	INTNO2（PRC2）: 静的 ISR なし ＋ ENA_DYNISR
 *	AID_ISR(4): 動的スロット4個
 *
 * 【チェックポイント】
 *
 *	PRC1（check_count[0]，TASK1）: 1..9 + check_finish(10)
 *	PRC2（check_count[1]，TASK3）: 1,2（出力は "Check point 2-1/2-2 passed."）
 *	  ＝ログ中の "Check point" 行は合計 12 本（check_finish 自身の1本を含む）
 *	  ★この本数は実測で確かめ，違っていたら実測値を正とする．
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
 *  PRC2 のタスクへの指令
 */
static volatile uint_t	prc2_cmd;
static volatile ID		hs_del_isrid;	/*  走査中に削除する ISRID  */
static volatile ER_ID	hs_acre_erid;	/*  走査中に生成した ISRID（結果）  */
static volatile bool_t	prc2_quit;

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
	isr_log_put((char)(intptr_t) exinf);
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
	char		c = (char)(intptr_t) exinf;

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
 *  PRC2 側の手先
 */
void
task3(EXINF exinf)
{
	uint32_t	i;
	T_CISR		cisr;

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
	check_assert(erid > ISR_SIO);
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
	check_ercd(del_isr(ISR_SIO), E_OBJ);

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

	prc2_quit = true;
	check_finish(10);
}
```

**注意（実装者へ）:**
- `(char)(intptr_t) exinf` の二重キャストは `EXINF` が `intptr_t` 系のため。
  **`EXINF` の実体を `include/kernel.h` で確かめ、警告が出ない書き方に直すこと。**
  直したら記録する。
- `cisr.exinf = (EXINF) 'a';` も同様。
- `check_ercd(del_isr(TNUM_ISRID + 1), E_ID);` の `TNUM_ISRID` は `kernel_cfg.h` の
  マクロ（静的 + AID の総数）。★`tmax_isrid` はアプリから見えないのでこれを使う。
- `ISR_SIO` は `serial.cfg` 由来の静的 ISR の ID マクロ。**`kernel_cfg.h` に
  実在することを確かめてから使う**（無ければその行を落とし、落とした事実を記録する）。
- `dly_tsk(1U)` は `TASK3` のアイドル待ち。**`dly_tsk` の最小値**が
  ターゲットで異なる場合は適切な値へ直す。
- `isr_log_is()` は `isr_log_cnt` を 2 回読むので、**ISR が走っていない時点でのみ
  呼ぶこと**（本テストはすべてそうなっている）。
- **チェックポイント番号は PRC1 が 1..9 + `check_finish(10)`、PRC2 が
  `(1,2)`・`(2,2)` の 2 個。各プロセッサ内で単調増加であることを QEMU 出力で確認する。**

- [ ] **Step 5: 登録**
  - `test/MANIFEST` の `test_dcre4.h` の直後に、アルファベット順で
    `test_dcre5.c` `test_dcre5.cfg` `test_dcre5.h` の 3 行
    （`test_dcre4` < `test_dcre5` < `test_dcre_mix` の並び）。
  - `test/testexec.rb` の `"dcre4"    => { SRC: "test_dcre4" },` の直後に
    `"dcre5"    => { SRC: "test_dcre5" },`。

- [ ] **Step 6: ビルド・等価性**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --preset musca_b1-2core -B build/musca_b1-2core-tdcre5 \
  -DFMP3_APPLNAME=test_dcre5 -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/isr-t6-conf.log 2>&1
echo "conf rc=$?"
cmake --build build/musca_b1-2core-tdcre5 > /tmp/isr-t6-build.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tdcre5 > /tmp/isr-t6-eq.log 2>&1; echo "eq rc=$?"
G=build/musca_b1-2core-tdcre5/generated/kernel_cfg.c
grep -n "_kernel_call_isr" $G
grep -n "const ISR_ENTRY _kernel_isr_queue_list" -A6 $G
sed -n '/^const ISRINIB _kernel_isrinib_table/,/^};/p' $G
```
期待:
- conf/build/eq とも rc=0。**eq rc=2 は不合格。**
- `_kernel_call_isr` が **2 箇所**（`INTNO1` と `INTNO2` の inthdr）。
- `_kernel_isr_queue_list[2]` に `INTNO1`（65612）と `INTNO2`（131148）が
  **intno の昇順**で並ぶ（65612 < 131148）。
- `_kernel_isrinib_table` に `ISR_SIO`（`p_isr_queue` が `NULL`）と
  `ISR_S4`/`ISR_S2`（`&(_kernel_isr_queue_table[0])`）が入る。
  ★**`ISR_SIO` の `p_isr_queue` が NULL であること**が、
  「opt-in していない intno の静的 ISR はキューに入らない」ことの生成物側の証拠である。

- [ ] **Step 7: 実行**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 120 cmake --build build/musca_b1-2core-tdcre5 --target run \
  > /tmp/isr-t6-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t6-run.log
grep -c 'Check point' /tmp/isr-t6-run.log
grep -n 'Check point' /tmp/isr-t6-run.log
grep -n 'Unexpected\|Assertion failed\|## ' /tmp/isr-t6-run.log | head
pgrep -a qemu
```
期待:
- `TTSP_RESULT: PASS` が実在（rc は見ない — Constraint 11）。
- `grep -c 'Check point'` が **12**（PRC1 の 1..9 の 9 本 + `check_finish(10)` 自身の
  1 本 + PRC2 の `2-1`/`2-2` の 2 本）。
  ★**この本数は見積りである。実測が違ったら実測を正とし、計画の期待値のほうを直して
  記録する**（段階3a では 3 回続けて外している）。
- `Check point 2-1 passed.` と `Check point 2-2 passed.` が**両方**出る。
- `Unexpected` / `Assertion failed` / `## ` の行が**出ない**。
- `pgrep` の出力なし。

★`timeout` は 120 秒（手順4・手順5 の待ちループと `LONG_ISR_SPIN` があるため
`test_dcre4` より長めに取る）。

★**失敗したときの読み方（手順別）:**
- **手順1（"24"）で落ちる** → キュー方式の静的 ISR の並びが isrpri 順になっていない。
  `isrorder_table` の順序（訂正I）か `enqueue_isr` の挿入位置を疑う。
- **手順2（"a2b4c"）で落ちる** → 静的と動的の混在順序。`enqueue_isr` の比較が
  `<` か `<=` かを疑う。
- **手順3（"2ABC4"）で落ちる** → `ISR_KEY_GT` の第2キー（isrseq）。
  ログが `"2A4"` なら isrpri だけで判定している。
- **手順4（"2ACD4"）で落ちる** → 走査の再決定。ログが `"2AC4"` なら
  acre された 'D' が拾われていない（isrseq の採番かキュー挿入位置）。
  `"2ABCD4"` なら del が走査に反映されていない。
  `hs_isr_timeout` で落ちるなら PRC2 の TASK3 が動いていない
  （`prc2_cmd` の設定順や `TASK3` の起動を疑う。**LONG_ISR_SPIN を増やして
  誤魔化さない**）。
- **手順5（`long_finished`）で落ちる** → quiesce が効いていない。
  `del_isr` の `while (p_isrcb->running != 0U)` と、`call_isr` の
  `running |= my_bit` / `&= ~my_bit` の位置（glock 保持中か）を疑う。
- **ハングする** → 待ちループの上限（`SPIN_LIMIT`）が効いていないか、
  `call_isr` の内側ループの終端判定（`&` の落とし）。

- [ ] **Step 8: ★★カーネル変異 negative control 1（quiesce）**

`kernel/interrupt.c` の `del_isr` の

```c
		while (p_isrcb->running != 0U) {
			release_glock();
			unlock_cpu();
			delay_for_interrupt();
			lock_cpu();
			acquire_glock();
		}
```

を**一時的にコメントアウト**して再ビルド・再実行する。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tdcre5 > /dev/null 2>&1
timeout -k 5 120 cmake --build build/musca_b1-2core-tdcre5 --target run \
  > /tmp/isr-t6-neg1.log 2>&1
grep -c 'TTSP_RESULT: PASS' /tmp/isr-t6-neg1.log     # 期待: 0
grep -n 'Check point 6 passed\|Assertion failed\|## ' /tmp/isr-t6-neg1.log | head
pgrep -a qemu
```
期待: **PASS 行が出ない**こと。`Check point 5 passed.` は出るが
**`Check point 6 passed.` は出ない**（手順5 の `check_assert(long_finished)` が失敗する）。
機序：quiesce が無いと `del_isr` は `long_isr` の空回しの途中で E_OK を返すので、
その時点で `long_finished` はまだ偽である。
★**もし変異を入れても PASS してしまったら、`LONG_ISR_SPIN` が短すぎる**
（del_isr が戻るまでに ISR が終わってしまっている）。`LONG_ISR_SPIN` を増やす。
調整した事実と最終値を記録する。
変異を**復元**し、Step 7 を再実行して `TTSP_RESULT: PASS` に戻ることまで確認する。

- [ ] **Step 9: ★★カーネル変異 negative control 2（走査の安定キー）**

`kernel/interrupt.c` の

```c
#define ISR_KEY_GT(pri1, seq1, pri2, seq2)								\
			(((pri1) > (pri2))											\
				|| (((pri1) == (pri2)) && ((seq1) > (seq2))))
```

を**一時的に** `(((pri1) > (pri2)))` へ退化させて（＝isrpri だけで判定する）
再ビルド・再実行する。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tdcre5 > /dev/null 2>&1
timeout -k 5 120 cmake --build build/musca_b1-2core-tdcre5 --target run \
  > /tmp/isr-t6-neg2.log 2>&1
grep -c 'TTSP_RESULT: PASS' /tmp/isr-t6-neg2.log     # 期待: 0
grep -n 'Check point 3 passed\|Check point 4 passed\|Assertion failed' /tmp/isr-t6-neg2.log | head
pgrep -a qemu
```
期待: **PASS 行が出ない**こと。`Check point 3 passed.` は出るが
**`Check point 4 passed.` は出ない**（手順3 のログが `"2ABC4"` ではなく
`"2A4"` になる＝同一 isrpri の 'B'/'C' を取りこぼす）。
★**これが Codex 指摘 #1 が現実の欠陥であることの実演であり、
本段階で最も重要な control である。省略しないこと。**
変異を**復元**し、`TTSP_RESULT: PASS` に戻ることまで確認する。

- [ ] **Step 10: ★カーネル変異 negative control 3（free-list 返却）**

`kernel/interrupt.c` の `del_isr` の
`queue_insert_prev(&free_isrcb, &(p_isrcb->isr_queue));` を**一時的にコメントアウト**する。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tdcre5 > /dev/null 2>&1
timeout -k 5 120 cmake --build build/musca_b1-2core-tdcre5 --target run \
  > /tmp/isr-t6-neg3.log 2>&1
grep -c 'TTSP_RESULT: PASS' /tmp/isr-t6-neg3.log     # 期待: 0
grep -n 'Check point 2 passed\|Check point 3 passed\|Assertion failed' /tmp/isr-t6-neg3.log | head
pgrep -a qemu
```
期待: **PASS 行が出ない**こと。`AID_ISR(4)` なので、手順2 で 3 個使い、
手順3 の `acre_isr` で 4 個目を取った後に**枯渇して E_NOID になり**
`check_assert(erid > ISR_S4)` が失敗する。
★**実際に失敗する手順は実測で記録すること。** 予測と一致しなくても、
**PASS が出ないこと + 失敗が「free-list 枯渇に起因する assert」であること**が
確認できればよい。
変異を**復元**し、`TTSP_RESULT: PASS` に戻ることまで確認する。

- [ ] **Step 11: ★★`test_int2` のインライン連鎖が保たれていることの再確認（zero-impact proof）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tint2 > /tmp/isr-t6-int2-build.log 2>&1; echo "rc=$?"
grep -c "call_isr" build/musca_b1-2core-tint2/generated/kernel_cfg.c   # 期待: 0
diff /tmp/isr-t2-inthdr-before.txt \
     <(sed -n '/^_kernel_inthdr_65612(void)$/,/^}$/p' \
       build/musca_b1-2core-tint2/generated/kernel_cfg.c); echo "inthdr diff rc=$?"
grep -n "ENA_DYNISR\|AID_ISR" test/test_int2.cfg      # 期待: 出力なし
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tint2 --target run > /tmp/isr-t6-int2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t6-int2.log ; pgrep -a qemu
```
期待: `call_isr` が **0 個**、inthdr の `diff rc=0`、`test_int2.cfg` に
`ENA_DYNISR`/`AID_ISR` が**無い**、`TTSP_RESULT: PASS`。
★**これが「既存構成への影響ゼロ」の headline proof である。**
`test_dcre5` と `test_int2` が同じカーネルで、片方はキュー方式・片方はインライン連鎖で
動いていることが、案B-2 ハイブリッドが成立している証拠になる。

- [ ] **Step 12: 非退行 — test_dcre1 / 2 / 3 / 4 の再実行**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for t in dcre1 dcre2 dcre3 dcre4; do
  d=build/musca_b1-2core-t$t
  [ -d $d ] || cmake --preset musca_b1-2core -B $d \
      -DFMP3_APPLNAME=test_$t -DFMP3_APPLDIR=test \
      -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/isr-t6-$t-conf.log 2>&1
  cmake --build $d > /tmp/isr-t6-$t-build.log 2>&1; echo "$t build rc=$?"
done
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre1 --target run > /tmp/isr-t6-d1.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t6-d1.log ; pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre2 --target run > /tmp/isr-t6-d2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t6-d2.log ; pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre3 --target run > /tmp/isr-t6-d3.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t6-d3.log
grep -c 'Check point' /tmp/isr-t6-d3.log       # 期待: 14（段階3a の実測値）
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre4 --target run > /tmp/isr-t6-d4.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t6-d4.log
grep -c 'Check point' /tmp/isr-t6-d4.log       # 期待: 段階3b Task 6 の実測値
pgrep -a qemu
```
期待: 4 本とも `TTSP_RESULT: PASS` が実在。
★`test_dcre4` の `Check point` 行数は**段階3b Task 6 の実測値**を使う
（計画作成時点で段階3b Task 6 は未完了だったため、この計画に数値を書けない。
`.superpowers/sdd/2026-08-04-fmp3-dcre-stage3b-dtq-pdq-mpf/progress.md` から拾うこと）。

- [ ] **Step 13: 台帳とコミット**

`DIVERGENCE_MAP.md` に `test/test_dcre5.c` `test/test_dcre5.cfg` `test/test_dcre5.h`
（種別 `add (dcre-port)`、理由「動的 ISR 生成（acre_isr/del_isr）の QEMU 回帰テスト。
musca_b1-2core・8 手順・変異 control 3 件（quiesce／走査の安定キー／free-list 返却）。
`INTNO1`（PRC1）に静的 ISR 2 本 + `ENA_DYNISR`、`INTNO2`（PRC2）は動的専用。
走査中の del/acre は PRC2 のタスクと PRC1 の ISR のハンドシェイクで決定的に組んだ。
★musca_b1 は intno にプロセッサ ID を符号化するため『同一キューを 2 コアが同時走査』は
実証できない（訂正H）。実証できたのは『PRC2 で実行中の ISR を PRC1 から del する』
＝quiesce の本質までである」）と、
`test/MANIFEST` `test/testexec.rb` の既存行への追記（`test_dcre5` 追加）を行って：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git status --short          # test/test_int2.cfg・kernel/interrupt.c の変異が残っていないこと
git diff --stat kernel/interrupt.c
git add -A && git commit -m "test(dcre): 動的ISR生成の回帰テスト test_dcre5 を追加（2コアQEMU・走査中のdel/acreとquiesceを決定的に実証・変異control3件）"
```

---
### Task 7: 最終回帰と台帳整理

**推奨モデル:** 中位（sonnet）

**Files:**
- Modify: `DIVERGENCE_MAP.md`（掃除）・`.superpowers/sdd/progress.md`（フラット台帳へ完了記録）

**このタスクではコードを直さない。** 欠陥を見つけたら**記録して報告**し、修正は別コミットに切る。

- [ ] **Step 1: 全9プリセット configure+build**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --preset $p > /tmp/isr-t7-conf-$p.log 2>&1; c=$?
  cmake --build build/$p > /tmp/isr-t7-build-$p.log 2>&1; b=$?
  echo "$p conf=$c build=$b"
done
```
期待: `polarfire_soc_kit`（実機プリセット）のみ SoftConsole ツールチェーン不在
（`fatal error: cannot read spec file 'nano.specs'`）で**既知の環境ギャップとして fail**。
それ**以外の 8 構成が exit=0**。

- [ ] **Step 2: ★★zero-impact の最終確認（本段階の headline item）**

**これが本段階で最も重要な検査である。** 案B-2 ハイブリッドの存在理由そのものを固定する。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
# opt-in を持たない全構成の inthdr が「call_isr を含まない」こと
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  n=$(grep -c "call_isr" build/$p/generated/kernel_cfg.c)
  echo "$p call_isr=$n"     # 期待: 全て 0
done
grep -c "call_isr" build/musca_b1-2core-tint2/generated/kernel_cfg.c   # 期待: 0
grep -c "call_isr" build/musca_b1-2core-tdcre1/generated/kernel_cfg.c  # 期待: 0
grep -c "call_isr" build/musca_b1-2core-tdcre4/generated/kernel_cfg.c  # 期待: 0
grep -c "call_isr" build/musca_b1-2core-tmix/generated/kernel_cfg.c    # 期待: 1
grep -c "call_isr" build/musca_b1-2core-tdcre5/generated/kernel_cfg.c  # 期待: 2
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
# インライン連鎖の本体がバイト不変であること（Task 2 Step 1 で保存した基準と比較）
diff /tmp/isr-t2-inthdr-before.txt \
     <(sed -n '/^_kernel_inthdr_65612(void)$/,/^}$/p' \
       build/musca_b1-2core-tint2/generated/kernel_cfg.c); echo "int2 inthdr diff rc=$?"
grep -c . /tmp/isr-t2-inthdr-before.txt      # 期待: 0 でないこと
diff -u /tmp/isr-base-generated/kernel_cfg.h build/musca_b1-2core/generated/kernel_cfg.h
echo "sample1 kernel_cfg.h diff rc=$?"
diff -u /tmp/isr-base-generated/kernel_cfg.c build/musca_b1-2core/generated/kernel_cfg.c \
     > /tmp/isr-t7-managed-diff.txt; wc -l /tmp/isr-t7-managed-diff.txt
```
期待:
- 8 構成 + `tint2` + `tdcre1` + `tdcre4` で **`call_isr` が 0 個**、
  `tmix` で **1 個**、`tdcre5` で **2 個**。
- `test_int2` の inthdr が**バイト一致**（`diff rc=0`）かつ**比較対象が空でない**。
- `kernel_cfg.h` が**差分ゼロ**。
- `kernel_cfg.c` の差分が **Task 2 Step 10 の許容リスト 12 項目と完全一致**
  （`/tmp/isr-t7-managed-diff.txt` を目で読んで確かめる。行数だけ見ない）。

★**基準ファイル（`/tmp/isr-base-generated`・`/tmp/isr-t2-inthdr-before.txt`）が
消えている場合**は、`git stash` で ISR 段階の変更を退避して再生成するか、
段階3b の完了コミット（Task 7 の記録から特定する）を別ディレクトリへ
`git worktree add` して基準を作り直す。**「比較元が無いので省略」は不可。**

- [ ] **Step 3: 全8構成 + 派生ビルドの `tools/cfg_equivalence.sh`**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  tools/cfg_equivalence.sh build/$p > /tmp/isr-t7-eq-$p.log 2>&1
  echo "$p eq=$?"
done
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for d in musca_b1-2core-tmix kria_arm64-tmix musca_b1-2core-tdcre5 \
         musca_b1-2core-tdcre4 musca_b1-2core-tdcre3 musca_b1-2core-tint2; do
  tools/cfg_equivalence.sh build/$d > /tmp/isr-t7-eq-$d.log 2>&1
  echo "$d eq=$?"
done
```
期待: 全件 0。**2 は不合格**（Constraint 13）。
- `-tmix`（32bit / 64bit）は **8 家族の AID が混在し、かつ opt-in intno と
  非 opt-in intno が同居する構成**、
- `-tdcre5` は **2 つの opt-in intno（PRC1/PRC2）を持つ全部入り構成**、
- `-tint2` は **opt-in を持たず 3 本のインライン連鎖を持つ構成**
なので、この 3 種が ISR 段階の cfg 変更を実構成で検査している本体である。

- [ ] **Step 4: QEMU 起動 7 構成（★プリセットごとに個別実行）**

段階1 Task 7 では `for` ループで全構成を 1 コマンドに詰めた結果、Bash ツールの
2 分タイムアウトに当たり **qemu が孤児化した**。**1 プリセット 1 コマンド**で実行し、
毎回 `pgrep` で残存を確認する。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/polarfire_soc_kit-qemu --target run > /tmp/isr-t7-run-polarfire.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/isr-t7-run-polarfire.log   # 期待: 4
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/musca_b1 --target run > /tmp/isr-t7-run-musca1.log 2>&1
grep -c 'Processor 1 start\.' /tmp/isr-t7-run-musca1.log          # 期待: 1
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/isr-t7-run-musca2.log 2>&1
grep -c 'Processor [12] start\.' /tmp/isr-t7-run-musca2.log       # 期待: 2
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_arm64-1core --target run > /tmp/isr-t7-run-arm64-1.log 2>&1
grep -c 'Processor 1 start\.' /tmp/isr-t7-run-arm64-1.log         # 期待: 1
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_arm64 --target run > /tmp/isr-t7-run-arm64-4.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/isr-t7-run-arm64-4.log     # 期待: 4
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_r5 --target run > /tmp/isr-t7-run-r5-1.log 2>&1
grep -c 'Processor 1 start\.' /tmp/isr-t7-run-r5-1.log            # 期待: 1
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_r5-2core --target run > /tmp/isr-t7-run-r5-2.log 2>&1
grep -c 'Processor [12] start\.' /tmp/isr-t7-run-r5-2.log         # 期待: 2
pgrep -a qemu
```
（`rp2350_pico2` は QEMU にマシンモデルが無く `run` ターゲット自体が無い＝設計どおり。
`kria_r5-2core` は既知どおり rc=124 になるが、**両 Processor start 行が出ていれば合格**
＝rc 単独で判定しない（Constraint 11）。）
**各コマンドの後に `pgrep -a qemu` が何も出さないこと。** 出たら
`pkill -f qemu-system` で掃除し、その事実を記録する。
★段階3a Task 8 では直後に一瞬 `<defunct>` が見えることがあった（reap-lag）。
3 秒後に再確認して消えていれば孤児化ではない。**そう判断した根拠を記録する。**
★**すべての構成で `initialize_isr` が起動時に走る**（訂正D）。起動 7/7 は
恒常出力が壊れていないことの検査でもある。

- [ ] **Step 5: 機能テスト 7 本の再実行（QEMU 6 本 + build のみ 1 本）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 120 cmake --build build/musca_b1-2core-tdcre5 --target run > /tmp/isr-t7-tdcre5.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t7-tdcre5.log
grep -c 'Check point' /tmp/isr-t7-tdcre5.log       # 期待: Task 6 Step 7 の実測値（見込み 12）
grep 'Check point 2-1 passed\|Check point 2-2 passed' /tmp/isr-t7-tdcre5.log
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre4 --target run > /tmp/isr-t7-tdcre4.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t7-tdcre4.log
grep -c 'Check point' /tmp/isr-t7-tdcre4.log       # 期待: 段階3b Task 6 の実測値
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre3 --target run > /tmp/isr-t7-tdcre3.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t7-tdcre3.log
grep -c 'Check point' /tmp/isr-t7-tdcre3.log       # 期待: 14
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre2 --target run > /tmp/isr-t7-tdcre2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t7-tdcre2.log ; pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre1 --target run > /tmp/isr-t7-tdcre1.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t7-tdcre1.log ; pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tint2 --target run > /tmp/isr-t7-tint2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/isr-t7-tint2.log ; pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tmix > /tmp/isr-t7-tmix-build.log 2>&1; echo "tmix build rc=$?"
```
期待: QEMU 6 本とも `TTSP_RESULT: PASS` が実在。
`test_dcre_mix` は自身の DIVERGENCE_MAP 記載どおり **build + equivalence のみ**。
★ビルドディレクトリが無いものは Task 6 Step 12 のコマンドで作る。

- [ ] **Step 6: エラー経路回帰マトリクス（既存 27 件 + ISR 段階 10 件 = 37 件）**

★**`run.sh` は 4 引数形**。`#include "test_int2.h"` を含む cfg は**第4引数が必須**
（付けないと rc=2）。**本計画では全件に引数を明記する。**

まず実在するファイルと突き合わせる（★下の列と食い違ったら、実在するものに合わせ、
**合わせた事実を記録する**）：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
ls tools/cfg_error_tests/*.cfg | sort | tee /tmp/isr-t7-cfgs.txt
grep -c . /tmp/isr-t7-cfgs.txt      # 期待: 37（既存27 + ISR段階10）
grep -n "isr_syntax_probe" /tmp/isr-t7-cfgs.txt   # 期待: 出力なし（Task 2 Step 3 の探り用）
```

**既存 27 件**（段階1/2/3a/3b 分。段階3b Task 7 で全数が確定している）：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
X="-I$PWD/test"
M=build/musca_b1-2core
$R build/polarfire_soc_kit-qemu $T/e_par_creisr_intno_keyerror.cfg   E_PAR;         echo "01:$?"
$R build/polarfire_soc_kit-qemu $T/e_rsatr_inhno_affinity.cfg        E_RSATR;       echo "02:$?"
$R $M                           $T/musca_b1_e_rsatr_intno_affinity.cfg E_RSATR;     echo "03:$?"
$R build/kria_r5-2core          $T/kria_r5_e_rsatr_intno_affinity.cfg  E_RSATR;     echo "04:$?"
$R build/rp2350_pico2           $T/rp2350_e_rsatr_intno_affinity.cfg   E_PAR;       echo "05:$?"
$R $M $T/musca_b1_clsid_warning.cfg  CLS_ALL_PRC2  "-DOMIT_MULTIPRC_INTERRUPT";     echo "06:$?"
```
★05 は**期待 ercd が E_PAR**（ファイル名は `e_rsatr` だが `build/rp2350_pico2` は
既定 1 コア構成のため）。★06 は**期待文字列が ercd ではなく `CLS_ALL_PRC2`**。
いずれも段階3a Task 8 で実測確認済み。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
X="-I$PWD/test"
M=build/musca_b1-2core
$R $M $T/dcre_aid_in_class.cfg          E_RSATR "$X"; echo "07:$?"
$R $M $T/dcre_mpk_in_class.cfg          E_RSATR "$X"; echo "08:$?"
$R $M $T/dcre_mpk_zero.cfg              E_PAR   "$X"; echo "09:$?"
$R $M $T/dcre_mpk_double.cfg            E_OBJ   "$X"; echo "10:$?"
$R $M $T/dcre_mpk_misaligned.cfg        E_PAR   "$X"; echo "11:$?"
$R $M $T/dcre_aid_tsk_no_static.cfg     E_OBJ   "$X"; echo "12:$?"
$R $M $T/dcre_aid_cyc_in_class.cfg      E_RSATR "$X"; echo "13:$?"
$R $M $T/dcre_aid_alm_in_class.cfg      E_RSATR "$X"; echo "14:$?"
$R $M $T/dcre_aid_cyc_no_static.cfg     E_OBJ   "$X"; echo "15:$?"
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
X="-I$PWD/test"
M=build/musca_b1-2core
$R $M $T/dcre_aid_sem_in_class.cfg      E_RSATR "$X"; echo "16:$?"
$R $M $T/dcre_aid_flg_in_class.cfg      E_RSATR "$X"; echo "17:$?"
$R $M $T/dcre_aid_mtx_in_class.cfg      E_RSATR "$X"; echo "18:$?"
$R $M $T/dcre_aid_sem_no_static.cfg     E_OBJ   "$X"; echo "19:$?"
$R $M $T/dcre_aid_flg_no_static.cfg     E_OBJ   "$X"; echo "20:$?"
$R $M $T/dcre_aid_mtx_no_static.cfg     E_OBJ   "$X"; echo "21:$?"
$R $M $T/dcre_aid_dtq_in_class.cfg      E_RSATR "$X"; echo "22:$?"
$R $M $T/dcre_aid_pdq_in_class.cfg      E_RSATR "$X"; echo "23:$?"
$R $M $T/dcre_aid_mpf_in_class.cfg      E_RSATR "$X"; echo "24:$?"
$R $M $T/dcre_aid_dtq_no_static.cfg     E_OBJ   "$X"; echo "25:$?"
$R $M $T/dcre_aid_pdq_no_static.cfg     E_OBJ   "$X"; echo "26:$?"
$R $M $T/dcre_aid_mpf_no_static.cfg     E_OBJ   "$X"; echo "27:$?"
```

**ISR 段階の新規 10 件**：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
X="-I$PWD/test"
M=build/musca_b1-2core
$R $M $T/dcre_aid_isr_in_class.cfg       E_RSATR "$X"; echo "28:$?"
$R $M $T/dcre_aid_isr_no_static.cfg      E_OBJ   "$X"; echo "29:$?"
$R $M $T/dcre_aid_isr_no_dynisr.cfg      E_OBJ   "$X"; echo "30:$?"
$R $M $T/dcre_dynisr_out_of_class.cfg    E_RSATR "$X"; echo "31:$?"
$R $M $T/dcre_dynisr_class_mismatch.cfg  E_RSATR "$X"; echo "32:$?"
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
X="-I$PWD/test"
M=build/musca_b1-2core
$R $M $T/dcre_dynisr_no_cfgint.cfg       E_OBJ   "$X"; echo "33:$?"
$R $M $T/dcre_dynisr_definh_conflict.cfg E_OBJ   "$X"; echo "34:$?"
$R $M $T/dcre_dynisr_no_static.cfg       E_OBJ   "$X"; echo "35:$?"
$R $M $T/dcre_dynisr_bad_intno.cfg       E_PAR   "$X"; echo "36:$?"
$R $M $T/dcre_dynisr_duplicated.cfg      E_OBJ   "$X"; echo "37:$?"
```

期待: **37 件すべて 0**（両エンジンが同じ ercd／文言を同じように検出）。
rc=2 は**前提未充足であり合格ではない**。
★`ls` の結果に**上の 37 件に無いファイル**が見つかった場合は、
「回帰列に入っていなかったファイル」として**実行して結果を記録する**
（対象 builddir・期待 ercd は cfg 冒頭のコメントから読む）。**列に無いから無視、はしない。**
★**`dcre_aid_alm_no_static.cfg` は存在しない**（段階3a 最終レビュー Minor の指摘。
`AID_ALM` だけ no-static 版が欠けている）。ISR 段階でも**作らない**（スコープ外）が、
**欠けている事実を progress.md に再掲する**。

- [ ] **Step 7: `KERNEL_FCSRCS` 突き合わせ**（AGENTS.md §4。22 個のまま不変のはず）

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
diff <(awk '/^KERNEL_FCSRCS/{f=1} f{print} f&&!/\\$/{exit}' kernel/Makefile.kernel \
       | grep -oE '[a-z_]+\.c' | sort -u) \
     <(grep -oE 'kernel/[a-z_]+\.c' CMakeLists.txt | sed 's#kernel/##' | sort -u)
echo "diff rc=$?"
```
期待: rc=0（差分なし）。ISR 段階は既存 `.c` にしか手を入れていないので変化しないはず。

- [ ] **Step 8: `DIVERGENCE_MAP.md` の完全性監査**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git diff main...HEAD --name-only > /tmp/isr-t7-changed.txt
wc -l /tmp/isr-t7-changed.txt
while read f; do
  case "$f" in
    docs/*|tools/*|cmake/*|CMakeLists.txt|CMakePresets.json|cfg_py/*|.superpowers/*|kernel/*.py) continue;;
  esac
  grep -q -- "$f" DIVERGENCE_MAP.md || echo "MISSING: $f"
done < /tmp/isr-t7-changed.txt
```
期待: `MISSING:` が**1 行も出ない**。
（除外パターンは段階2 Task 8 の裁定に従う＝`kernel/*.py` は Python cfg エンジンの派生
テンプレートで `DIVERGENCE_MAP.md:17` の「kernel/*.py（15個）」枠に記録済み。
`kernel/*.trb` は **pristine なので除外しない**。）

ISR 段階で触った pristine は次のとおり（段階1/2/3a/3b 分は既に記録済み）：
`kernel/kernel_api.def`、`kernel/interrupt.trb`、`include/kernel.h`、
`kernel/kernel_impl.h`、`kernel/check.h`、`kernel/interrupt.h`、`kernel/interrupt.c`、
`kernel/allfunc.h`、`kernel/Makefile.kernel`、`kernel/kernel_rename.def`＋再生成 2、
`test/test_dcre5.*`、`test/test_dcre_mix.{c,cfg,h}`、`test/MANIFEST`、`test/testexec.rb`。
漏れが（あってはならないが）見つかったら**理由込みで**追記する。

あわせて次を反映する：
- **上流報告候補は 4 件のまま**である。★`del_isr` は dcre も `CHECK_ID`（E_ID）なので、
  候補 d（`del_flg`/`del_dtq` の `CHECK_PAR` 非一貫）は**拡張しない**。
  この「拡張しなかった」判断を明示的に書く（3 件目を探して無かった、という記録）。
- **新しい観察（上流報告候補にはしない）**：dcre `interrupt.trb:338-346` の
  `isrorder_table` 生成には `TNUM_SISRID == 0` のガードが無く、静的 ISR が 0 個の
  構成では `const ID _kernel_isrorder_table[TNUM_SISRID] = { };` ＝ゼロ長配列と
  空初期化子（GNU 拡張）を出力する。FMP3 は他の表と同様に `TOPPERS_EMPTY_LABEL` で
  ガードした。**dcre 側の可搬性の問題であって現行バグではない**（GCC では通る）ので
  報告候補には上げず、観察として記録するに留める。
- **未 hardening の記録**：`acre_isr` は `intno` の範囲検査を持たない（訂正A）。
  適格 intno 表の二分探索が唯一の検証であり、これは**十分**である（表は cfg が
  生成した閉じた集合なので、範囲外の値が表に載ることはない）。ただし
  **`E_PAR` と `E_OBJ` の区別が dcre と異なる**ため、dcre 向けに書かれた
  アプリケーションの移植時に ercd の差が見える。この非互換を明記する。

- [ ] **Step 9: `.superpowers/sdd/progress.md`（フラット台帳）へ ISR 段階完了を記録し、コミット**

記録に含めること（**推測と事実を分ける**）：
- Task 1 の実装前確認 8 項目の結論と、それに基づく spec 訂正 10 件（A〜J）。
  **訂正D／訂正H は「現物の性質の記録」であって現物の誤りではない**ことを明示する。
  **訂正B は spec の設計上の欠陥の修正である**（二重 `del_isr` の競合）ことを明示する。
- 【事実】**本段階は移植ではなく新規サブシステムの構築であった**：FMP3 には ISR の
  ランタイムオブジェクトが存在せず、`ISRQCB`/`ISRINIB`/`ISRCB`/`ISR_ENTRY` の
  4 型と `call_isr` のキュー走査を新設した。段階1〜3b の 7 家族は
  「既存オブジェクトに動的スロットを足す」作業だったが、ISR だけは違った。
- 【事実】**dcre からの意図的な逸脱の一覧**：
  (1) キューを作るのは `ENA_DYNISR` された intno だけ（dcre は全 intno。案B-2 の核心）、
  (2) `ISRQCB` の新設（dcre の `QUEUE isr_queue_table[]` に isrseq を持たせるため）、
  (3) `ISRCB` に `isrseq` と `running` を追加（Codex 指摘 #1/#2 への対応）、
  (4) `call_isr` の全面書き直し（走査の再決定・ロック規約）、
  (5) `del_isr` の quiesce 新設と `TA_NOEXS` の前倒し、
  (6) `acre_isr` から `VALID_INTNO_CREISR` を削除（Codex 指摘 #3 への対応）、
  (7) `ISRID` を 2 レンジの INIB ポインタ差分式へ（FMP3 の CB がポインタ表のため）、
  (8) `isrorder_table` を isrid 昇順へ（opt-in の有無で呼出し順が変わらないようにするため）、
  (9) `isrorder_table` の `TOPPERS_EMPTY_LABEL` ガード追加。
- 【事実】**Codex 外部レビューの 3 指摘はすべて実装で解決し、うち 2 件は変異 control で
  実演した**：
  #1（走査再開位置の安定キー）→ `ISR_KEY_GT` を isrpri だけに退化させると
  `test_dcre5` の手順3 が倒れる（Task 6 Step 9 で実演済み）。
  #2（del 後の寿命意味論）→ quiesce ループを外すと手順5 が倒れる（Task 6 Step 8）。
  #3（intno 検証のコア非依存化）→ `search_isr_queue` の二分探索のみにした
  （**実行時の control は無い**。cfg の適格 intno 表が閉じた集合であることが根拠）。
- 【事実】**既存構成への影響**：`_kernel_inthdr_<intno>` の本体は**バイト不変**
  （`test_int2` の 3 本連鎖で実証）。一方、`AID_ISR` を `kernel_api.def` に登録した
  時点で ISRINIB 表・ISRCB 実体・`p_isrcb_table`・`isrorder_table`・`tmax_isrid`/
  `tmax_sisrid`/`TNUM_SISRID`・`initialize_isr` の起動時呼出しが**恒常的に加わる**
  （訂正D）。実測増分は text=<実測> / data=<実測> / bss=<実測> バイト
  （`build/musca_b1-2core`）。**この受容は設計判断であり、数値で記録した。**
- 【事実】**musca_b1 では実証できなかったこと**（訂正H）：
  同一キューを 2 コアが同時に走査する構成は musca_b1 では作れない
  （intno にプロセッサ ID が符号化されるため）。したがって
  「2 コア同時走査時の `running` ビットマップの独立性」と
  「同一 ISR の 2 コア同時実行」は**設計論証とコード検査でしか裏付けていない**。
  PLIC/GIC のグローバル割込みを持つターゲット（polarfire_soc・zynq 系）では到達可能で
  あり、そこでの実証は**引き継ぎ課題**である。
- 【事実】E_NOEXS 挿入は **0 関数**である（ISR には `ref_isr` 等の状態参照 API が
  dcre にも無いため）。段階1〜3b と違い、既存サービスコールへの字下げ改造は発生しなかった。
- 【推測含む・引き継ぎ課題】
  (a) quiesce の待ち時間は ISR 本体の実行時間で有界だが、**ISR が長い場合に
      `del_isr` を呼んだタスクが長く待つ**。TOPPERS の ISR 短時間規約に依存しており、
      規約違反の ISR に対する保護は無い。
  (b) `call_isr` の走査は 1 要素進めるごとにキューを先頭から見るので **O(n^2)**
      である（dcre は O(n)）。opt-in した intno に多数の ISR を登録すると
      割込みレイテンシが伸びる。実測はしていない。
  (c) `isrseq` の u32 ラップは「キューが空にならないまま 2^32 回 enqueue」でのみ
      起こる。**到達可能性の実証も対策も行っていない**（実用上到達不能という
      spec の判断に依拠）。
  (d) 同一 intno の割込みハンドラが同一コアで多重に走らないことに依存して
      `running` をビットマップにしている。**この前提の根拠は Task 4 Step 1 で
      <確認結果>**（確認できていれば file:line、できていなければ「未確認」と書く）。
- 【回帰の実測値】builds 8/9（polarfire は既知の環境ギャップ）／equivalence 14/14／
  QEMU 7/7 孤児なし／機能テスト 6/6 PASS + mix build-only（計 7 本）／
  エラー行列 37/37／FCSRCS 差分 0／台帳監査 MISSING=0／
  zero-impact 検査：`call_isr` が非 opt-in 構成 11 種で 0 個・`tmix` で 1 個・
  `tdcre5` で 2 個、`test_int2` の inthdr がバイト一致。
  `test_dcre5` の `Check point` 行数は **<実測>**、`test_dcre3` は **14**、
  `test_dcre4` は **<段階3b の実測値>**。
- 【dcre 標準の動的生成 API の完成】段階1（tsk）→2（cyc/alm）→3a（sem/flg/mtx）→
  3b（dtq/pdq/mpf）→ISR で、**dcre 標準の動的生成オブジェクトが全て揃った**。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add -A && git commit -m "chore(dcre): 動的ISR生成の最終回帰と台帳整理（既存構成のinthdrバイト不変を実証）"
```

---

## Self-Review 済み事項（計画作成時の検証記録）

**spec 要件 → Task 対応:**

| spec | 内容 | Task |
|---|---|---|
| GC 1 | ブランチ・台帳 | 全 Task（各 Task の最終 Step） |
| GC 2 | ★既存構成への影響ゼロ | **T2 Step 6/10/11/12、T6 Step 11、T7 Step 2**（4 段階で検査） |
| GC 3 | dcre 標準 API のみ・コア引数を追加しない | T2 Step 5（★dcre とのバイト diff）、T5 Step 1 の禁止注記 |
| GC 4 | F-1 検証（両エンジン・exit 0 のみ合格） | T2 Step 9/12/14、T3-T6 の各ビルド Step、T7 Step 3 |
| GC 5 | 段階1〜3b の規約踏襲（FIFO・訂正C） | T3 Step 4（FIFO）、T5 Step 2（★訂正C は**適用対象外**と明記） |
| GC 6 | rc=124 単独禁止・パイプ判定禁止・exit2 不合格・QEMU 個別＋pgrep | T6 Step 7、T7 Step 4/6（全件に引数を明記） |
| §1.1 | `AID_ISR` + `ENA_DYNISR`（案B-2） | T2 Step 2（`.def` 2 行）・Step 7/8（両エンジン） |
| §1.2 | `acre_isr`/`del_isr`/`T_CISR` | T2 Step 5、T5 Step 1/2 |
| §1.3 | エラー（★訂正A で範囲検査を廃止） | T5 Step 1、T6 Step 4 手順6 |
| §2 | cfg 層（切替・キュー表・isrorder・AID プール・エラー回帰） | T2（全体） |
| §3 | ISRINIB/ISRCB/★ISRQCB・isrseq・running・free_isrcb・2 レンジ ISRID | T3（全体） |
| §4 | del_isr の寿命意味論（quiesce・★訂正B で順序修正） | T5 Step 2、T6 Step 4 手順5、T6 Step 8（変異 control） |
| §5 | call_isr の走査（★本段階の novel core） | T4（全体）、T6 Step 4 手順3/4、T6 Step 9（変異 control） |
| §6 | test_dcre5 の 6 シナリオ | T6 Step 4（手順1-8）+ Step 8/9/10（変異 control ×3）+ Step 11/12（非退行） |
| §7 | 案B-1 との比較（記録のみ・変更しない） | T1 Step 10（**触らない**と明記） |
| §8 | 実装前確認 8 項目 | T1 Step 1-9 |
| §9 | 統治（7 タスク・台帳・全構成回帰） | T1 Step 10（7 タスクに確定）、T7 |
| Codex #1 | 走査再開位置の安定キー | T3 Step 3（`ISR_KEY_GT`/`isrseq`）、T4 Step 2、**T6 Step 9（変異 control）** |
| Codex #2 | del 後の寿命意味論 | T5 Step 2（quiesce）、**T6 Step 8（変異 control）** |
| Codex #3 | intno 検証のコア非依存化 | T3 Step 3（`search_isr_queue`）、T5 Step 1（★訂正A）、T6 手順6 |

**現物確認済み（計画作成時に実ファイルで確認した事実）:**
- `TFN_ACRE_ISR (-204)`（`include/kernel_fncode.h:144`）/ `TFN_DEL_ISR (-220)`（`:156`）
  ＝**2 件とも既存**。
- FMP3 に**ランタイム ISR オブジェクトが無い**こと：`kernel/interrupt.h` は
  `INHINIB`（`:57-62`）と `INTINIB`（`:81-87`）と `initialize_interrupt`（`:104`）だけで、
  ISRINIB/ISRCB/ISR キューが 1 つも無い（全 106 行）。
- `kernel/interrupt.trb:411-477` / `kernel/interrupt.py:370-442` の inthdr 生成：
  prcid × intno の二重ループ、`isrParamsList` が空なら何もしない、
  `affinityPrcList` に prcid が無ければ `continue`、`DEF_INH` は prcid ごと、
  本体は `isr_flag[intnoVal]` で 1 回だけ、
  ソートは `sorted(cfgData["CRE_ISR"].items())`（isrid 昇順）を基底とする
  `isrpri` の安定ソート、ISR 間に
  `if (_kernel_sense_lock()) { _kernel_force_unlock_spin(p_my_pcb); _kernel_unlock_cpu(); }`。
- `kernel/interrupt.py` が `kernelCfgH` へ書くのは `:56-60` の**1 箇所だけ**であること。
- 生成物の実例（`build/musca_b1-2core-tint2/generated/kernel_cfg.c:237-260`）：
  `_kernel_inthdr_65612` が `PCB *p_my_pcb = get_my_pcb();` + 3 本の直接呼出し +
  2 回のロック復元。`INTHDR_ENTRY(65612, 65612, _kernel_inthdr_65612)` と
  `_kernel_inhinib_table` の該当行。`kernel_cfg.h` の `#define TNUM_ISRID 4`。
- dcre `interrupt.h:56-62`（ISRINIB）/`:67-70`（ISRCB・**2 フィールドのみ**）/
  `:75-78`（ISR_ENTRY）/`:126`（ISRID＝配列差分）。
- dcre `interrupt.c:169-170`（tnum_isr/tnum_sisr）/`:176-177`（INDEX_ISR/get_isrcb）/
  `:182-195`（enqueue_isr）/`:202`（free_isrcb）/`:207-231`（initialize_isr）/
  `:240-260`（call_isr）/`:267-293`（search_isr_queue）/`:303-352`（acre_isr）/
  `:361-392`（del_isr）。**dcre の del_isr に quiesce は無い**。
- dcre `interrupt.trb:263-294`（キュー表）/`:299-316`（inthdr = `call_isr` 1 行）/
  `:321-336`（IsrObject）/`:338-346`（isrorder_table・**ゼロ長ガード無し**）。
- dcre `include/kernel.h:322-328`（T_CISR）/`:481-482`（**dcre 自身が `ER_ID`**）。
- dcre `kernel_impl.h:127`（TMIN_ISRID）/`:155-157`（TARGET_ISRATR）、
  dcre `check.h:64`（VALID_ISRID）/`:73-74`（VALID_ISRPRI）/`:235-240`（CHECK_OBJ）。
  **FMP3 にはこの 5 つとも無い**（`TMIN_SPNID`＝`kernel_impl.h:194` /
  `TARGET_TSKATR`＝`:209-211` / `VALID_SPNID`＝`check.h:65` /
  `VALID_DPRI`＝段階3b が追加 / `CHECK_ILUSE`＝`check.h:305-311` が隣接行）。
- `TMIN_ISRPRI 1` / `TMAX_ISRPRI 16`（`include/kernel.h:654-655`）は**ある**。
  `ISR` 型（`:114`）・`INTNO` 型（`:105`）も**ある**。
- `kernel/kernel_sym.def:73` が `TARGET_ISRATR,,,defined(TARGET_ISRATR),0`、
  `:69` が `TARGET_TSKATR,,,defined(TARGET_TSKATR),0` で、
  `TARGET_TSKATR` は C 側にも定義がある（＝先例）。
- `signal_time`（`kernel/time_event.c:709-768`）が
  `PCB *p_my_pcb = get_my_pcb();` → `assert(sense_context(p_my_pcb)); assert(!sense_lock());`
  → `lock_cpu(); acquire_glock();` → … → `release_glock(); unlock_cpu();`。
- `call_cyclic`（`kernel/cyclic.c:518-548`）が `release_glock(); unlock_cpu();` →
  ハンドラ → `if (sense_lock()) { force_unlock_spin(p_my_pcb); } else { lock_cpu(); }`
  → `acquire_glock();`（**3 分岐**）。
- `force_unlock_spin`（`kernel/spin_lock.c:162-176`）が
  **スピンロックだけを解放し CPU ロックには触らない**こと。
- `wait_tmout`（`kernel/wait.c:109-131`）と `wait_tmout_ok`（`:138-160`）の末尾が
  **同じ 5 行**（`release_glock(); unlock_cpu(); delay_for_interrupt(); lock_cpu();
  acquire_glock();`）で、コメントが `/* ここで優先度の高い割込みを受け付ける． */`。
- `delay_for_interrupt()` が 4 アーキすべてに存在
  （`arch/arm_m_gcc/common/core_insn.h:192` /
  `arch/arm_gcc/common/core_kernel_impl.h:261` /
  `arch/arm64_gcc/common/core_kernel_impl.h:248` /
  `arch/riscv_gcc/common/core_kernel_impl.h:191`）し、
  `kernel/wait.c:129,157` と `kernel/spin_lock.c:215,276` が既に使っていること。
- `arch/arm_m_gcc/musca_b1/chip_kernel_impl.h:184-192` の `lock_native_spn` が
  「ロックを外して待ち、割込みを通し、取り直す」形で、そのコメントが
  `doc/porting.txt (6-21-3-2)` を引用していること。
- `get_my_prcidx()` が **0 始まり**で、`kernel/time_event.c:150` の
  `(1 << get_my_prcidx())` が既存の用例であること。
- `cfg_py/pass1.py:106-131`（`.def` の prefix/postfix。`*`＝KEYPAR・`?`＝OPTIONAL）と
  `cfg_py/pass2.py:380-395`（KEYPAR による登録キーと**重複時の E_OBJ**）。
- **`cfg_py/pass2.py:178-186`（Ruby は `cfg/pass2.rb:165-172`）が
  `apiDefinition` の全 API について `cfgData[api] = {}` を先に作る**こと
  → `kernel.py:142` の `has_aid` は `.def` に登録した時点で恒真（訂正D の根拠）。
- 汎用枠組み `kernel/kernel.py:109-277` / `kernel/kernel.trb:115-295` の
  `generate()` が出力するものの内訳と、`self.inibList` の既定
  （`{"ISRINIB": "aisrinib_table"}` になる）、
  `initializeFunctions.append(f"_kernel_initialize_{object_name}(p_my_pcb);")`（`:257-258`）、
  訂正E ガード（`:157-161`）、`if len(cfgData[api]) > 0:`（`:200`）。
  `KernelObject` の定義位置（`kernel.py:109` / `kernel.trb:115`）が
  `IncludeTrb("kernel/interrupt.py")`（`kernel.py:470` / `kernel.trb:493`）より
  **前**であること＝`IsrObject` を `interrupt.py`/`.trb` で定義できる。
- `kernel/kernel_api.def` の現在の 28 行（`CRE_ISR`＝`:13`、`CFG_INT`＝`:12`、
  末尾が `AID_MPF .nompf`＝`:28`）。
- `kernel/allfunc.h:279-287`（`/* interrupt.c */` 節）/
  `kernel/Makefile.kernel:122-123`（`interrupt =`）/
  `kernel/kernel_rename.def:140-141`（`# interrupt.c`）と `:146-` の `# kernel_cfg.c` 節
  （`p_almcb_table` の次が `tnum_def_inhno`）。**ISR 関連は 1 つも無い**
  （dcre `kernel_rename.def:114-116,176-184` には 12 個ある）。
- `target/musca_b1_gcc/target_kernel.trb:17-24`（`(prcid << 16) | intno`）と
  `target_class.trb:24-33`（2 コア時の 4 クラス）、
  `target/musca_b1_gcc/target_test.h:43-53`（`INTNO1`/`INTNO2` と空の clear マクロ、
  「`ras_int` は自コアの NVIC のみ」というコメント）。
- `arch/riscv_gcc/polarfire_soc/chip_kernel.trb:12-19` と
  `arch/arm_gcc/zynq7000/chip_kernel.trb:13-22` が**intno をコア間で共有する**こと。
- `tools/cfg_error_tests/musca_b1_e_rsatr_intno_affinity.cfg` が
  「affinity が 2 コアのクラスに `CFG_INT`」を E_RSATR で固定していること。
- 静的 `CRE_ISR` の所在：`sample/sample1.cfg`（6 個）・
  `target/polarfire_soc_kit_gcc/softconsole/sample1/sample1.cfg`（4 個）・
  `target/musca_b1_gcc/target_serial.cfg:11`（`ISR_SIO`）・
  各 `arch/*/*/chip_serial.cfg`（`ISR_SIO`）・`test/test_int2.cfg`（3 個）・
  `tools/cfg_error_tests/e_par_creisr_intno_keyerror.cfg`。
  ★**`serial.cfg` 経由の `CRE_ISR(ISR_SIO)` が `test_common1.cfg` を INCLUDE する
  全テストに入る**（no-static の回帰 cfg は段階3a の `dcre_aid_sem_no_static.cfg` と
  同じ回避が要る）。
- `tools/cfg_error_tests/` の実在 `.cfg` が **27 個**（段階3b Task 7 完了時点の想定値。
  ★段階3b Task 6/7 が未完了の時点で数えているので、**着手時に再度数える**）。
- `test/MANIFEST:44-52`（`test_dcre3.*`〜`test_dcre_mix.*`）と
  `test/testexec.rb:90-95`（`"dcre3"`/`"dcre4"`/`"dcremix"`）の登録形式。
- `syssvc/test_svc.c:110-190` の `check_point_prc`/`check_finish` の意味論。
- `test/test_int2.c` の ISR が `check_point`/`check_assert` を ISR 内で呼び、
  `isr1` が `iloc_cpu()` してから戻る（C-1 の前提を作る）こと。
- `include/queue.h:77,90,119,134,151` の `queue_initialize`/`queue_insert_prev`/
  `queue_delete`/`queue_delete_next`/`queue_empty`。
  ★`queue_delete` は**削除したエントリのリンクを作り直さない**（訂正B の根拠）。
- `arch/arm_m_gcc/common/core_stddef.h:64` の
  `TOPPERS_EMPTY_LABEL(type, var)` の定義（`var[0]`）。

**未検証（実装者が最初に当たること）:**
- **`ENA_DYNISR` をクラスの囲みの中に書けるか**（T2 Step 3。★ゲート G3。
  `.def` の文法上は書けるはずだが、`CLASS(...) { ENA_DYNISR(...); }` を
  実際にパースさせるまで確定ではない）。
- **`TARGET_ISRATR` を C 側に定義しても cfg 出力が変わらないか**（T2 Step 6）。
  変わるなら `kernel_sym.def` の評価文脈に `kernel_impl.h` が入っている。
- **`AID_ISR` の恒常出力による size 増分の実測値**（T2 Step 16 / T3 Step 9）。
  桁が違ったら設計の誤り。
- **`test_dcre5` の手順4（走査中の del/acre）が決定的に組めるか**（T6 Step 7）。
  `hs_isr_timeout` が立つなら PRC2 の TASK3 が想定どおり動いていない。
  **`LONG_ISR_SPIN` や `SPIN_LIMIT` をいじって誤魔化さず、まずどちらのコアで
  何が走るかを紙に書いて確かめる。**
- **`LONG_ISR_SPIN = 2000000U` が quiesce の変異 control の梃子として十分か**
  （T6 Step 8。足りなければ増やす — 減らさない）。
- **`test_dcre5` の `Check point` 行数が本当に 12 か**（T6 Step 7）。
  段階3a では見積りを 3 回続けて外している。**実測を正とし、計画の期待値を直して記録する。**
- **`ISR_SIO` が `kernel_cfg.h` に ID マクロとして実在するか**（T6 Step 4）。
  無ければ該当行を落とす。
- **`CHECK_FUNC_NONNULL` / `CHECK_FUNC_ALIGN` が musca_b1 で定義されているか**
  （T6 Step 1）。定義されていなければ `isr == NULL` の E_PAR はテストできない。
- **`EXINF` へ `char` を出し入れするキャストの書き方**（T6 Step 4）。
  警告が出ない形に直し、直した事実を記録する。
- **`dcre_dynisr_definh_conflict.cfg` で E_OBJ が何回出るか**（T2 Step 15 ケース 7）。
  `CRE_ISR` 自身も DEF_INH と競合するので 2 回出る可能性がある。
- **`dcre_aid_isr_no_static.cfg` / `dcre_dynisr_no_static.cfg` で実際に E_OBJ が出るか**
  （T2 Step 15）。出なければ `serial.cfg` の回避が効いていない。
- **`tools/cfg_error_tests/` の実在 `.cfg` が着手時点で 27 個か**（T7 Step 6）。
  段階3b Task 6/7 の完了状況で変わりうる。
- **`test_dcre4` の `Check point` 行数**（T6 Step 12 / T7 Step 5）。
  段階3b Task 6 の実測値を `.superpowers/sdd/2026-08-04-fmp3-dcre-stage3b-dtq-pdq-mpf/progress.md`
  から拾う。
- **「同一 intno の割込みハンドラが同一コアで多重に走らない」ことの根拠**（T4 Step 1）。
  `running` をビットマップにしてよい根拠である。見つからなければ「未確認」と記録し、
  コメントに断定を書かない。

---
