# FMP3 動的生成API 段階1（acre_tsk/del_tsk + DEF_MPK）実装計画

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `feature/dynamic-creation` ブランチ上で `acre_tsk`/`del_tsk` と `DEF_MPK` を、
cfg 両エンジン（Ruby オラクル + Python 製品）同時対応・QEMU 回帰テスト付きで動かす。

**Architecture:** ASP3 dcre の機構（cfg 予約スロットの free-list + RAM `atinib_table[]`）を
忠実移植し、FMP3 固有の3点（ジャイアントロック・TINIB の iprcid/affinity・
named-static TCB + const ポインタ表）を局所適応。cfg 出力は「恒常的に新形式」とし、
既存構成への影響は**管理された差分**（後述）として検査する。

**Tech Stack:** C（カーネル）、Python/Ruby（cfg テンプレート）、CMake、QEMU（musca_b1）。

## Global Constraints（spec から転記。実装中に再解釈しない）

1. ブランチ `feature/dynamic-creation`。**main へはマージしない。**
   pristine への改変はブランチ上でも `DIVERGENCE_MAP.md` に記録する（種別 `mod (dcre-port)`、上流報告欄 `-`）。
2. 段階1 = `acre_tsk`/`del_tsk` + `DEF_MPK` のみ。段階2（cyc/alm/isr）・段階3（sem 等）のタスクを含めない。
3. API 面は dcre 標準のみ：`acre_tsk`/`del_tsk`・`T_CTSK`・`AID_TSK`/`DEF_MPK`。**独自 API なし。`acre_*` にクラス引数なし。**
4. 動的生成タスクは **`iprcid = 1`（PRC1）、`affinity = (1U << TNUM_PRCID) - 1`（全プロセッサ）** をカーネルが固定で埋める。`del_tsk` は休止状態のみ。
5. 検証 = F-1：Ruby `.trb` にも同時移植し `tools/cfg_equivalence.sh`（exit 0=一致/1=不一致/**2=前提未充足であり合格ではない**）を主検査に維持。
6. free-list + RAM `atinib_table[]`。CB はヒープ確保しない。スタックは呼び出し側供給か `DEF_MPK` プール。
7. 汎用層 `CMakeLists.txt`・`fmp3_core.cmake`・`cfg_py/pass1.py`・`cfg_py/pass2.py` は**変更しない**。
8. `rc=124` 単独を成功判定に使わない（期待出力の実在を grep で確認する）。

## ★spec からの訂正2件（Task 1 で spec に反映してから実装に入ること）

**訂正A：「AID 無し構成の生成物はバイト単位で不変」（spec §3.3/§6.1）は成立しない。**
根拠：本リポジトリのカーネルは `ALLFUNC` で全関数コンパイルされる（`CMakeLists.txt:562`）ため、
`initialize_task`（task.c）・`del_tsk` が参照する `_kernel_tmax_stskid`・`_kernel_atinib_table`・
`_kernel_mpksz`・`_kernel_mpk` は **AID/DEF_MPK の有無に関わらずリンクに必要**。
dcre 自身も無条件に新形式を出力する（dcre DIFF:2129 以降の kernel.trb 差分は
`TNUM_#{OBJ}ID` の定義や inib サイズトークンを無条件に変更している）。
→ 代替検査 = **管理された差分**：AID 無し構成の生成物の変更前後 diff が
「Task 2 Step 7 の許容リスト」**と完全一致**すること（それ以外の差分は不合格）。
Ruby-vs-Python の等価性検査（真のゲート）は全構成 exit=0 を維持する。

**訂正B：ARM-M は `USE_TSKINICTXB` を定義する（spec §2.2 が未考慮）。**
`arch/arm_m_gcc/common/core_kernel_impl.h:113` が `USE_TSKINICTXB` を定義し、TINIB は
`stksz`/`stk` でなく `TSKINICTXB { uint32_t *stk_top; uint32_t *stk_bottom; }`（同:115-118）を持つ。
主検証ターゲット musca_b1 がまさに ARM-M。→ `acre_tsk`/`del_tsk` は dcre と同じ
`#ifdef USE_TSKINICTXB` 分岐を持ち、ARM-M 側に `init_tskinictxb()` と
`tskinictxb_memalloc_ptr()`（内部ヘルパ、Task 4）を追加する。
ASP3 3.7 で USE_TSKINICTXB を使う arch は posix のみ（`posix_kernel_impl.h:204`）で
スタック実体を持たないため、**この組み合わせに上流前例は無い**。

---

## 変更ファイル一覧（全体像）

| 層 | ファイル | 種別 |
|---|---|---|
| spec | `docs/superpowers/specs/2026-08-03-fmp3-dynamic-creation-design.md` | 派生・修正 |
| cfg 定義 | `kernel/kernel_api.def` | **pristine・台帳** |
| cfg Python | `kernel/kernel.py` `kernel/task.py` `kernel/kernel_check.py` | 派生 |
| cfg Ruby | `kernel/kernel.trb` `kernel/task.trb` `kernel/kernel_check.trb` | **pristine・台帳** |
| cfg 検査 | `kernel/kernel_sym.def` | **pristine・台帳** |
| カーネル | `kernel/kernel_impl.h` `kernel/check.h` `kernel/startup.c` `kernel/task.h` `kernel/task.c` `kernel/task_manage.c` `kernel/task_refer.c` `kernel/task_sync.c` `kernel/task_term.c` `kernel/allfunc.h` `kernel/Makefile.kernel` | **pristine・台帳** |
| rename | `kernel/kernel_rename.def` →（再生成）`kernel/kernel_rename.h` `kernel/kernel_unrename.h` | **pristine・台帳** |
| API | `include/kernel.h` | **pristine・台帳** |
| arch | `arch/arm_m_gcc/common/core_kernel_impl.h` | **pristine・台帳** |
| テスト | `test/test_dcre1.{c,cfg,h}` `test/MANIFEST` `test/testexec.rb` | **pristine・台帳**（新規追加も記録） |
| エラー回帰 | `tools/cfg_error_tests/dcre_aid_in_class.cfg` | 派生 |

新規 `.c` ファイルは**作らない**（`KERNEL_FCSRCS` 不変。acre/del は既存 `task_manage.c` に入る）。

---

### Task 1: spec の訂正（管理された差分・TSKINICTXB）

**推奨モデル:** 最安価（機械的な文書修正）

**Files:**
- Modify: `docs/superpowers/specs/2026-08-03-fmp3-dynamic-creation-design.md` §2.2, §3.3, §6.1

- [ ] **Step 1:** §3.3 の「既存構成の生成物がバイト単位で不変であることを…確認する」と
  §6.1 の「AID 無し構成の生成物はバイト不変であること」を、本計画冒頭の**訂正A**の文面
  （ALLFUNC 根拠・管理された差分への置換）で書き換える。§6.3 の negative control の記述も
  「バイト不変」→「許容リスト一致」に直す。
- [ ] **Step 2:** §2.2 の手順5-6 の間に**訂正B**（USE_TSKINICTXB 分岐と ARM-M 側ヘルパ追加）を挿入する。
- [ ] **Step 3: コミット**

```bash
git add docs/superpowers/specs/2026-08-03-fmp3-dynamic-creation-design.md
git commit -m "docs(spec): dcre段階1の実装前訂正2件（管理された差分・USE_TSKINICTXB）"
```

---

### Task 2: cfg 両エンジン — AID_TSK/DEF_MPK と恒常出力基盤

**推奨モデル:** 中位（sonnet）。テンプレート2言語の対応関係を保つ判断が要る。

**Files:**
- Modify: `kernel/kernel_api.def`（2行追加・pristine）
- Modify: `kernel/kernel.py:109-208`（KernelObject.generate）と同 `kernel.py` 末尾（mpk 出力）
- Modify: `kernel/task.py:113`（torder）
- Modify: `kernel/kernel.trb:115-218`（KernelObject#generate）と同 `.trb` 末尾（mpk 出力・pristine）
- Modify: `kernel/task.trb:128`（torder・pristine）
- Modify: `kernel/kernel_check.py` / `kernel/kernel_check.trb`（mpk 検査・後者 pristine）
- Modify: `kernel/kernel_sym.def`（3行追加・pristine）
- Create: `tools/cfg_error_tests/dcre_aid_in_class.cfg`
- Modify: `DIVERGENCE_MAP.md`

**Interfaces（後続 Task が依存する生成物）:**
- kernel_cfg.c に恒常出力: `#define TNUM_STSKID <n>`、`const ID _kernel_tmax_stskid`、
  `TINIB _kernel_atinib_table[N]`（N=0 時 `TOPPERS_EMPTY_LABEL(TINIB, _kernel_atinib_table);`）、
  予約 TCB `static TCB _kernel_atcb_<i>;`（i=1..N）と `_kernel_p_tcb_table` 末尾への追加、
  `const size_t _kernel_mpksz` / `MB_T *const _kernel_mpk`
- `TNUM_TSKID` は総数（静的+AID）へ意味変更、`_kernel_torder_table[TNUM_STSKID]`

- [ ] **Step 1: 基準生成物の保存（管理された差分の比較元）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --preset musca_b1-2core && cmake --build build/musca_b1-2core
cp -r build/musca_b1-2core/generated /tmp/dcre-base-generated
```

- [ ] **Step 2: `kernel/kernel_api.def` に dcre と同一の2行を追加**（ファイル末尾）

```
AID_TSK .notsk
DEF_MPK { .mpksz &mpk? }
```

- [ ] **Step 3: `kernel/kernel.py` の KernelObject を拡張**（dcre kernel.trb 差分の忠実な Python 化）

`__init__`（`kernel.py:110-120`）へ追加：
```python
        self.noobj = "no" + obj
        self.aidapi = "AID_" + obj.upper()
        self.inibList = {f"{self.OBJ_S}INIB": f"a{self.obj_s}inib_table"}
```

`generate()` 冒頭（`kernel.py:122-125`、TNUM 出力の直前）へ追加：
```python
        # AID_xxx の処理（クラス外専用。ENA_SPR と同じ規約: kernel/task.py:141-142）
        numAutoObjid = 0
        for _, params in cfgData[self.aidapi].items():
            if "class" in params:
                error_ercd("E_RSATR", params,
                           f"{self.aidapi} must not be within a class")
            numAutoObjid += int(params[self.noobj])
        numObjid = len(cfgData[self.api]) + numAutoObjid
```
`TNUM_{OBJ}ID` の値を `len(cfgData[self.api])` → `numObjid` に変更。

`tmax` 出力（`kernel.py:141-142`）を次に置換：
```python
        kernelCfgC.add(f"const ID _kernel_tmax_{self.obj}id"
                       f" = (TMIN_{self.OBJ}ID + TNUM_{self.OBJ}ID - 1);")
        kernelCfgC.add(f"#define TNUM_S{self.OBJ}ID\t{len(cfgData[self.api])}")
        kernelCfgC.add2(f"const ID _kernel_tmax_s{self.obj}id"
                        f" = (TMIN_{self.OBJ}ID + TNUM_S{self.OBJ}ID - 1);")
```
inib_table のサイズトークン（`kernel.py:163-164`）を `[TNUM_{self.OBJ}ID]` → `[TNUM_S{self.OBJ}ID]` へ。

inib_table 出力ブロックの直後（静的0個の else 分岐と対応をとりつつ）へ動的 inib を追加：
```python
        # 動的生成オブジェクト用の初期化ブロック（RAM・非const）
        for typ, array in self.inibList.items():
            if numAutoObjid > 0:
                kernelCfgC.add2(f"{typ} _kernel_{array}[{numAutoObjid}];")
            else:
                kernelCfgC.add2(f"TOPPERS_EMPTY_LABEL({typ}, _kernel_{array});")
```
CB 生成（`kernel.py:174-193`）：予約分の named static とポインタ表末尾を追加。
静的 CB ループの後に：
```python
                for i in range(1, numAutoObjid + 1):
                    kernelCfgC.add(f"static {self.OBJ_S}CB "
                                   f"_kernel_a{self.obj_s}cb_{i};")
```
ポインタ表ループの後（閉じる前）に：
```python
                for i in range(1, numAutoObjid + 1):
                    kernelCfgC.add(",")
                    kernelCfgC.append(f"\t&_kernel_a{self.obj_s}cb_{i}")
```
データ構造全体のガード条件は `len(cfgData[self.api]) > 0` のままでよい
（タスクは静的0個が cfg エラー：`kernel/task.py:104-105`。段階3で `numObjid > 0` へ一般化する）。

- [ ] **Step 4: `kernel/kernel.py` 末尾（Time Event Management の後）にメモリプール出力を追加**
  （dcre kernel.trb 差分 413-460 の Python 化）

```python
#
#  カーネルメモリプール領域
#
kernelCfgC.comment_header("Kernel Memory Pool Area")

if len(cfgData["DEF_MPK"]) == 0:
    mpksz = "0"
    mpk = "NULL"
else:
    if len(cfgData["DEF_MPK"]) > 1:
        error("E_OBJ: too many DEF_MPK")
    params0 = cfgData["DEF_MPK"][1]
    params0.setdefault("mpk", "NULL")
    if params0["mpksz"] == 0:
        error_wrong("E_PAR", params0, "mpksz", "zero")
    if str(params0["mpk"]) == "NULL":
        kernelCfgC.add("static MB_T _kernel_memory_pool"
                       f"[COUNT_MB_T({params0['mpksz']})];")
        mpksz = f"ROUND_MB_T({params0['mpksz']})"
        mpk = "_kernel_memory_pool"
    else:
        if (params0["mpksz"] & (CHECK_MB_ALIGN - 1)) != 0:
            error_wrong("E_PAR", params0, "mpksz", "not aligned")
        mpksz = f"({params0['mpksz']})"
        mpk = f"(void *)({params0['mpk']})"

kernelCfgC.add(f"const size_t _kernel_mpksz = {mpksz};")
kernelCfgC.add2(f"MB_T *const _kernel_mpk = {mpk};")
```
`kernel/task.py:113` の torder を `[TNUM_TSKID]` → `[TNUM_STSKID]` へ。

- [ ] **Step 5: Ruby 側（オラクル）へ同一変更**
  `kernel/kernel.trb`（115-218 の対応箇所）と `kernel/task.trb:128` に、dcre DIFF:2019-2128 の
  該当ハンクを**そのまま**適用する（dcre 版と FMP3 版の kernel.trb は KernelObject の構造が
  同型であることを確認済み。AID のクラス外検査 `error_ercd("E_RSATR", ...)` は Python 側 Step 3 と
  同文言・同位置で入れる）。`kernel/kernel_check.trb` へ dcre DIFF:2152-2192 の DEF_MPK 検査
  ハンク（mpk align/nonnull）を、`kernel/kernel_check.py` へその Python 等価を追加する。
  `kernel/kernel_sym.def` へ dcre と同一の3行を追加：

```
CHECK_MPK_ALIGN,,,defined(CHECK_MPK_ALIGN),1
CHECK_MPK_NONNULL,true,bool,defined(CHECK_MPK_NONNULL),false
CHECK_MB_ALIGN,,,defined(CHECK_MB_ALIGN),1
```

- [ ] **Step 6: 全8構成のビルドと等価性**

```bash
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --preset $p && cmake --build build/$p || exit 1
  tools/cfg_equivalence.sh build/$p || exit 1
done
```
期待: すべて exit=0（`RESULT = MATCH`）。exit=2 は不合格として原因を調べる。

- [ ] **Step 7: 管理された差分の検査（訂正A の代替検査）**

```bash
diff -u /tmp/dcre-base-generated/kernel_cfg.h build/musca_b1-2core/generated/kernel_cfg.h
diff -u /tmp/dcre-base-generated/kernel_cfg.c build/musca_b1-2core/generated/kernel_cfg.c
```
期待: kernel_cfg.h は**差分なし**。kernel_cfg.c の差分が次の**許容リストと完全一致**すること
（1件でも余分な差分があれば不合格）：
1. `#define TNUM_STSKID` 行と `_kernel_tmax_stskid` 行の追加（tmax ブロックの空行位置変化を含む）
2. `_kernel_tinib_table` のサイズトークン `[TNUM_TSKID]`→`[TNUM_STSKID]`
3. `TOPPERS_EMPTY_LABEL(TINIB, _kernel_atinib_table);` の追加
4. `_kernel_torder_table` のサイズトークン同上
5. `Kernel Memory Pool Area` コメントヘッダと `_kernel_mpksz = 0;` / `_kernel_mpk = NULL;` の追加

- [ ] **Step 8: positive control — AID 有り構成で出力が実際に変わること**
  `tools/cfg_error_tests/run.sh` の流儀（ビルド外で両エンジンを回す）で、
  `test/test_int2.cfg` 相当の最小 cfg に `AID_TSK(2); DEF_MPK({ 4096, NULL });` を足した
  一時 cfg を作り pass2 まで実行、生成 kernel_cfg.c に
  `TINIB _kernel_atinib_table[2];`・`_kernel_atcb_1`・`_kernel_atcb_2`・
  `COUNT_MB_T(4096)` が現れ、`TNUM_TSKID` が静的数+2 になることを Ruby/Python 双方で確認し
  **バイト一致**することを確認する。
- [ ] **Step 9: negative control（検査が壊れていないことの実演）**
  `kernel/task.py:113` の `TNUM_STSKID` を一時的に `TNUM_TSKID` へ戻し
  `tools/cfg_equivalence.sh build/musca_b1-2core` が **exit=1** になることを確認して復元する。
- [ ] **Step 10: エラー回帰ケースの追加** — `tools/cfg_error_tests/dcre_aid_in_class.cfg` を新規作成：

```c
/*  AID_TSK をクラスの囲みの中に書くと E_RSATR（クラス外専用 API）  */
INCLUDE("test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	AID_TSK(1);
}
```

```bash
tools/cfg_error_tests/run.sh build/musca_b1-2core \
    tools/cfg_error_tests/dcre_aid_in_class.cfg E_RSATR
```
期待: exit=0（両エンジンが E_RSATR を検出、文言一致）。

- [ ] **Step 11: 台帳とコミット** — `DIVERGENCE_MAP.md` に `kernel_api.def`・`kernel.trb`・
  `task.trb`・`kernel_check.trb`・`kernel_sym.def` の5行（`mod (dcre-port)`）を追記して：

```bash
git add -A && git commit -m "feat(cfg): AID_TSK/DEF_MPK を両エンジンへ追加（dcre段階1、恒常出力基盤）"
```

---

### Task 3: カーネル共通定義とメモリプール

**推奨モデル:** 中位（sonnet）

**Files:**
- Modify: `kernel/kernel_impl.h`（`:199` の TA_NOEXS 直後に追加）
- Modify: `kernel/check.h`（末尾に追加）
- Modify: `kernel/startup.c`（mempool 関数群 + `sta_ker` の mpk 初期化）
- Modify: `kernel/kernel_rename.def` → `utils/genrename.rb` で再生成
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Produces: `TA_MEMALLOC`、`mpk`/`mpksz`/`mpk_valid`、`initialize_mempool`/`malloc_mempool`/
  `aligned_alloc_mempool`/`free_mempool`、inline `malloc_mpk`/`aligned_alloc_mpk`/`free_mpk`、
  check.h の `ALIGNED`/`STKSZ_ALIGN`/`FUNC_ALIGN`/`FUNC_NONNULL`/`STACK_ALIGN`
- Consumes: Task 2 の `_kernel_mpksz`/`_kernel_mpk`（cfg 生成）

- [ ] **Step 1: `kernel/kernel_impl.h`** — `TA_NOEXS`（`:199`）の直後に dcre DIFF:2193-2277 と
  同内容を追加（`TA_MEMALLOC 0x8000`、`extern MB_T *const mpk; extern const size_t mpksz;
  extern bool_t mpk_valid;`、`initialize_mempool` 等4関数の extern、`malloc_mpk`/
  `aligned_alloc_mpk`/`free_mpk` の Inline 3関数）。dcre の該当ブロックを転記し、
  ASP3 に無い `mpksz` の extern を1行足す（dcre は kernel.trb 出力の `_kernel_mpksz` を
  startup.c から直接 extern しているが、FMP3 では kernel_impl.h に集約する）。
- [ ] **Step 2: `kernel/check.h`** — 末尾（`#endif` 直前）に dcre DIFF:569-656 の
  `ALIGNED`/`STKSZ_ALIGN`/`INTPTR_ALIGN`/`FUNC_ALIGN`/`FUNC_NONNULL`/`STACK_ALIGN`/
  `MPF_ALIGN`/`MB_ALIGN` マクロ群を転記する（`CHECK_*_ALIGN` 未定義時 true に落ちる形。全文は dcre の
  `extension/dcre/kernel/check.h` 該当部をそのまま）。
- [ ] **Step 3: `kernel/startup.c`** — ファイル末尾（`ext_ker` 群の後）に dcre `startup.c` の
  `TOPPERS_kermem` ブロック（`MEMPOOLCB` 構造体・`align_pointer`・`initialize_mempool`・
  `malloc_mempool`・`aligned_alloc_mempool`・`free_mempool`、`OMIT_MEMPOOL_DEFAULT` ガード付き）を
  そのまま転記。`sta_ker` の `barrier_sync(2)`（`startup.c:176`）の直後に挿入：

```c
	/*
	 *  カーネルメモリプール領域の初期化（マスタプロセッサのみ）
	 */
	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		if (mpksz > 0U && mpk != NULL) {
			mpk_valid = initialize_mempool(mpk, mpksz);
		}
		else {
			mpk_valid = false;
		}
	}
```
（可視性は後続の `barrier_sync(3)` が保証する。）`bool_t mpk_valid;` の定義も startup.c に置く。

- [ ] **Step 4: rename 再生成** — `kernel/kernel_rename.def` の `# startup.c` 節に
  `mpk_valid` `initialize_mempool` `malloc_mempool` `aligned_alloc_mempool` `free_mempool` を追加し：

```bash
cd kernel && ruby ../utils/genrename.rb kernel && cd ..
git diff --stat kernel/kernel_rename.h kernel/kernel_unrename.h   # 追加分だけ増えること
```
（`kernel_rename.h:1` に「generated from kernel_rename.def by genrename」とある通り手書き禁止。）

- [ ] **Step 5: ビルド・起動回帰**

```bash
cmake --build build/musca_b1-2core && tools/cfg_equivalence.sh build/musca_b1-2core
timeout -k 5 20 cmake --build build/musca_b1-2core --target run > /tmp/dcre-t3-run.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre-t3-run.log   # 期待: 2
```
- [ ] **Step 6: 台帳（kernel_impl.h / check.h / startup.c / kernel_rename.def+再生成2ファイル）とコミット**

```bash
git add -A && git commit -m "feat(kernel): カーネルメモリプールと共通定義を追加（dcre段階1）"
```

---

### Task 4: task 層 — free_tcb・initialize_task・TSKID・ARM-M ctxb ヘルパ

**推奨モデル:** 中位（sonnet）。挙動不変のまま構造だけ入れる、境界の判断が要る。

**Files:**
- Modify: `kernel/task.h`（externs・tnum_stsk・TSKID）
- Modify: `kernel/task.c`（free_tcb 定義・initialize_task）
- Modify: `arch/arm_m_gcc/common/core_kernel_impl.h`（ctxb ヘルパ2つ）
- Modify: `kernel/kernel_rename.def`（再生成含む）
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Produces: `QUEUE free_tcb`、`tmax_stskid`/`tnum_stsk`、2レンジ対応 `TSKID()`、
  `init_tskinictxb(TSKINICTXB *, size_t, STK_T *)`、`void *tskinictxb_memalloc_ptr(TSKINICTXB *)`
- Consumes: Task 2 の `_kernel_atinib_table`/`_kernel_tmax_stskid`

- [ ] **Step 1: `kernel/task.h`** — `p_tcb_table` extern（`:297`）の周辺に dcre task.h 差分と同型で追加：

```c
extern QUEUE	free_tcb;            /* 使用していないTCBのリスト */
extern const ID	tmax_stskid;         /* 静的生成タスクのID番号の最大値 */
extern TINIB	atinib_table[];      /* 動的生成タスクの初期化ブロック（RAM） */
#define tnum_stsk	((uint_t)(tmax_stskid - TMIN_TSKID + 1))
```
`TSKID` マクロ（`task.h:313`）を2レンジ版へ置換（spec §4.3 の式そのまま）：

```c
#define TSKID(p_tcb) \
	((((p_tcb)->p_tinib >= atinib_table) \
		&& ((p_tcb)->p_tinib < &atinib_table[tnum_tsk - tnum_stsk])) \
	  ? ((ID)(((p_tcb)->p_tinib - atinib_table) + TMIN_TSKID + tnum_stsk)) \
	  : ((ID)(((p_tcb)->p_tinib - tinib_table) + TMIN_TSKID)))
```
- [ ] **Step 2: `kernel/task.c`** — `#ifdef TOPPERS_tskini` ブロック（`:52`）に `QUEUE free_tcb;` を
  定義し、`initialize_task`（`:57-89`）の静的ループ境界を `tnum_tsk` → `tnum_stsk` に変え、
  ループ後に追加：

```c
	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		queue_initialize(&free_tcb);
		for (i = tnum_stsk, j = 0; i < tnum_tsk; i++, j++) {
			p_tcb = p_tcb_table[i];
			atinib_table[j].tskatr = TA_NOEXS;
			p_tcb->p_tinib = (const TINIB *) &(atinib_table[j]);
			p_tcb->p_pcb = get_pcb(1);		/* 動的タスクは PRC1 所属（Constraint 4） */
			queue_insert_prev(&free_tcb, &(p_tcb->task_queue));
		}
	}
```
（静的ループは `torder_table[TNUM_STSKID]` しか走査しないため他プロセッサと競合しない。
マスタ以外は動的スロットに触れない。）

- [ ] **Step 3: `arch/arm_m_gcc/common/core_kernel_impl.h`** — TSKINICTXB 定義（`:118`）の直後に追加：

```c
#ifndef TOPPERS_MACRO_ONLY
/*
 *  タスク初期化コンテキストブロックの初期化（動的生成用）
 */
Inline void
init_tskinictxb(TSKINICTXB *p_tskinictxb, size_t stksz, STK_T *stk)
{
	p_tskinictxb->stk_top = (uint32_t *)(stk);
	p_tskinictxb->stk_bottom = (uint32_t *)((char *)(stk) + (stksz));
}

/*
 *  TA_MEMALLOC で確保したスタック領域の先頭番地（free_mpk へ渡す値）
 */
Inline void *
tskinictxb_memalloc_ptr(TSKINICTXB *p_tskinictxb)
{
	return((void *)(p_tskinictxb->stk_top));
}
#endif /* TOPPERS_MACRO_ONLY */
```
（`GenerateTskinictxb`（`core_kernel.trb:58-64`）が stk_top=先頭番地/stk_bottom=末尾番地で
埋めるのと同じ約束。v6m 版 `core_kernel_v6m.trb:102-108` も同一レイアウトなので共通で足りる。）

- [ ] **Step 4: rename 追加・再生成** — `# task.c` 節（`kernel_rename.def:7-`）へ
  `free_tcb` `tmax_stskid` `atinib_table` を追加し `ruby ../utils/genrename.rb kernel`。
- [ ] **Step 5: 全構成ビルド + musca_b1-2core / kria_r5-2core 起動 + 等価性**（Task 3 Step 5 と同じ
  コマンド列。kria_r5-2core は `grep -c 'Processor [12] start\.'` 期待2）。挙動はまだ不変
  （AID 無しでは `tnum_stsk == tnum_tsk`、動的ループは空振り）。
- [ ] **Step 6: 台帳（task.h / task.c / core_kernel_impl.h / rename 3ファイル）とコミット**

```bash
git add -A && git commit -m "feat(kernel): free_tcb と動的スロット初期化・2レンジTSKID（dcre段階1）"
```

---

### Task 5: acre_tsk / del_tsk と E_NOEXS 検査・宣言配線

**推奨モデル:** 中位（sonnet）

**Files:**
- Modify: `include/kernel.h`（`:136` T_RTSK の手前に T_CTSK、`:212` act_tsk 宣言群の手前に2宣言）
- Modify: `kernel/task_manage.c`（acre_tsk / del_tsk 追加 + E_NOEXS 検査）
- Modify: `kernel/task_refer.c` `kernel/task_sync.c` `kernel/task_term.c`（E_NOEXS 検査）
- Modify: `kernel/allfunc.h`（3行）・`kernel/Makefile.kernel`（TASK_MANAGE の .o 2個追記）
- Modify: `kernel/kernel_rename.def`（再生成含む・必要分）
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Consumes: Task 3 の `aligned_alloc_mpk`/`free_mpk`/`TA_MEMALLOC`/check マクロ、
  Task 4 の `free_tcb`/`atinib_table`/`tmax_stskid`/`TSKID`/`init_tskinictxb`/`tskinictxb_memalloc_ptr`
- Produces: `ER_ID acre_tsk(const T_CTSK *)` / `ER del_tsk(ID)`（ユーザ API）

- [ ] **Step 1: `include/kernel.h`** — `t_rtsk`（`:136`）の直前に spec §2.1 の `T_CTSK` を、
  `act_tsk`（`:212`）の直前に追加：

```c
extern ER_ID	acre_tsk(const T_CTSK *pk_ctsk) throw();
extern ER		del_tsk(ID tskid) throw();
```
（機能コードは `include/kernel_fncode.h:134,146` に `TFN_ACRE_TSK (-193)`/`TFN_DEL_TSK (-209)` が
既存。追加不要。）

- [ ] **Step 2: `kernel/task_manage.c` に acre_tsk を追加**（`act_tsk` ブロックの直前。
  dcre 版 `extension/dcre/kernel/task_manage.c` を FMP3 のロック・ディスパッチ規約
  （`task_manage.c:152-153` の act_tsk と同じ）へ適応した全文）：

```c
/*
 *  タスクの生成（動的生成）
 */
#ifdef TOPPERS_acre_tsk

#ifndef LOG_ACRE_TSK_ENTER
#define LOG_ACRE_TSK_ENTER(pk_ctsk)
#endif /* LOG_ACRE_TSK_ENTER */
#ifndef LOG_ACRE_TSK_LEAVE
#define LOG_ACRE_TSK_LEAVE(ercd)
#endif /* LOG_ACRE_TSK_LEAVE */

ER_ID
acre_tsk(const T_CTSK *pk_ctsk)
{
	TCB		*p_tcb;
	TINIB	*p_tinib;
	ATR		tskatr;
	TASK	task;
	PRI		itskpri;
	size_t	stksz;
	STK_T	*stk;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_ACRE_TSK_ENTER(pk_ctsk);
	CHECK_TSKCTX_UNL();

	tskatr = pk_ctsk->tskatr;
	task = pk_ctsk->task;
	itskpri = pk_ctsk->itskpri;
	stksz = pk_ctsk->stksz;
	stk = pk_ctsk->stk;

	CHECK_VALIDATR(tskatr, TA_ACT|TA_NOACTQUE|TARGET_TSKATR);
	CHECK_PAR(FUNC_ALIGN(task));
	CHECK_PAR(FUNC_NONNULL(task));
	CHECK_PAR(VALID_TPRI(itskpri));
	CHECK_PAR(stksz >= TARGET_MIN_STKSZ);
	if (stk != NULL) {
		CHECK_PAR(STKSZ_ALIGN(stksz));
		CHECK_PAR(STACK_ALIGN(stk));
	}

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	p_selftsk = p_my_pcb->p_runtsk;
	if (queue_empty(&free_tcb)) {
		ercd = E_NOID;
	}
	else {
		if (stk == NULL) {
			stksz = ROUND_STK_T(stksz);
			stk = aligned_alloc_mpk(alignof(STK_T), stksz);
			tskatr |= TA_MEMALLOC;
		}
		if (stk == NULL) {
			ercd = E_NOMEM;
		}
		else {
			p_tcb = ((TCB *) queue_delete_next(&free_tcb));
			p_tinib = (TINIB *)(p_tcb->p_tinib);
			p_tinib->tskatr = tskatr;
			p_tinib->exinf = pk_ctsk->exinf;
			p_tinib->task = task;
			p_tinib->ipriority = INT_PRIORITY(itskpri);
#ifdef USE_TSKINICTXB
			init_tskinictxb(&(p_tinib->tskinictxb), stksz, stk);
#else /* USE_TSKINICTXB */
			p_tinib->stksz = stksz;
			p_tinib->stk = stk;
#endif /* USE_TSKINICTXB */
			p_tinib->iprcid = 1;			/* Constraint 4: PRC1 固定 */
			p_tinib->affinity = ((uint_t)((1U << TNUM_PRCID) - 1U));

			p_tcb->actque = false;
			p_tcb->actprc = TPRC_NONE;
			p_tcb->subpri = UINT_MAX;
			p_tcb->p_pcb = get_pcb(1);
			p_tcb->p_lastmtx = NULL;
			make_dormant(p_tcb);
			ercd = TSKID(p_tcb);
			if ((p_tcb->p_tinib->tskatr & TA_ACT) != 0U) {
				make_active(p_my_pcb, p_tcb);
				if (p_selftsk != p_my_pcb->p_schedtsk) {
					release_glock();
					dispatch();
					goto unlock_and_exit;
				}
			}
		}
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu();

  error_exit:
	LOG_ACRE_TSK_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_acre_tsk */
```

- [ ] **Step 3: del_tsk を追加**（acre_tsk の直後）：

```c
/*
 *  タスクの削除
 */
#ifdef TOPPERS_del_tsk

#ifndef LOG_DEL_TSK_ENTER
#define LOG_DEL_TSK_ENTER(tskid)
#endif /* LOG_DEL_TSK_ENTER */
#ifndef LOG_DEL_TSK_LEAVE
#define LOG_DEL_TSK_LEAVE(ercd)
#endif /* LOG_DEL_TSK_LEAVE */

ER
del_tsk(ID tskid)
{
	TCB		*p_tcb;
	TINIB	*p_tinib;
	ER		ercd;

	LOG_DEL_TSK_ENTER(tskid);
	CHECK_TSKCTX_UNL();
	CHECK_ID(VALID_TSKID(tskid));
	p_tcb = get_tcb(tskid);

	lock_cpu();
	acquire_glock();
	if (p_tcb->p_tinib->tskatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (tskid <= tmax_stskid || !TSTAT_DORMANT(p_tcb->tstat)) {
		ercd = E_OBJ;
	}
	else {
		p_tinib = (TINIB *)(p_tcb->p_tinib);
		if ((p_tinib->tskatr & TA_MEMALLOC) != 0U) {
#ifdef USE_TSKINICTXB
			free_mpk(tskinictxb_memalloc_ptr(&(p_tinib->tskinictxb)));
#else /* USE_TSKINICTXB */
			free_mpk(p_tinib->stk);
#endif /* USE_TSKINICTXB */
		}
		p_tinib->tskatr = TA_NOEXS;
		queue_insert_prev(&free_tcb, &(p_tcb->task_queue));
		ercd = E_OK;
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_DEL_TSK_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_del_tsk */
```
（DORMANT はジャイアントロック下で `actque == false` を含意する — spec §2.3。追加検査不要。）

- [ ] **Step 4: E_NOEXS 検査の追加** — 次の**16関数**に、`acquire_glock()` の直後・既存の状態判定の
  **最初の分岐として** `if (p_tcb->p_tinib->tskatr == TA_NOEXS) { ercd = E_NOEXS; } else ...` を
  挿入する（dcre の task_manage.c/task_refer.c/task_sync.c/task_term.c 差分と同位置。
  `TSK_SELF` で `p_selftsk` を使う経路は実行中タスクであり TA_NOEXS になり得ないため対象外）：
  - `task_manage.c`: `act_tsk` `mact_tsk` `can_act` `mig_tsk` `get_tst` `chg_pri` `get_pri` `chg_spr`
  - `task_refer.c`: `ref_tsk`
  - `task_sync.c`: `wup_tsk` `can_wup` `rel_wai` `sus_tsk` `rsm_tsk`
  - `task_term.c`: `ras_ter` `ter_tsk`

  例（act_tsk、`task_manage.c:152-155` を次の形へ）：

```c
	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_tcb->p_tinib->tskatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (TSTAT_DORMANT(p_tcb->tstat)) {
		...（既存本体は else if 連鎖へ字下げのまま繰り込む）
```

- [ ] **Step 5: 配線** — `kernel/allfunc.h` 末尾（`#endif` 手前）へ
  `#define TOPPERS_acre_tsk` `#define TOPPERS_del_tsk` `#define TOPPERS_kermem` の3行。
  `kernel/Makefile.kernel` の `task_manage =` 行へ `acre_tsk.o del_tsk.o` を追記
  （CMake は `ALLFUNC` ビルドで参照しないが上流形式維持。`KERNEL_FCSRCS` は**触らない**）。
  rename 追加が必要な新規内部シンボルは今 Task では無し（acre/del は公開名）。
- [ ] **Step 6: ビルド + 等価性 + 起動**（Task 3 Step 5 と同一コマンド列、全8構成ビルドは
  `for` ループ版）。AID 無し構成の挙動は不変であること。
- [ ] **Step 7: 台帳（kernel.h / task_manage.c / task_refer.c / task_sync.c / task_term.c /
  allfunc.h / Makefile.kernel）とコミット**

```bash
git add -A && git commit -m "feat(kernel): acre_tsk/del_tsk とE_NOEXS検査を追加（dcre段階1）"
```

---

### Task 6: QEMU 回帰テスト test_dcre1

**推奨モデル:** 中位（sonnet）。テストハーネス自体が本リポジトリ初回実行になるため。

**Files:**
- Create: `test/test_dcre1.c` `test/test_dcre1.cfg` `test/test_dcre1.h`
- Modify: `test/MANIFEST`（アルファベット順の位置へ3行）・`test/testexec.rb:96` 付近
  （`"dcre1"    => { SRC: "test_dcre1" },`）
- Modify: `DIVERGENCE_MAP.md`（test/ への追加も記録する運用）

**Interfaces:** Consumes Task 2-5 の全成果。`syssvc/test_svc.h` の `check_point`/
`check_point_prc`/`check_ercd`/`check_finish`（`test_svc.c:201-213` — 成功時
`All check points passed.` と `TTSP_RESULT: PASS` を syslog 出力）。

- [ ] **Step 1: ★ハーネスの実証（新テストより先）** — 本リポジトリの CMake で test/ 配下を
  ビルド・実行した前例は無い（test_int2 の検証は ESP32-S3 ポート側で行われた。
  コミット cfd4f3c 末尾の記載）。まず既存 test_int2 で経路を通す：

```bash
cmake --preset musca_b1-2core -B build/musca_b1-2core-tint2 \
  -DFMP3_APPLNAME=test_int2 -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c
cmake --build build/musca_b1-2core-tint2
timeout -k 5 30 cmake --build build/musca_b1-2core-tint2 --target run \
  > /tmp/dcre-tint2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre-tint2.log
```
期待: PASS 行が実在（rc は見ない）。`FMP3_SYSSVC_TARGET_C_FILES` はキャッシュ変数として渡すと
`target/musca_b1_gcc/target.cmake:50` の `list(APPEND ...)` が読み継ぐ
（APPEND は通常変数が無ければキャッシュ値から出発する）。
**この Step が失敗したら、それはハーネスの欠陥であり本 Task の残りに進まず報告する。**

- [ ] **Step 2: `test/test_dcre1.h`**（`test_int2.h` と同型のヘッダ + 本テスト固有定義）：

```c
#include <kernel.h>
#include "target_test.h"

#define HIGH_PRIORITY	9
#define MID_PRIORITY	10
#define LOW_PRIORITY	11

#ifndef STACK_SIZE
#define	STACK_SIZE		4096
#endif /* STACK_SIZE */

#ifndef TEST_TIME_PROC
#define TEST_TIME_PROC	1000U		/* 他タスクに実行機会を与える待ち時間 */
#endif /* TEST_TIME_PROC */

/*  MEMPOOLCB とスタック1本（ROUND後）が入り、2本目は入らない大きさ  */
#define MPK_SIZE		(STACK_SIZE + 1024)

#ifndef TOPPERS_MACRO_ONLY
extern void	task1(EXINF exinf);
extern void	dtask_a(EXINF exinf);
extern void	dtask_b(EXINF exinf);
extern void	dtask_prc2(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */
```

- [ ] **Step 3: `test/test_dcre1.cfg`**：

```c
/*
 *		動的生成API（acre_tsk/del_tsk/DEF_MPK）のテスト
 */
INCLUDE("test_common1.cfg");

#include "test_dcre1.h"

CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
}

/*  AID_TSK/DEF_MPK はクラス外専用（Task 2 の E_RSATR 検査対象）  */
AID_TSK(2);
DEF_MPK({ MPK_SIZE, NULL });
```

- [ ] **Step 4: `test/test_dcre1.c`**（著作権ヘッダは test_int2.c と同形式で付ける）：

```c
#include <kernel.h>
#include <t_syslog.h>
#include "syssvc/test_svc.h"
#include "kernel_cfg.h"
#include "test_dcre1.h"

/*  ユーザ供給スタック（シナリオ1で使用）  */
static STK_T	user_stk[COUNT_STK_T(STACK_SIZE)];

static ID	dtskid1;
static ID	dtskid2;

/*  動的タスクA（PRC1・ユーザ供給スタック・即終了）  */
void
dtask_a(EXINF exinf)
{
	ID		prcid;

	check_ercd(get_pid(&prcid), E_OK);
	check_assert(prcid == 1);
	check_assert(((intptr_t) exinf) == 0x11);
	check_point(2);
	ext_tsk();
}

/*  動的タスクB（低優先度・待ち続ける。del_tsk(E_OBJ) の的）  */
void
dtask_b(EXINF exinf)
{
	check_point(7);
	slp_tsk();				/* 待ち中に ter_tsk で強制終了される（戻ってこない） */
	check_point(0);			/* 到達したら「Unexpected check point 0」で失敗する */
}

/*  動的タスクC（PRC2 で走行することを自ら検証）  */
void
dtask_prc2(EXINF exinf)
{
	ID		prcid;

	check_ercd(get_pid(&prcid), E_OK);
	check_assert(prcid == 2);
	check_point_prc(11, 2);
	check_ercd(wup_tsk(TASK1), E_OK);
	ext_tsk();
}

void
task1(EXINF exinf)
{
	T_CTSK	ctsk;
	ER_ID	erid;
	ER		ercd;
	T_RTSK	rtsk;

	test_start(__FILE__);
	check_point(1);

	/*  1) acre → act → 実行 → 自然終了  */
	ctsk.tskatr = TA_NULL;
	ctsk.exinf = (EXINF) 0x11;
	ctsk.task = dtask_a;
	ctsk.itskpri = HIGH_PRIORITY;
	ctsk.stksz = STACK_SIZE;
	ctsk.stk = user_stk;
	erid = acre_tsk(&ctsk);
	check_assert(erid > 0);
	dtskid1 = (ID) erid;
	check_ercd(act_tsk(dtskid1), E_OK);		/* HIGH が MID を横取り → cp2 */
	check_point(3);

	/*  2) 2個目の生成でスロットを使い切ってから del → 再 acre で同一 ID。
	 *  ★free-list は dcre 忠実移植で FIFO（del_tsk は queue_insert_prev で
	 *    末尾へ・acre_tsk は queue_delete_next で先頭から）。LIFO ではない。
	 *    空きが1個だけの状態での再 acre なら、削除したスロット＝同一 ID の
	 *    返却が FIFO/LIFO どちらでも決定的に成立する（Task 4 レビューで
	 *    旧シナリオの LIFO 仮定が dcre の実挙動と矛盾すると判明し修正）。  */
	ctsk.task = dtask_b;
	ctsk.itskpri = LOW_PRIORITY;
	erid = acre_tsk(&ctsk);
	check_assert(erid > 0 && ((ID) erid) != dtskid1);
	dtskid2 = (ID) erid;
	check_ercd(del_tsk(dtskid1), E_OK);
	ctsk.task = dtask_a;
	ctsk.itskpri = HIGH_PRIORITY;
	erid = acre_tsk(&ctsk);
	check_assert(((ID) erid) == dtskid1);
	check_point(4);

	/*  3) スロット枯渇 E_NOID（全スロット使用中に追加を要求）  */
	erid = acre_tsk(&ctsk);
	check_assert(erid == E_NOID);
	check_point(5);

	/*  4) del_tsk のエラー系：静的タスク → E_OBJ / 休止でない → E_OBJ  */
	check_ercd(del_tsk(TASK1), E_OBJ);
	check_point(6);
	check_ercd(act_tsk(dtskid2), E_OK);		/* LOW なので READY のまま */
	check_ercd(del_tsk(dtskid2), E_OBJ);	/* 休止でない */
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);	/* LOW に実行機会 → cp7 → slp */

	/*  5) 強制終了 → 休止 → del 成功  */
	check_ercd(ter_tsk(dtskid2), E_OK);		/* 待ち中タスクの強制終了 */
	check_ercd(get_tst(dtskid2, &(rtsk.tskstat)), E_OK);
	check_assert(rtsk.tskstat == TTS_DMT);
	check_ercd(del_tsk(dtskid2), E_OK);
	check_point(8);

	/*  6) 削除済み ID へのサービスコール → E_NOEXS  */
	check_ercd(wup_tsk(dtskid2), E_NOEXS);
	check_ercd(act_tsk(dtskid2), E_NOEXS);
	check_point(9);

	/*  7) stk=NULL 自動確保：プール超過 E_NOMEM と、成功
	 *  ※E_NOMEM を先に試す。逆順だと空きスロットが尽きて E_NOID が先に
	 *    返り、メモリ枯渇経路を通らない（acre_tsk は free_tcb 検査が先）。  */
	ctsk.task = dtask_prc2;
	ctsk.itskpri = HIGH_PRIORITY;
	ctsk.stk = NULL;
	ctsk.stksz = MPK_SIZE * 4;				/* プールに入らない大きさ */
	erid = acre_tsk(&ctsk);
	check_assert(erid == E_NOMEM);
	ctsk.stksz = STACK_SIZE;
	erid = acre_tsk(&ctsk);
	check_assert(erid > 0);
	dtskid1 = (ID) erid;
	check_point(10);

	/*  8) 全コア affinity の実証：PRC2 へ mact_tsk  */
	check_ercd(mact_tsk(dtskid1, 2), E_OK);
	check_ercd(slp_tsk(), E_OK);			/* dtask_prc2 が cp11 → wup */
	check_point(12);
	do {
		check_ercd(get_tst(dtskid1, &(rtsk.tskstat)), E_OK);
	} while (rtsk.tskstat != TTS_DMT);
	check_ercd(del_tsk(dtskid1), E_OK);

	check_finish(13);
}
```
※ 手順4の途中コメントにある通り、dtask_b への実行機会付与は `dly_tsk` 等で調整が要る。
実装時に cp 番号の整合（6↔7 の順序）を QEMU 出力で確認し、**必ず cp 番号の単調増加が
test_svc に検査される**ことを利用する（順序が崩れると `## Unexpected check point` が出て失敗する）。
`check_assert`/`test_start` が `test_svc.h` に無い場合は既存テストで使われている等価マクロ
（`check_point`/`check_ercd` と `syslog` 直書き）に置き換えること — **`test_svc.h` を先に読み、
存在する検査プリミティブだけで書く**。

- [ ] **Step 5: 登録** — `test/MANIFEST:56`（`test_int2.h` の後）に `test_dcre1.c` `.cfg` `.h` を
  アルファベット順で、`test/testexec.rb:96` の並びに `"dcre1"    => { SRC: "test_dcre1" },` を追加。
- [ ] **Step 6: ビルド・実行**

```bash
cmake --preset musca_b1-2core -B build/musca_b1-2core-tdcre1 \
  -DFMP3_APPLNAME=test_dcre1 -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c
cmake --build build/musca_b1-2core-tdcre1
tools/cfg_equivalence.sh build/musca_b1-2core-tdcre1        # AID有り実構成の等価性（初）
timeout -k 5 40 cmake --build build/musca_b1-2core-tdcre1 --target run \
  > /tmp/dcre-t6-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre-t6-run.log
grep -c 'Check point' /tmp/dcre-t6-run.log
```
期待: `TTSP_RESULT: PASS` が実在。`Check point 2-11 passed.`（PRC2 実行の証拠）を含む。

- [ ] **Step 7: ★カーネル変異 negative control（生きた経路で）** — `del_tsk` の
  `queue_insert_prev(&free_tcb, ...)` 行を一時的にコメントアウトして再ビルド・再実行し、
  シナリオ2の再 acre が **E_NOID となってテストが実際に失敗する**
  （`## Unexpected` 行が出る／PASS 行が出ない）ことを確認して復元する。
  **TNUM_PRCID==1 の死んだ分岐への変異は不可**（本セッションで2度やらかした型）。

```bash
git stash && cmake --build build/musca_b1-2core-tdcre1 && \
timeout -k 5 40 cmake --build build/musca_b1-2core-tdcre1 --target run \
  > /tmp/dcre-t6-neg.log 2>&1 ; grep -L 'TTSP_RESULT: PASS' /tmp/dcre-t6-neg.log
git stash pop
```
（↑stash の向きは「変異を stash から適用」ではなく「変異を作ってから検証→復元」の順で
適宜読み替えること。復元後に Step 6 を再実行して PASS に戻ることまで確認する。）

- [ ] **Step 8: 台帳（test/ 3ファイル+MANIFEST+testexec.rb）とコミット**

```bash
git add -A && git commit -m "test(dcre): 動的生成APIの回帰テスト test_dcre1 を追加（2コアQEMU）"
```

---

### Task 7: 最終回帰と台帳整理

**推奨モデル:** 中位（sonnet）

**Files:**
- Modify: `DIVERGENCE_MAP.md`（掃除）・`.superpowers/sdd/progress.md`（記録）

- [ ] **Step 1: 全9プリセット configure+build**（`polarfire_soc_kit` 実機プリセットは
  SoftConsole ツールチェーン不在で fail する既知の環境ギャップ — nano.specs。
  それ**以外**の8構成が exit=0 であること）。
- [ ] **Step 2: 全8構成 `tools/cfg_equivalence.sh` exit=0。**
- [ ] **Step 3: QEMU 起動7構成**（polarfire-qemu 4行 / musca_b1 1行 / musca_b1-2core 2行 /
  kria_arm64 4行・1core 1行 / kria_r5 1行・2core 2行の `Processor N start.` を grep で確認。
  rc は見ない）。test_dcre1（Task 6 Step 6）も再実行して PASS。
- [ ] **Step 4: エラー経路回帰** — `tools/cfg_error_tests/run.sh` を既存ケース
  （`e_par_creisr_intno_keyerror`(polarfire)・`musca_b1_e_rsatr_intno_affinity`(musca_b1-2core)・
  `kria_r5_e_rsatr_intno_affinity`(kria_r5-2core)）+ Task 2 の `dcre_aid_in_class` で実行、全 exit=0。
- [ ] **Step 5: `KERNEL_FCSRCS` 突き合わせ**（AGENTS.md §4。22個のまま不変のはず）：

```bash
diff <(awk '/^KERNEL_FCSRCS/{f=1} f{print} f&&!/\\$/{exit}' kernel/Makefile.kernel \
       | grep -oE '[a-z_]+\.c' | sort -u) \
     <(grep -oE 'kernel/[a-z_]+\.c' CMakeLists.txt | sed 's#kernel/##' | sort -u)
```
期待: 差分なし。
- [ ] **Step 6: `DIVERGENCE_MAP.md` の掃除** — Task 2〜6 の全 pristine 編集
  （kernel_api.def / kernel.trb / task.trb / kernel_check.trb / kernel_sym.def / kernel_impl.h /
  check.h / startup.c / task.h / task.c / task_manage.c / task_refer.c / task_sync.c / task_term.c /
  allfunc.h / Makefile.kernel / kernel_rename.def+再生成2 / include/kernel.h /
  arm_m core_kernel_impl.h / test 5ファイル）が**1ファイル1行以上**記録されていることを
  `git diff main...HEAD --name-only` と突き合わせて確認。漏れは（あってはならないが）
  この時点で気づいたなら**理由込みで**追記する。
- [ ] **Step 7: progress.md へ段階1完了を記録し、コミット**

```bash
git add -A && git commit -m "chore(dcre): 段階1の最終回帰と台帳整理"
```

---

## Self-Review 済み事項（計画作成時の検証記録）

- spec 全要件→Task 対応：§2.1-2.2(T5)・§2.3(T5)・§2.4(T5 Step4)・§3.1-3.4(T2)・§4.1(T5)・
  §4.2(T4)・§4.3(T4)・§4.4(T3)・§5(T3-T5)・§6.1(T2/T7)・§6.2(T6)・§6.3(T2 Step8-9/T6 Step7)。
  §4.5（段階3互換）は T2 の `inibList`/`aidapi` 一般化で担保。
- 現物確認済みの前提：`TFN_ACRE_TSK/-193`・`TFN_DEL_TSK/-209`（`include/kernel_fncode.h:134,146`）、
  `MB_T`（`include/t_stddef.h:131`）、`TOPPERS_COUNT_SZ`（`include/kernel.h:547-`）、
  `genrename.rb` は `<name>_rename.def` を cwd から読む（`utils/genrename.rb:96`）、
  `NumStr.__int__`（`cfg_py/cfg.py:109`）、`get_pcb`（`kernel/pcb.h:181`）、
  `TOPPERS_MASTER_PRCID`（`kernel/kernel_impl.h:139-141` 近傍）、
  test_svc の PASS 文字列（`syssvc/test_svc.c:205-206`）。
- 未検証（実装者が最初に当たること）：`check_assert`/`test_start` マクロの存在（T6 Step 4 注記）、
  `FMP3_SYSSVC_TARGET_C_FILES` のキャッシュ変数渡し（T6 Step 1 が実証を兼ねる）、
  dtask_b の実行機会調整（cp6/7 の順序）。
