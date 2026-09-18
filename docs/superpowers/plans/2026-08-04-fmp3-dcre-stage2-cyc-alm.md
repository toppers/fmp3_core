# FMP3 動的生成API 段階2（acre_cyc/del_cyc + acre_alm/del_alm）実装計画

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `feature/dynamic-creation` ブランチ上で `acre_cyc`/`del_cyc`・`acre_alm`/`del_alm` と
`AID_CYC`/`AID_ALM` を、cfg 両エンジン（Ruby オラクル + Python 製品）同時対応・
QEMU 回帰テスト付きで動かす。あわせて arm_m の `core_rename.def` 欠落
（`sense_lock`/`unlock_cpu`）を修正し、段階1で閉じられなかった test_int2/musca_b1-2core の
ハーネス経路を閉じる。

**Architecture:** ASP3 dcre の機構（cfg 予約スロットの free-list + RAM `acycinib_table[]` +
ランタイム通知機構 `check_nfyinfo`/`notify_handler`）を忠実移植し、FMP3 固有の4点
（ジャイアントロック・CYCINIB の iprcid/affinity・CYCCB の p_pcb・named-static CB +
const ポインタ表）を局所適応する。段階1が一般化した cfg 共通枠組み（`@aidapi`/`inibList`）は
**そのまま再利用**でき、cyclic/alarm 側の変更は `inibList` への1行追加で足りる。

**Tech Stack:** C（カーネル）、Python/Ruby（cfg テンプレート）、CMake、QEMU（musca_b1・kria_r5 ほか）。

---

## Global Constraints（spec から転記。実装中に再解釈しない）

1. ブランチ `feature/dynamic-creation`（段階1の続き）。**main へはマージしない。**
   pristine への改変は `DIVERGENCE_MAP.md` に記録する（種別 `mod (dcre-port)`、上流報告欄 `-`）。
2. 段階2 = `acre_cyc`/`del_cyc`・`acre_alm`/`del_alm` + `AID_CYC`/`AID_ALM` +
   ランタイム通知機構 + arm_m rename 修正のみ。**ISR は一切含めない**（`acre_isr`/`del_isr`・
   ISRINIB・isr_queue のいずれにも触れない）。段階3（sem/flg/dtq/pdq/mtx/mpf）も含めない。
3. API 面は dcre 標準のみ：`T_CCYC`/`T_CALM` は `T_NFYINFO` を含む dcre 定義そのまま
   （全通知モードをサポート）。独自 API なし。`acre_*` にクラス引数なし。
   `AID_CYC`/`AID_ALM` はクラス外専用（クラス内は E_RSATR）。
4. 動的生成 cyc/alm は **`iprcid = TOPPERS_MASTER_PRCID`（= PRC1）** をカーネルが固定で埋める。
   affinity は **`TOPPERS_TEPP_PRC`**（時間イベント処理プロセッサ集合）。
   ★訂正C（後述）により spec Constraint 4 の `(1U << TNUM_PRCID) - 1` から変更。
5. 検証 = F-1：Ruby `.trb` にも同時移植し `tools/cfg_equivalence.sh`
   （exit 0=一致 / 1=不一致 / **2=前提未充足であり合格ではない**）を主検査に維持。
6. CB はヒープ確保しない。予約 CB（named static + ポインタ表末尾）+ RAM inib 配列。
   free-list のリンクには **tmevtb 領域を転用**する（dcre cyclic.c:118-121 の技法）。
7. **free-list は FIFO**（`del_*` = `queue_insert_prev` で末尾へ / `acre_*` =
   `queue_delete_next` で先頭から）。これは段階1で**裁定済み**の設計であり、
   実装者・レビュアーとも再議しない。テストは FIFO/LIFO 不問で決定的になる形
   （「空きが1個だけの状態で del → 再 acre」）に組む。
8. 汎用層 `CMakeLists.txt`・`cmake/fmp3_core.cmake`・`cfg_py/pass1.py`・`cfg_py/pass2.py` は
   **変更しない**。
9. `rc=124` 単独を成功判定に使わない（期待出力の実在を `grep` で確認する）。
10. **`cmd | tail` / `cmd | grep` で成否判定しない。** パイプラインの `$?` は最後の要素のもの。
    ファイルへリダイレクトしてから `grep` するか、`${PIPESTATUS[0]}` を見る。
11. `tools/cfg_equivalence.sh` の **exit 2 は合格ではない**（前提未充足）。exit 0 のみ合格。
12. QEMU 実行は**プリセットごとに個別コマンド**で行い、ログを別ファイルに落とす。
    `for` ループで全構成を1コマンドに詰めると Bash ツールの 2 分タイムアウトに当たり、
    **qemu が孤児化する**（段階1 Task 7 の実害）。各実行後に `pgrep -a qemu` で残存 0 を確認する。

---

## ★spec からの訂正5件（Task 1 で spec に反映してから実装に入ること）

計画作成時に現物確認した結果、spec の記述と実装対象の現物が食い違う点が5件ある。
いずれも **Task 1 で spec 本文を直してから** 後続タスクに入る。

**訂正A：`T_NFYINFO` は FMP3 に存在しない。** spec §1.1 は「既存であることを確認する」と
書くが、`include/kernel.h` に `T_NFYINFO`・`T_NFY_HDR` 等の型は**一切無い**
（`TNFY_*`/`TENFY_*` の定数のみ `include/kernel.h:447-465` に存在）。
FMP3 は静的生成時に cfg が通知を関数合成して消し込むため、ランタイム型が要らなかった。
→ dcre `include/kernel.h:135-198` の型群（`T_NFY_HDR`〜`T_NFYINFO`）を移植する。
**移植先タスクは Task 6 ではなく Task 3**（理由は訂正Bを参照）。

**訂正B：cfg が `T_NFYINFO` を出力する以上、型の追加は cfg 変更と同じタスクに入れる。**
段階1 Task 6 で「cfg が出力した `COUNT_MB_T` が `include/kernel.h` に無くビルド不能」という
現行バグが**テストタスクになるまで発見されなかった**（`cfg_equivalence.sh` は両エンジンの
生成文字列を diff するだけでコンパイルしないため、**両エンジン同時の欠落を検出できない**）。
段階2の `T_NFYINFO _kernel_acyc_nfyinfo_table[N];` はこれと**完全に同型**の罠である。
→ Task 3 に「AID_CYC/AID_ALM > 0 の実構成を**実際にコンパイル・リンクする**」ステップを
必須で入れ、その前提として `T_NFYINFO` の追加も Task 3 に含める。

**訂正C：動的 cyc/alm の affinity は `(1U << TNUM_PRCID) - 1` ではなく `TOPPERS_TEPP_PRC`。**
FMP3 の cyc/alm は**時間イベント処理プロセッサ（`p_tevtcb != NULL`）にしか割り付けられない**。
静的生成側では `kernel/cyclic.py:65-68`（および `alarm.py:59-62`）が
「クラスの affinity が `TOPPERS_TEPP_PRC` に含まれないなら E_RSATR」と検査してこれを保証している。
動的生成で affinity を全プロセッサにすると、`msta_cyc(id, prcid)` の `CHECK_MIG` を通過した上で
`p_tevtcb == NULL` のプロセッサのイベントキューへ `tmevtb_enqueue_reltim` する経路ができてしまう。
→ `p_cycinib->affinity = (uint_t) TOPPERS_TEPP_PRC;`（alm 同様）。
`TOPPERS_TEPP_PRC` は `target_kernel.h` で定義され `include/kernel.h:69` 経由で
全カーネルソースから見える。**現行8プリセットではいずれも
`TOPPERS_TEPP_PRC == (1U << TNUM_PRCID) - 1`** なので観測上の挙動は spec の値と一致するが、
意味論として正しいのは `TOPPERS_TEPP_PRC` である（Task 1 で全プリセット分を確認する）。

**訂正D：`free-list` 転用は 64bit で `tmevtb.callback` を破壊する。**
`TMEVTB`（`kernel/tmevt.h:72-77`）は `{ EVTTIM evttim; uint_t index; CBACK callback; void *arg; }`。
`QUEUE`（`include/queue.h`）はポインタ2本。
- 32bit（musca_b1 / rp2350 / kria_r5 / polarfire RV32 相当）：`sizeof(QUEUE)==8`、
  `offsetof(TMEVTB, callback)==8` → **callback は無傷**。
- 64bit（kria_arm64）：`sizeof(QUEUE)==16`、`offsetof(TMEVTB, callback)==8` →
  **callback が QUEUE の p_prev に上書きされる**。

dcre は 32bit 前提で `acre_cyc` が callback/arg を再設定しないが、FMP3 は 64bit ターゲットを持つ。
→ **`acre_cyc`/`acre_alm` は free-list から pop した直後に `tmevtb.callback`/`tmevtb.arg` を
必ず再設定する**（全ビット幅で無条件に正しい。dcre からの意図的な逸脱として台帳に記録）。

**訂正E：`AID_CYC > 0` かつ静的 `CRE_CYC` が 0 個の構成は現行枠組みで壊れる。**
spec §2.2 は「共通枠組みの変更は不要」とするが、`kernel.py:177` /
`kernel.trb:186` のデータ構造生成ガードは `len(cfgData[self.api]) > 0`（＝**静的**個数）である。
タスクは静的0個が cfg エラーなので段階1では表面化しなかったが、cyc/alm は静的0個が正当な構成であり、
その場合に `p_cyccb_table` が `TOPPERS_EMPTY_LABEL` になり、CYCCB の実体も
`initialize_cyclic` の呼出し登録も生成されないまま `TNUM_CYCID` だけが 2 になる
（＝`free_cyccb` 未初期化のまま `acre_cyc` が走る沈黙した破壊）。dcre も同じ穴を持つ。
→ 共通枠組みに **「AID_* が 1 個以上なのに静的オブジェクトが 0 個なら cfg エラー」** を追加し、
エラー回帰 cfg で固定する（Task 3）。段階2の対象は cyc/alm だが、枠組み側の追加なので
段階3で他オブジェクトへ AID を足したときも自動的に効く。

---

## 変更ファイル一覧（全体像）

| 層 | ファイル | 種別 |
|---|---|---|
| spec | `docs/superpowers/specs/2026-08-04-fmp3-dcre-stage2-cyc-alm-design.md` | 派生・修正 |
| arch rename | `arch/arm_m_gcc/common/core_rename.def` →（再生成）`core_rename.h` `core_unrename.h` | **pristine・台帳** |
| cfg 定義 | `kernel/kernel_api.def` | **pristine・台帳** |
| cfg Python | `kernel/kernel.py` `kernel/cyclic.py` `kernel/alarm.py` | 派生 |
| cfg Ruby | `kernel/kernel.trb` `kernel/cyclic.trb` `kernel/alarm.trb` | **pristine・台帳** |
| API | `include/kernel.h`（T_NFYINFO 群 / T_CCYC / T_CALM / 4宣言） | **pristine・台帳** |
| カーネル | `kernel/check.h` `kernel/kernel_impl.h` `kernel/time_manage.c` `kernel/cyclic.h` `kernel/cyclic.c` `kernel/alarm.h` `kernel/alarm.c` `kernel/allfunc.h` `kernel/Makefile.kernel` | **pristine・台帳** |
| rename | `kernel/kernel_rename.def` →（再生成）`kernel_rename.h` `kernel_unrename.h` | **pristine・台帳** |
| テスト | `test/test_dcre2.{c,cfg,h}` `test/MANIFEST` `test/testexec.rb` | **pristine・台帳**（新規追加も記録） |
| エラー回帰 | `tools/cfg_error_tests/dcre_aid_cyc_in_class.cfg` `dcre_aid_alm_in_class.cfg` `dcre_aid_cyc_no_static.cfg` | 派生 |

新規 `.c` ファイルは**作らない**（`KERNEL_FCSRCS` 22個は不変。`acre_cyc`/`del_cyc` は既存
`kernel/cyclic.c`、`acre_alm`/`del_alm` は `kernel/alarm.c`、通知機構は `kernel/time_manage.c`）。

---

### Task 1: 実装前確認（spec §9 の8項目）と spec の訂正5件反映

**推奨モデル:** 最安価（現物確認と機械的な文書修正。判断は本計画が既に与えている）

**Files:**
- Modify: `docs/superpowers/specs/2026-08-04-fmp3-dcre-stage2-cyc-alm-design.md`
  （§1.1・§2.2・§3.1・§3.5・§5.2・Global Constraints 4）
- Create: なし

**Interfaces（後続 Task が参照する記録）:**
- Produces: 本 Task の**確認結果表**（spec 末尾 §10 として追記）。後続 Task は
  「Task 1 の記録」としてこれを参照する。特に
  (a) CYCID/ALMID マクロの要否判断、(b) `TFN_*` の既存値、(c) 全プリセットの
  `TOPPERS_TEPP_PRC` と PRC1 の `p_tevtcb`、(d) `check_nfyinfo`/`notify_handler` の所在。

- [ ] **Step 1: §9-1 `T_NFYINFO` の有無**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -n "T_NFYINFO\|T_NFY_HDR\|nfymode" include/kernel.h > /tmp/dcre2-t1-nfyinfo.txt
grep -c "TNFY_HANDLER\|TENFY_SETVAR" include/kernel.h
```
期待（計画作成時の実測）: `T_NFYINFO` 系の**型は0件**、`TNFY_*`/`TENFY_*` の**定数は存在**
（`include/kernel.h:447-465`）。→ **訂正A** のとおり型群を移植する必要があることを記録。

- [ ] **Step 2: §9-2 機能コードの有無**

```bash
grep -n "TFN_ACRE_CYC\|TFN_DEL_CYC\|TFN_ACRE_ALM\|TFN_DEL_ALM" include/kernel_fncode.h
```
期待（実測）: `TFN_ACRE_CYC (-202)` / `TFN_ACRE_ALM (-203)` / `TFN_DEL_CYC (-218)` /
`TFN_DEL_ALM (-219)` が**4件とも既存**。→ `include/kernel_fncode.h` の変更は**不要**。
この4値を確認結果表に転記する（Task 6 が参照する）。

- [ ] **Step 3: §9-3 CYCID/ALMID マクロの実装形態（2レンジ化の要否判断）**

```bash
grep -n "CYCID\|ALMID" kernel/cyclic.h kernel/alarm.h kernel/check.h kernel/kernel_impl.h
grep -n "p_cyccb_table\|p_almcb_table" kernel/cyclic.h kernel/alarm.h
```
期待（実測）: **FMP3 には `CYCID()`/`ALMID()` マクロが存在しない**（`acre_*` が無いため不要だった）。
CB は `CYCCB *const p_cyccb_table[]`（**ポインタ表**、`cyclic.h:93`）であり、
dcre のような `cyccb_table[]` 実体配列ではない。
→ **判断: dcre の `(p_cyccb - cyccb_table)` 方式は使えない。段階1 の TSKID と同型の
「inib ポインタによる2レンジ判定」で新規に定義する**（Task 5 が実装）。
この判断と根拠（ポインタ表であること）を確認結果表に明記する。

- [ ] **Step 4: §9-4 free-list 転用の成立条件（★訂正D の根拠）**

`kernel/tmevt.h:72-77` の `TMEVTB` と `include/queue.h` の `QUEUE` を読み、
32bit / 64bit それぞれの `sizeof(QUEUE)` と `offsetof(TMEVTB, callback)` を求める。
机上で終わらせず、**実測**する：

```bash
cat > /tmp/dcre2-t1-size.c <<'EOF'
#include <stddef.h>
#include <stdint.h>
typedef uint32_t EVTTIM;
typedef unsigned int uint_t;
typedef void (*CBACK)(void *, void *);
typedef struct { EVTTIM evttim; uint_t index; CBACK callback; void *arg; } TMEVTB;
typedef struct queue { struct queue *p_next; struct queue *p_prev; } QUEUE;
char buf[256];
int main(void) {
	__builtin_printf("sizeof(QUEUE)=%zu offsetof(callback)=%zu sizeof(TMEVTB)=%zu\n",
		sizeof(QUEUE), offsetof(TMEVTB, callback), sizeof(TMEVTB));
	return 0;
}
EOF
gcc -m32 -o /tmp/dcre2-size32 /tmp/dcre2-t1-size.c 2>/dev/null && /tmp/dcre2-size32
gcc -o /tmp/dcre2-size64 /tmp/dcre2-t1-size.c && /tmp/dcre2-size64
```
期待: 32bit → `sizeof(QUEUE)=8 offsetof(callback)=8`（callback 無傷）、
64bit → `sizeof(QUEUE)=16 offsetof(callback)=8`（**callback が上書きされる**）。
`-m32` が使えない環境なら 64bit のみで可（結論は 64bit 側で出る）。
→ **訂正D のとおり `acre_*` で callback/arg を毎回再設定する**方針を確認結果表に記録する。
（`sizeof(TMEVTB) >= sizeof(QUEUE)` 自体はどちらでも成立＝転用の前提は満たす。）

- [ ] **Step 5: §9-5 全8プリセットで PRC1 が `p_tevtcb` を持つこと（★設計の成立条件）**

`initialize_cyclic`（`kernel/cyclic.c:115-118`）は `p_my_pcb->p_tevtcb == NULL` の
プロセッサで即 return する。Constraint 4（iprcid=1 固定）が成立するには
**PRC1 が必ず時間イベント処理プロセッサ**でなければならない。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  echo "=== $p"
  grep -n "TOPPERS_TEPP_PRC" build/$p/generated/*.h build/$p/generated/*.c 2>/dev/null
  sed -n '/p_tevtcb_table\[/,/};/p' build/$p/generated/kernel_cfg.c
done > /tmp/dcre2-t1-tevtcb.txt 2>&1
grep -c "NULL" /tmp/dcre2-t1-tevtcb.txt
```
（`build/<preset>` が無ければ先に `cmake --preset <p> && cmake --build build/<p>` する。）

判定基準: **`p_tevtcb_table[]` の第1要素（PRC1）が `NULL` でないこと**を全8プリセットで確認する。
あわせて各ターゲットの `TOPPERS_TEPP_PRC` 定義（`target/*/target_kernel.h`）を読み、
bit0（PRC1）が必ず立っていること、および `(1U << TNUM_PRCID) - 1` と一致するかを表にする。

**★ここで PRC1 が `p_tevtcb` を持たない構成が1つでも見つかったら、Constraint 4 が
成立しないため設計が無効になる。その場合は以降のステップに進まず、
発見内容（どのプリセット・どの定義）を報告して停止すること。**

- [ ] **Step 6: §9-6 `check_nfyinfo`/`notify_handler` の所在と FMP3 での不在**

```bash
grep -rn "check_nfyinfo\|notify_handler" \
    /home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/kernel/ | sort
grep -rn "check_nfyinfo\|notify_handler" kernel/ include/
```
期待（実測）: dcre 側は **`kernel/time_manage.c:225`（`check_nfyinfo`、`TOPPERS_chknfy`）と
`kernel/time_manage.c:309`（`notify_handler`、`TOPPERS_nfyhdr`）**、宣言は
`kernel/kernel_impl.h:287,292`、rename は `kernel/kernel_rename.def:100-101`。
FMP3 側は **0件**。→ 転写元ファイルと関数区画名（`TOPPERS_chknfy`/`TOPPERS_nfyhdr`）を
確認結果表に記録する（Task 4 が参照する）。
あわせて `dcre/kernel/check.h:130-134` の **`INTPTR_NONNULL`** が FMP3 `kernel/check.h` に
**無い**ことを確認する（`grep -n "INTPTR_NONNULL" kernel/check.h` が0件）。
`check_nfyinfo` が使うため Task 4 で追加が要る。

- [ ] **Step 7: §9-7 `call_cyclic`/`call_alarm` の削除耐性（§5.2 の結論を出す）**

`kernel/cyclic.c` の `call_cyclic`（`TOPPERS_cyccal` 区画）と `kernel/alarm.c` の
`call_alarm`（`TOPPERS_almcal` 区画）を読み、**ハンドラ復帰後に CB を再参照する箇所**を列挙する。

期待（実測）: どちらも
`release_glock(); unlock_cpu(); LOG_*_ENTER(p_*cb); (*(nfyhdr))(exinf); LOG_*_LEAVE(p_*cb);`
のあと `sense_lock()` 判定 → `lock_cpu()`/`force_unlock_spin()` → `acquire_glock()` で
**戻るだけで、CB のフィールドは一切再参照しない**（`LOG_*` はデフォルト空マクロ）。
→ **結論: 「ハンドラ復帰後の CB 再参照」に起因する未防御窓は存在しない。**
残る唯一の窓は **ハンドラ呼出し式そのもの**
（`(*(p_cyccb->p_cycinib->nfyhdr))(p_cyccb->p_cycinib->exinf)`）が glock 解放後に
inib を読む点で、この読取り中に他コアが `del_cyc` + `acre_cyc` を完了させると
「削除済みオブジェクトの旧ハンドラではなく新ハンドラが呼ばれる」可能性がある。
これは**段階1 spec §2.3 の「自終了タスクのスタック残余ウィンドウ」と同種の受容済み窓**として
spec §5.2 に明文化する（ユーザが del 直後に acre する競合を自ら書いた場合のみ到達）。
dcre 側の `call_cyclic` に FMP3 に無い再判定分岐が無いことも同時に確認する（無ければ補わない）。

- [ ] **Step 8: §9-8 AID_* のクラス外専用検査が cyc/alm でも共通枠組みで発火すること**

`kernel/kernel.py:127-146` / `kernel/kernel.trb:130-149` の `has_aid` ブロックを読み、
`self.aidapi`（`AID_CYC`/`AID_ALM`）が `kernel_api.def` に登録されれば
`error_ercd("E_RSATR", ...)` が**オブジェクト種別に依らず**発火する構造であることを確認する。
あわせて **★訂正E** の根拠を現物で確認する：

```bash
sed -n '175,200p' kernel/kernel.py     # データ構造ガード len(cfgData[self.api]) > 0
sed -n '184,200p' kernel/kernel.trb
```
期待: ガードが**静的個数のみ**で、`numAutoObjid` を見ていないこと。
→ 訂正E のとおり Task 3 で「AID>0 かつ静的0個は cfg エラー」を追加する必要があることを記録。

- [ ] **Step 9: spec への反映（訂正5件）**
  - §1.1 の「`T_NFYINFO` は既存であることを確認する」を **訂正A**（存在しない・
    dcre `include/kernel.h:135-198` から移植・追加タスクは Task 3）に書き換える。
  - §1.2 の「機能コードの既存有無を確認」を Step 2 の実測値4件で確定させる。
  - Global Constraints 4 と §3.3 の affinity 式を **訂正C**（`TOPPERS_TEPP_PRC`）に書き換え、
    根拠（`cyclic.py:65-68` の静的側検査）を1文添える。
  - §3.1 に **訂正D**（64bit で callback が上書きされる／`acre_*` で再設定する）を追記する。
  - §2.2 に **訂正E**（静的0個の穴と cfg エラー追加）を追記する。
  - §3.5 を Step 3 の結論（**マクロは存在しないので新規に2レンジ版を定義する**）で確定させる。
  - §5.2 を Step 7 の結論（**復帰後再参照は無い／残る窓はハンドラ呼出し式のみ・受容**）で確定させる。
  - spec 末尾に `## 10. 実装前確認の結果（YYYY-MM-DD 実測）` を新設し、Step 1-8 の
    確認結果を**表**で記録する（後続 Task はここを参照する）。
- [ ] **Step 10: コミット**

```bash
git add docs/superpowers/specs/2026-08-04-fmp3-dcre-stage2-cyc-alm-design.md
git commit -m "docs(spec): dcre段階2の実装前確認と訂正5件（T_NFYINFO不在・TEPP_PRC・64bit callback上書き・静的0個・CYCID新設）"
```

---

### Task 2: arm_m `core_rename.def` の欠落修正（sense_lock / unlock_cpu）

**推奨モデル:** 中位（sonnet）。生成物の差分を厳密に見る判断が要る。

**Files:**
- Modify: `arch/arm_m_gcc/common/core_rename.def`（節を1つ追加）
- Modify（再生成）: `arch/arm_m_gcc/common/core_rename.h` `arch/arm_m_gcc/common/core_unrename.h`
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Produces: `_kernel_sense_lock` / `_kernel_unlock_cpu` のリネーム定義。
  Task 7 の test_dcre2 は musca_b1-2core 上のテストハーネスを使うため、
  本 Task が閉じるハーネス経路に依存する。
- Consumes: なし（独立）。

**背景（段階1 Task 6 のブロッカー）:** `kernel/interrupt.py:433-435` /
`kernel/interrupt.trb:462-464` は、多重 ISR 連鎖を含む構成の割込みハンドラを生成するとき
`_kernel_sense_lock()` / `_kernel_unlock_cpu()` を**リネーム後の名前で直書き**する。
arm64 の `core_rename.def` は `# core_kernel_impl.h` 節に `lock_cpu` `unlock_cpu` `sense_lock` を
持つが、**arm_m の `core_rename.def` には該当節が丸ごと無い**ため、musca_b1 では
これらのシンボルが未定義になり test_int2 がリンクできない。pristine 由来の既存ギャップで、
段階1 でコントローラが `main` ブランチでも再現確認済み（上流報告候補 b）。

- [ ] **Step 1: 欠落の再現（修正前の失敗を実物で見る）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --preset musca_b1-2core -B build/musca_b1-2core-tint2 \
  -DFMP3_APPLNAME=test_int2 -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/dcre2-t2-conf.log 2>&1
echo "configure rc=$?"
cmake --build build/musca_b1-2core-tint2 > /tmp/dcre2-t2-before.log 2>&1
echo "build rc=$?"
grep -n "undefined reference to ._kernel_sense_lock\|undefined reference to ._kernel_unlock_cpu" \
     /tmp/dcre2-t2-before.log
```
期待: build rc≠0 かつ `_kernel_sense_lock` / `_kernel_unlock_cpu` の未定義参照が実在。
**未定義シンボルの一覧をそのまま記録する**（`_kernel_lock_cpu` も出るなら Step 2 に足す）。

- [ ] **Step 2: 修正前の生成ヘッダを退避（negative control の比較元）**

```bash
cp arch/arm_m_gcc/common/core_rename.h   /tmp/dcre2-t2-core_rename.h.orig
cp arch/arm_m_gcc/common/core_unrename.h /tmp/dcre2-t2-core_unrename.h.orig
```

- [ ] **Step 3: `arch/arm_m_gcc/common/core_rename.def` に節を追加**

arm64 版（`arch/arm64_gcc/common/core_rename.def` の `# core_kernel_impl.h` 節）と
同じ形式で、**ファイル先頭の `# core_kernel_impl.c` 節の直前**に次を挿入する：

```
# core_kernel_impl.h
sense_lock
unlock_cpu

```
（Step 1 で `_kernel_lock_cpu` の未定義参照も出た場合に限り `lock_cpu` を1行足す。
出ていないなら**足さない** — 最小差分を保つ。）

- [ ] **Step 4: 再生成**

```bash
cd arch/arm_m_gcc/common && ruby ../../../utils/genrename.rb core && cd -
```
（`core_rename.h:1` に「generated from core_rename.def by genrename」とある通り手書き禁止。
`genrename.rb` は `<name>_rename.def` を cwd から読む＝`utils/genrename.rb:96`。）

- [ ] **Step 5: ★negative control — 追加した2エントリ以外の差分が無いこと**

```bash
diff -u /tmp/dcre2-t2-core_rename.h.orig   arch/arm_m_gcc/common/core_rename.h   > /tmp/dcre2-t2-diff-r.txt
diff -u /tmp/dcre2-t2-core_unrename.h.orig arch/arm_m_gcc/common/core_unrename.h > /tmp/dcre2-t2-diff-u.txt
grep -c '^+[^+]' /tmp/dcre2-t2-diff-r.txt   # 期待: 2（sense_lock, unlock_cpu）
grep -c '^-[^-]' /tmp/dcre2-t2-diff-r.txt   # 期待: 0
grep -c '^+[^+]' /tmp/dcre2-t2-diff-u.txt   # 期待: 2
grep -c '^-[^-]' /tmp/dcre2-t2-diff-u.txt   # 期待: 0
cat /tmp/dcre2-t2-diff-r.txt /tmp/dcre2-t2-diff-u.txt
```
期待: 追加行は
`#define sense_lock _kernel_sense_lock` / `#define unlock_cpu _kernel_unlock_cpu`
（および unrename 側の `#undef` 2行）**のみ**。
**削除行が1行でもあれば不合格**（genrename の実行位置ミス等を疑う）。
`lock_cpu` を足した場合は期待値を 3 / 0 に読み替える。

- [ ] **Step 6: test_int2 がビルドでき、QEMU で PASS に到達すること**

```bash
cmake --build build/musca_b1-2core-tint2 > /tmp/dcre2-t2-after.log 2>&1
echo "build rc=$?"                       # 期待: 0
timeout -k 5 40 cmake --build build/musca_b1-2core-tint2 --target run \
  > /tmp/dcre2-t2-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre2-t2-run.log
pgrep -a qemu                            # 期待: 何も出ない（孤児 0）
```
期待: build rc=0、`TTSP_RESULT: PASS` 行が**実在**（rc は見ない — Constraint 9）。
**これで段階1 Task 6 が閉じられなかったハーネス経路（musca_b1-2core）が閉じる。**
`pgrep` が qemu を出したら `pkill -f qemu-system-arm` で掃除してから次へ進む。

- [ ] **Step 7: 既存構成の無回帰（arm_m を使う4プリセット）**

```bash
for p in musca_b1 musca_b1-2core rp2350_pico2; do
  cmake --build build/$p > /tmp/dcre2-t2-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/dcre2-t2-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
```
期待: 全て rc=0（`RESULT = MATCH`）。**exit=2 は不合格**（Constraint 11）。

- [ ] **Step 8: 台帳とコミット** — `DIVERGENCE_MAP.md` に3行
  （`arch/arm_m_gcc/common/core_rename.def` / `core_rename.h` / `core_unrename.h`、
  種別 `mod (upstream-gap-fix)`、理由「cfg 生成コードが参照する `_kernel_sense_lock`/
  `_kernel_unlock_cpu` が未リネームで多重ISR構成がリンク不能」、
  **上流報告欄 = `報告候補（段階1最終レビュー 上流報告候補 b）`**）を追記して：

```bash
git add -A && git commit -m "fix(arch): arm_m core_rename.def に sense_lock/unlock_cpu を追加（pristine既存ギャップ、test_int2 が musca_b1 でリンク可能に）"
```

---

### Task 3: cfg 両エンジン — AID_CYC / AID_ALM と nfyinfo テーブル

**推奨モデル:** 中位（sonnet）。テンプレート2言語の対応関係を保つ判断が要る。

**Files:**
- Modify: `kernel/kernel_api.def`（2行追加・pristine）
- Modify: `kernel/cyclic.py` `kernel/alarm.py`（`__init__` に1行ずつ）
- Modify: `kernel/cyclic.trb` `kernel/alarm.trb`（同・pristine）
- Modify: `kernel/kernel.py` `kernel/kernel.trb`（訂正E のガード追加・後者 pristine）
- Modify: `include/kernel.h`（**訂正A/B**: `T_NFY_HDR`〜`T_NFYINFO` を追加・pristine）
- Create: `tools/cfg_error_tests/dcre_aid_cyc_in_class.cfg`
  `tools/cfg_error_tests/dcre_aid_alm_in_class.cfg`
  `tools/cfg_error_tests/dcre_aid_cyc_no_static.cfg`
- Modify: `DIVERGENCE_MAP.md`

**Interfaces（後続 Task が依存する生成物）:**
- Produces（kernel_cfg.c に恒常出力）:
  `#define TNUM_SCYCID <n>` / `const ID _kernel_tmax_scycid`、
  `CYCINIB _kernel_acycinib_table[N]`（N=0 時 `TOPPERS_EMPTY_LABEL(CYCINIB, _kernel_acycinib_table);`）、
  `T_NFYINFO _kernel_acyc_nfyinfo_table[N]`（N=0 時 EMPTY_LABEL）、
  予約 CYCCB `static CYCCB _kernel_acyccb_<i>;`（i=1..N）と `_kernel_p_cyccb_table` 末尾への追加。
  alm 側は `TNUM_SALMID` / `_kernel_tmax_salmid` / `_kernel_aalminib_table` /
  `_kernel_aalm_nfyinfo_table` / `_kernel_aalmcb_<i>` / `_kernel_p_almcb_table`。
  `TNUM_CYCID`/`TNUM_ALMID` は**総数**（静的+AID）へ意味変更。
- Produces（`include/kernel.h`）: `T_NFY_HDR` `T_NFY_VAR` `T_NFY_IVAR` `T_NFY_TSK`
  `T_NFY_SEM` `T_NFY_FLG` `T_NFY_DTQ` `T_ENFY_VAR` `T_ENFY_DTQ` `T_NFYINFO`
- Consumes: Task 1 の確認結果表（訂正A/E の確定）。

- [ ] **Step 1: 基準生成物の保存（管理された差分の比較元）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core > /tmp/dcre2-t3-base-build.log 2>&1; echo "rc=$?"
rm -rf /tmp/dcre2-base-generated
cp -r build/musca_b1-2core/generated /tmp/dcre2-base-generated
```

- [ ] **Step 2: `kernel/kernel_api.def` に dcre と同一の2行を追加**（ファイル末尾、`DEF_MPK` の次）

```
AID_CYC .nocyc
AID_ALM .noalm
```

- [ ] **Step 3: `include/kernel.h` に `T_NFYINFO` 型群を追加（★訂正A/B）**

`MPF_T` の typedef（`include/kernel.h:131`）の**直後**、`T_CTSK`（`:136`）の**手前**に、
dcre `extension/dcre/include/kernel.h:133-198` を**そのまま**転記する：

```c
/*
 *  タイムイベントの通知方法のパケット形式の定義
 */
typedef struct {
	EXINF		exinf;		/* タイムイベントハンドラの拡張情報 */
	TMEHDR		tmehdr;		/* タイムイベントハンドラの先頭番地 */
} T_NFY_HDR;

typedef struct {
	intptr_t	*p_var;		/* 変数の番地 */
	intptr_t	value;		/* 設定する値 */
} T_NFY_VAR;

typedef struct {
	intptr_t	*p_var;		/* 変数の番地 */
} T_NFY_IVAR;

typedef struct {
	ID			tskid;		/* タスクID */
} T_NFY_TSK;

typedef struct {
	ID			semid;		/* セマフォID */
} T_NFY_SEM;

typedef struct {
	ID			flgid;		/* イベントフラグID */
	FLGPTN		flgptn;		/* セットするビットパターン */
} T_NFY_FLG;

typedef struct {
	ID			dtqid;		/* データキューID */
	intptr_t	data;		/* 送信する値 */
} T_NFY_DTQ;

typedef struct {
	intptr_t	*p_var;		/* 変数の番地 */
} T_ENFY_VAR;

typedef struct {
	ID			dtqid;		/* データキューID */
} T_ENFY_DTQ;

typedef struct {
	MODE	nfymode;			/* 通知処理モード */
	union {						/* タイムイベントの通知に関する付随情報 */
		T_NFY_HDR	handler;
		T_NFY_VAR	setvar;
		T_NFY_IVAR	incvar;
		T_NFY_TSK	acttsk;
		T_NFY_TSK	wuptsk;
		T_NFY_SEM	sigsem;
		T_NFY_FLG	setflg;
		T_NFY_DTQ	snddtq;
	} nfy;
	union {						/* エラーの通知に関する付随情報 */
		T_ENFY_VAR	setvar;
		T_NFY_IVAR	incvar;
		T_NFY_TSK	acttsk;
		T_NFY_TSK	wuptsk;
		T_NFY_SEM	sigsem;
		T_NFY_FLG	setflg;
		T_ENFY_DTQ	snddtq;
	} enfy;
} T_NFYINFO;
```
（`TMEHDR` は `include/kernel.h:113`、`FLGPTN` は `:104`、`MODE` は `include/t_stddef.h:105` に既存。
`intptr_t` は `t_stddef.h` 経由で入る。追加の include は不要。）

- [ ] **Step 4: `kernel/cyclic.py` / `kernel/alarm.py` に nfyinfo テーブルを登録**

dcre `cyclic.trb:48-51` / `alarm.trb:48-51` と同じく、`__init__` に1行足すだけでよい
（段階1 が `self.inibList` を共通枠組みにしたため、出力側の変更は不要）。

`kernel/cyclic.py`：
```python
class CyclicObject(KernelObject):
    def __init__(self):
        super().__init__("cyc", "cyclic")
        self.inibList["T_NFYINFO"] = "acyc_nfyinfo_table"
```

`kernel/alarm.py`：
```python
class AlarmObject(KernelObject):
    def __init__(self):
        super().__init__("alm", "alarm")
        self.inibList["T_NFYINFO"] = "aalm_nfyinfo_table"
```

**静的 inib のサイズトークン `TNUM_CYCID` → `TNUM_SCYCID` は共通枠組み側で
自動的に切り替わる**（`kernel.py:174` の `inibSizeToken`。`has_aid` が真になるため）。
cyclic.py/alarm.py 側にサイズトークンは書かれていないので**触らない**。

- [ ] **Step 5: Ruby 側（オラクル）へ同一変更**

`kernel/cyclic.trb`：
```ruby
class CyclicObject < KernelObject
  def initialize()
    super("cyc", "cyclic")
    @inibList["T_NFYINFO"] = "acyc_nfyinfo_table"
  end
```

`kernel/alarm.trb`：
```ruby
class AlarmObject < KernelObject
  def initialize()
    super("alm", "alarm")
    @inibList["T_NFYINFO"] = "aalm_nfyinfo_table"
  end
```

- [ ] **Step 6: ★訂正E — 「AID>0 かつ静的0個」を cfg エラーにする**

`kernel/kernel.py` の `generate()` 内、`numObjid` を求めた**直後**（`kernel.py:146` 相当）に：

```python
        # AID_xxx が 1 個以上あるのに静的オブジェクトが 0 個の構成は、
        # データ構造生成ガード（下記 len(cfgData[self.api]) > 0）により
        # CB 実体もアクセステーブルも初期化関数登録も生成されないまま
        # TNUM_xxxID だけが増える（free-list 未初期化のまま acre される）。
        # 段階2 ではこれを cfg エラーとして弾く（dcre も同じ穴を持つ）。
        if numAutoObjid > 0 and len(cfgData[self.api]) == 0:
            for _, params in cfgData[self.aidapi].items():
                error_ercd("E_OBJ", params,
                           f"{self.aidapi} requires at least one "
                           f"{self.api} in the system")
```

`kernel/kernel.trb` の対応位置（`kernel.trb:149` の `numObjid` 算出直後）に同義の Ruby：

```ruby
    # AID_xxxが1個以上あるのに静的オブジェクトが0個の構成は，データ構造
    # 生成ガード（下記$cfgData[@api].size > 0）によりCB実体もアクセス
    # テーブルも初期化関数登録も生成されないままTNUM_xxxIDだけが増える
    # （free-list未初期化のままacreされる）．段階2ではcfgエラーで弾く．
    if numAutoObjid > 0 && $cfgData[@api].size == 0
      $cfgData[@aidapi].each do |_, params|
        error_ercd("E_OBJ", params,
                   "#{@aidapi} requires at least one #{@api} in the system")
      end
    end
```
**両エンジンで文言を完全に一致させること**（`cfg_error_tests/run.sh` はメッセージ文言も突き合わせる）。

- [ ] **Step 7: 全8構成のビルドと等価性**

```bash
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --preset $p > /tmp/dcre2-t3-conf-$p.log 2>&1 || { echo "$p CONF FAIL"; continue; }
  cmake --build build/$p > /tmp/dcre2-t3-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/dcre2-t3-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
```
期待: すべて rc=0（`RESULT = MATCH`）。**exit=2 は不合格**として原因を調べる。

- [ ] **Step 8: 管理された差分の検査（AID 無し構成の生成物）**

```bash
diff -u /tmp/dcre2-base-generated/kernel_cfg.h build/musca_b1-2core/generated/kernel_cfg.h
diff -u /tmp/dcre2-base-generated/kernel_cfg.c build/musca_b1-2core/generated/kernel_cfg.c \
     > /tmp/dcre2-t3-managed-diff.txt; cat /tmp/dcre2-t3-managed-diff.txt
```
期待: `kernel_cfg.h` は**差分なし**。`kernel_cfg.c` の差分が次の**許容リストと完全一致**すること
（1件でも余分な差分があれば不合格）：
1. Cyclic ブロック: `#define TNUM_SCYCID` 行と `const ID _kernel_tmax_scycid` 行の追加
   （tmax ブロックの空行位置変化を含む）
2. `_kernel_cycinib_table` のサイズトークン `[TNUM_CYCID]` → `[TNUM_SCYCID]`
3. `TOPPERS_EMPTY_LABEL(CYCINIB, _kernel_acycinib_table);` の追加
4. `TOPPERS_EMPTY_LABEL(T_NFYINFO, _kernel_acyc_nfyinfo_table);` の追加
5. Alarm ブロック: 1〜4 の alm 版
   （`TNUM_SALMID` / `_kernel_tmax_salmid` / `[TNUM_SALMID]` /
   `TOPPERS_EMPTY_LABEL(ALMINIB, _kernel_aalminib_table);` /
   `TOPPERS_EMPTY_LABEL(T_NFYINFO, _kernel_aalm_nfyinfo_table);`）

`_kernel_atinib_table`（段階1 の tsk 分）や mempool ブロックに**変化が無いこと**も
同じ diff 上で確認する（Task 3 が tsk 側へ波及していない証拠）。

- [ ] **Step 9: positive control — AID 有り構成で出力が実際に変わり、両エンジンでバイト一致**

`sample/sample1.cfg` は `CRE_CYC`/`CRE_ALM` を既に含む（`sample1.cfg:24-25` ほか）ので、
**一時的に2行追記**して AID 有り実構成を作る：

```bash
cp sample/sample1.cfg /tmp/dcre2-sample1.cfg.orig
printf '\nAID_CYC(2);\nAID_ALM(2);\n' >> sample/sample1.cfg
cmake --build build/musca_b1-2core > /tmp/dcre2-t3-aid-build.log 2>&1; echo "build rc=$?"
grep -n "TNUM_SCYCID\|_kernel_acycinib_table\|_kernel_acyc_nfyinfo_table\|_kernel_acyccb_" \
     build/musca_b1-2core/generated/kernel_cfg.c
grep -n "TNUM_SALMID\|_kernel_aalminib_table\|_kernel_aalm_nfyinfo_table\|_kernel_aalmcb_" \
     build/musca_b1-2core/generated/kernel_cfg.c
grep -n "define TNUM_CYCID\|define TNUM_ALMID" build/musca_b1-2core/generated/kernel_cfg.h
tools/cfg_equivalence.sh build/musca_b1-2core > /tmp/dcre2-t3-aid-eq.log 2>&1; echo "eq rc=$?"
```
期待:
- `CYCINIB _kernel_acycinib_table[2];` と `T_NFYINFO _kernel_acyc_nfyinfo_table[2];`
  （EMPTY_LABEL ではなく実体）が出る。alm も同様。
- `static CYCCB _kernel_acyccb_1;` `_kernel_acyccb_2;` と `_kernel_p_cyccb_table` 末尾への
  `&_kernel_acyccb_1` `&_kernel_acyccb_2` 追加。alm も同様。
- `TNUM_CYCID` が「静的個数 + 2」、`TNUM_SCYCID` が静的個数。
- `cfg_equivalence.sh` rc=0（**Ruby/Python がバイト一致**）。

- [ ] **Step 10: ★★compile-through control（段階1 COUNT_MB_T 事故の再発防止）**

Step 9 の `build rc=0` は**単なる cfg 生成の成功ではなく、`kernel_cfg.c` の
実コンパイルとリンクの成功**でなければならない。`cfg_equivalence.sh` は両エンジンの
**生成文字列を diff するだけでコンパイルしない**ため、
「両エンジンが同じように未定義の型/マクロを出力する」欠陥を**構造的に検出できない**
（段階1 Task 6 の `COUNT_MB_T`/`ROUND_MB_T` 未定義がこの型）。

```bash
grep -c "T_NFYINFO" build/musca_b1-2core/generated/kernel_cfg.c   # 期待: 2 以上
ls -l build/musca_b1-2core/generated/kernel_cfg.c
# ★ 生成物ではなくオブジェクトの実在で確認する
find build/musca_b1-2core -name 'kernel_cfg.c.obj' -newer sample/sample1.cfg -print
```
期待: `kernel_cfg.c.obj` が **AID 追記後のタイムスタンプで実在**すること
（＝`T_NFYINFO` を含む `kernel_cfg.c` が実際にコンパイルされた）。
Step 3 で `T_NFYINFO` を追加していなければここで**必ずコンパイルエラーになる** —
それが本ステップの存在理由である。

さらに**64bit ターゲットでも**同じ確認を行う（型サイズ差の早期検出）：

```bash
cmake --build build/kria_arm64 > /tmp/dcre2-t3-aid-build-arm64.log 2>&1; echo "rc=$?"
tools/cfg_equivalence.sh build/kria_arm64 > /tmp/dcre2-t3-aid-eq-arm64.log 2>&1; echo "eq rc=$?"
```
期待: 両方 rc=0。

- [ ] **Step 11: negative control（検査が壊れていないことの実演）**

`kernel/cyclic.py` の `self.inibList["T_NFYINFO"] = "acyc_nfyinfo_table"` を
**一時的にコメントアウト**（Python 側だけ壊す）して：

```bash
cmake --build build/musca_b1-2core > /dev/null 2>&1
tools/cfg_equivalence.sh build/musca_b1-2core > /tmp/dcre2-t3-neg.log 2>&1; echo "rc=$?"
```
期待: **rc=1（MISMATCH）**。rc=0 なら等価性検査が空虚である。rc=2 も不合格（前提未充足）。
確認後、コメントアウトを**復元**して rc=0 に戻ることを再確認する。

- [ ] **Step 12: sample1.cfg を復元し、基準状態に戻す**

```bash
cp /tmp/dcre2-sample1.cfg.orig sample/sample1.cfg
git diff --stat sample/sample1.cfg     # 期待: 差分なし（出力が空）
cmake --build build/musca_b1-2core > /tmp/dcre2-t3-restore.log 2>&1; echo "rc=$?"
diff -u /tmp/dcre2-t3-managed-diff.txt <(diff -u /tmp/dcre2-base-generated/kernel_cfg.c \
        build/musca_b1-2core/generated/kernel_cfg.c) && echo "managed diff reproduced"
```
期待: `sample/sample1.cfg` に差分が残っていないこと、管理された差分が Step 8 と一致すること。

- [ ] **Step 13: エラー回帰ケース3件の追加**

`tools/cfg_error_tests/dcre_aid_cyc_in_class.cfg`：
```c
/*  AID_CYC をクラスの囲みの中に書くと E_RSATR（クラス外専用 API）  */
INCLUDE("test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CRE_CYC(CYC1, { TA_NULL, { TNFY_HANDLER, 0, task1 }, 1000000, 0 });
	AID_CYC(1);
}
```

`tools/cfg_error_tests/dcre_aid_alm_in_class.cfg`：
```c
/*  AID_ALM をクラスの囲みの中に書くと E_RSATR（クラス外専用 API）  */
INCLUDE("test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CRE_ALM(ALM1, { TA_NULL, { TNFY_HANDLER, 0, task1 } });
	AID_ALM(1);
}
```

`tools/cfg_error_tests/dcre_aid_cyc_no_static.cfg`（★訂正E の固定）：
```c
/*  静的な CRE_CYC が1個も無いのに AID_CYC を書くと E_OBJ  */
INCLUDE("test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
}
AID_CYC(2);
```

```bash
tools/cfg_error_tests/run.sh build/musca_b1-2core \
    tools/cfg_error_tests/dcre_aid_cyc_in_class.cfg E_RSATR; echo "rc=$?"
tools/cfg_error_tests/run.sh build/musca_b1-2core \
    tools/cfg_error_tests/dcre_aid_alm_in_class.cfg E_RSATR; echo "rc=$?"
tools/cfg_error_tests/run.sh build/musca_b1-2core \
    tools/cfg_error_tests/dcre_aid_cyc_no_static.cfg E_OBJ; echo "rc=$?"
```
期待: 3件とも rc=0（両エンジンが同じ ercd を同じ文言で検出）。
`CRE_CYC` の par1（exinf）に `task1` を渡しているのは、**エラーに到達する前に
他のエラーで落ちない**ようにするための便宜であり、意味は問わない
（TNFY_HANDLER の par2 がハンドラ）。cfg が別のエラーを先に出すようなら、
`test_int2.h` に既にある関数名へ差し替えて**目的のエラーだけが出る**最小形に調整すること。

- [ ] **Step 14: 台帳とコミット** — `DIVERGENCE_MAP.md` に
  `kernel/kernel_api.def` / `kernel/kernel.trb` / `kernel/cyclic.trb` / `kernel/alarm.trb` /
  `include/kernel.h` の5行（種別 `mod (dcre-port)`）を追記して：

```bash
git add -A && git commit -m "feat(cfg): AID_CYC/AID_ALM と T_NFYINFO を両エンジンへ追加（dcre段階2）"
```

---

### Task 4: ランタイム通知機構（check_nfyinfo / notify_handler）

**推奨モデル:** 中位（sonnet）

**Files:**
- Modify: `kernel/check.h`（`INTPTR_NONNULL` 追加。`FUNC_NONNULL` ブロックの直後）
- Modify: `kernel/kernel_impl.h`（`NFYHDR` typedef の直後に2宣言）
- Modify: `kernel/time_manage.c`（`TOPPERS_chknfy` / `TOPPERS_nfyhdr` 区画を末尾に追加）
- Modify: `kernel/allfunc.h`（time_manage.c 節に2行）
- Modify: `kernel/Makefile.kernel`（`time_manage =` 行に2個追記）
- Modify: `kernel/kernel_rename.def` →（再生成）`kernel_rename.h` `kernel_unrename.h`
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Consumes: Task 1 の確認結果表（転写元 = dcre `kernel/time_manage.c:225`/`:309`、
  区画名 `TOPPERS_chknfy`/`TOPPERS_nfyhdr`）、Task 3 の `T_NFYINFO`（`include/kernel.h`）
- Produces: `ER check_nfyinfo(const T_NFYINFO *)` / `void notify_handler(EXINF)` /
  `INTPTR_NONNULL(p_var)`。Task 6 の `acre_cyc`/`acre_alm` がこの2関数を使う。

**この Task の挙動は不変である**（まだ誰も呼ばない）。全構成がビルドできること＝合格。

- [ ] **Step 1: `kernel/check.h` に `INTPTR_NONNULL` を追加**

`FUNC_NONNULL` ブロック（`kernel/check.h:423-427`）の**直後**、`#endif /* TOPPERS_CHECK_H */` の
手前に、dcre `extension/dcre/kernel/check.h:130-134` をそのまま転記する：

```c
#ifdef CHECK_INTPTR_NONNULL
#define INTPTR_NONNULL(p_var)	((p_var) != NULL)
#else /* CHECK_INTPTR_NONNULL */
#define INTPTR_NONNULL(p_var)	true
#endif /* CHECK_INTPTR_NONNULL */
```
（`CHECK_INTPTR_NONNULL` はどのターゲットも定義していないため実質 `true` に落ちる。
`INTPTR_ALIGN`（`check.h:390-394`）は段階1 Task 3 で移植済みで、**こちらだけ漏れていた**。）

- [ ] **Step 2: `kernel/kernel_impl.h` に2宣言を追加**

`NFYHDR` の typedef（`kernel/kernel_impl.h:357-359`）の**直後**、
`#endif /* TOPPERS_MACRO_ONLY */` の手前に：

```c
/*
 *  通知方法のエラーチェック（time_manage.c）
 */
extern ER		check_nfyinfo(const T_NFYINFO *p_nfyinfo);

/*
 *  通知ハンドラ（time_manage.c）
 *
 *  exinfとして渡されたT_NFYINFOに従い，変数設定・タスク起動等の通知
 *  処理を行うトランポリン．動的生成されたcyc/almのうち通知方法が
 *  TNFY_HANDLER以外のものは，nfyhdrとしてこの関数を登録する．
 */
extern void		notify_handler(EXINF exinf);
```

- [ ] **Step 3: `kernel/time_manage.c` の末尾に2区画を転写**

dcre `extension/dcre/kernel/time_manage.c` の `TOPPERS_chknfy` 区画（`:220-296`）と
`TOPPERS_nfyhdr` 区画（`:302-375`）を、**現行 dcre ソースから**（DIFF ファイルではない）
`kernel/time_manage.c` の末尾（`TOPPERS_fch_hrt` 区画の `#endif` の後）へ**そのまま**転記する。
FMP3 側の適応は**不要**（内部で使う `CHECK_PAR`/`CHECK_ID`/`VALID_*`/`act_tsk`/`wup_tsk`/
`sig_sem`/`set_flg`/`psnd_dtq`/`loc_cpu`/`unl_cpu` はいずれも FMP3 に存在する）。
転写後の全文は次の形になる（★転記の正否は Step 6 の diff で機械的に確認する）：

```c
/*
 *  通知方法のエラーチェック
 */
#ifdef TOPPERS_chknfy

ER
check_nfyinfo(const T_NFYINFO *p_nfyinfo)
{
	ER		ercd;

	if (p_nfyinfo->nfymode == TNFY_HANDLER) {
		CHECK_PAR(FUNC_ALIGN(p_nfyinfo->nfy.handler.tmehdr));
		CHECK_PAR(FUNC_NONNULL(p_nfyinfo->nfy.handler.tmehdr));
	}
	else {
		switch (p_nfyinfo->nfymode & 0x0fU) {
		case TNFY_SETVAR:
			CHECK_PAR((p_nfyinfo->nfymode & ~0x0fU) == 0);
			CHECK_PAR(INTPTR_ALIGN(p_nfyinfo->nfy.setvar.p_var));
			CHECK_PAR(INTPTR_NONNULL(p_nfyinfo->nfy.setvar.p_var));
			break;
		case TNFY_INCVAR:
			CHECK_PAR((p_nfyinfo->nfymode & ~0x0fU) == 0);
			CHECK_PAR(INTPTR_ALIGN(p_nfyinfo->nfy.incvar.p_var));
			CHECK_PAR(INTPTR_NONNULL(p_nfyinfo->nfy.incvar.p_var));
			break;
		case TNFY_ACTTSK:
			CHECK_ID(VALID_TSKID(p_nfyinfo->nfy.acttsk.tskid));
			break;
		case TNFY_WUPTSK:
			CHECK_ID(VALID_TSKID(p_nfyinfo->nfy.wuptsk.tskid));
			break;
		case TNFY_SIGSEM:
			CHECK_ID(VALID_SEMID(p_nfyinfo->nfy.sigsem.semid));
			break;
		case TNFY_SETFLG:
			CHECK_ID(VALID_FLGID(p_nfyinfo->nfy.setflg.flgid));
			break;
		case TNFY_SNDDTQ:
			CHECK_ID(VALID_DTQID(p_nfyinfo->nfy.snddtq.dtqid));
			break;
		default:
			CHECK_PAR(false);
			break;
		}
		switch (p_nfyinfo->nfymode & ~0x0fU) {
		case 0:
			break;
		case TENFY_SETVAR:
			CHECK_PAR(INTPTR_ALIGN(p_nfyinfo->enfy.setvar.p_var));
			CHECK_PAR(INTPTR_NONNULL(p_nfyinfo->enfy.setvar.p_var));
			break;
		case TENFY_INCVAR:
			CHECK_PAR(INTPTR_ALIGN(p_nfyinfo->enfy.incvar.p_var));
			CHECK_PAR(INTPTR_NONNULL(p_nfyinfo->enfy.incvar.p_var));
			break;
		case TENFY_ACTTSK:
			CHECK_ID(VALID_TSKID(p_nfyinfo->enfy.acttsk.tskid));
			break;
		case TENFY_WUPTSK:
			CHECK_ID(VALID_TSKID(p_nfyinfo->enfy.wuptsk.tskid));
			break;
		case TENFY_SIGSEM:
			CHECK_ID(VALID_SEMID(p_nfyinfo->enfy.sigsem.semid));
			break;
		case TENFY_SETFLG:
			CHECK_ID(VALID_FLGID(p_nfyinfo->enfy.setflg.flgid));
			break;
		case TENFY_SNDDTQ:
			CHECK_ID(VALID_DTQID(p_nfyinfo->enfy.snddtq.dtqid));
			break;
		default:
			CHECK_PAR(false);
			break;
		}
	}
	ercd = E_OK;

  error_exit:
	return(ercd);
}

#endif /* TOPPERS_chknfy */

/*
 *  通知ハンドラ
 */
#ifdef TOPPERS_nfyhdr

void
notify_handler(EXINF exinf)
{
	T_NFYINFO	*p_nfyinfo = (T_NFYINFO *) exinf;
	ER			ercd;

	switch (p_nfyinfo->nfymode & 0x0fU) {
	case TNFY_SETVAR:
		*(p_nfyinfo->nfy.setvar.p_var) = p_nfyinfo->nfy.setvar.value;
		ercd = E_OK;
		break;
	case TNFY_INCVAR:
		(void) loc_cpu();
		*(p_nfyinfo->nfy.incvar.p_var) += 1;
		(void) unl_cpu();
		ercd = E_OK;
		break;
	case TNFY_ACTTSK:
		ercd = act_tsk(p_nfyinfo->nfy.acttsk.tskid);
		break;
	case TNFY_WUPTSK:
		ercd = wup_tsk(p_nfyinfo->nfy.wuptsk.tskid);
		break;
	case TNFY_SIGSEM:
		ercd = sig_sem(p_nfyinfo->nfy.sigsem.semid);
		break;
	case TNFY_SETFLG:
		ercd = set_flg(p_nfyinfo->nfy.setflg.flgid,
							p_nfyinfo->nfy.setflg.flgptn);
		break;
	case TNFY_SNDDTQ:
		ercd = psnd_dtq(p_nfyinfo->nfy.snddtq.dtqid,
							p_nfyinfo->nfy.snddtq.data);
		break;
	default:
		ercd = E_SYS;
		break;
	}

	if (ercd != E_OK) {
		switch (p_nfyinfo->nfymode & ~0x0fU) {
		case TENFY_SETVAR:
			*(p_nfyinfo->enfy.setvar.p_var) = (intptr_t) ercd;
			break;
		case TENFY_INCVAR:
			(void) loc_cpu();
			*(p_nfyinfo->enfy.incvar.p_var) += 1;
			(void) unl_cpu();
			break;
		case TENFY_ACTTSK:
			(void) act_tsk(p_nfyinfo->enfy.acttsk.tskid);
			break;
		case TENFY_WUPTSK:
			(void) wup_tsk(p_nfyinfo->enfy.wuptsk.tskid);
			break;
		case TENFY_SIGSEM:
			(void) sig_sem(p_nfyinfo->enfy.sigsem.semid);
			break;
		case TENFY_SETFLG:
			(void) set_flg(p_nfyinfo->enfy.setflg.flgid,
							p_nfyinfo->enfy.setflg.flgptn);
			break;
		case TENFY_SNDDTQ:
			(void) psnd_dtq(p_nfyinfo->enfy.snddtq.dtqid, (intptr_t) ercd);
			break;
		default:
			break;
		}
	}
}

#endif /* TOPPERS_nfyhdr */
```

`kernel/time_manage.c` の先頭で `check.h` が include されていることを確認する
（されていなければ `#include "check.h"` を追加し、台帳の理由欄に書く）。

- [ ] **Step 4: 配線（allfunc.h / Makefile.kernel）**

`kernel/allfunc.h` の `/* time_manage.c */` 節（`allfunc.h:212-216`）の
`#define TOPPERS_fch_hrt` の直後に2行：
```c
#define TOPPERS_chknfy
#define TOPPERS_nfyhdr
```
（ALLFUNC ビルド＝`CMakeLists.txt:562` で全区画を有効化するため、**ここに書かないと
関数が1つもコンパイルされない**。段階1 Task 3 で `TOPPERS_kermem` を前倒しで足したのと同じ理由。）

`kernel/Makefile.kernel:106` を次に変更（上流形式の維持。CMake は参照しない）：
```
time_manage = set_tim.o get_tim.o adj_tim.o fch_hrt.o chknfy.o nfyhdr.o
```
**`KERNEL_FCSRCS`（`Makefile.kernel:52-56`）は触らない**（22個のまま）。

- [ ] **Step 5: rename 追加・再生成**

`kernel/kernel_rename.def` の `# mempfix.c` 節と `# spin_lock.c` 節のあいだ、
`# cyclic.c` 節（`:102`）の**直前**に新しい節を追加する：

```
# time_manage.c
check_nfyinfo
notify_handler

```

```bash
cd kernel && ruby ../utils/genrename.rb kernel && cd ..
git diff --stat kernel/kernel_rename.h kernel/kernel_unrename.h
grep -n "check_nfyinfo\|notify_handler" kernel/kernel_rename.h kernel/kernel_unrename.h
```
期待: 各ファイル**+2行のみ**、削除0行。`_kernel_check_nfyinfo` / `_kernel_notify_handler` が実在。

- [ ] **Step 6: 転写の正しさを機械的に確認（★目視に頼らない）**

```bash
DCRE=/home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/kernel/time_manage.c
diff -u <(sed -n '/^#ifdef TOPPERS_chknfy$/,/^#endif \/\* TOPPERS_nfyhdr \*\/$/p' $DCRE) \
        <(sed -n '/^#ifdef TOPPERS_chknfy$/,/^#endif \/\* TOPPERS_nfyhdr \*\/$/p' kernel/time_manage.c) \
        > /tmp/dcre2-t4-transcribe.txt; echo "diff rc=$?"
cat /tmp/dcre2-t4-transcribe.txt
```
期待: **rc=0（差分なし）**。差分が出たら、それが意図した FMP3 適応かどうかを1件ずつ
判断して記録する（本 Task の想定では**適応は0件**）。

- [ ] **Step 7: 全8構成ビルド + 等価性 + 起動2構成**

```bash
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --build build/$p > /tmp/dcre2-t4-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/dcre2-t4-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
```
期待: 全て rc=0。**挙動は不変**（誰も呼んでいない。`ALLFUNC` によりコンパイルはされる）。

```bash
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/dcre2-t4-run-musca.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre2-t4-run-musca.log      # 期待: 2
pgrep -a qemu                                                     # 期待: 何も出ない
timeout -k 5 25 cmake --build build/kria_r5-2core --target run > /tmp/dcre2-t4-run-r5.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre2-t4-run-r5.log         # 期待: 2
pgrep -a qemu                                                     # 期待: 何も出ない
```

- [ ] **Step 8: 台帳とコミット** — `DIVERGENCE_MAP.md` に
  `kernel/check.h` / `kernel/kernel_impl.h` / `kernel/time_manage.c` / `kernel/allfunc.h` /
  `kernel/Makefile.kernel` / `kernel/kernel_rename.def`（+再生成2ファイル）の各行を追記して：

```bash
git add -A && git commit -m "feat(kernel): ランタイム通知機構 check_nfyinfo/notify_handler を移植（dcre段階2）"
```

---

### Task 5: cyclic/alarm 基盤 — free-list・initialize・ID マクロ

**推奨モデル:** 中位（sonnet）。挙動不変のまま構造だけ入れる、境界の判断が要る。

**Files:**
- Modify: `kernel/cyclic.h`（`#include <queue.h>` / externs / CYCID）
- Modify: `kernel/cyclic.c`（`tnum_scyc` / `free_cyccb` 定義 / `initialize_cyclic`）
- Modify: `kernel/alarm.h`（同・alm）
- Modify: `kernel/alarm.c`（同・alm）
- Modify: `kernel/kernel_rename.def` →（再生成）`kernel_rename.h` `kernel_unrename.h`
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Consumes: Task 3 の `_kernel_tmax_scycid` / `_kernel_acycinib_table` /
  `_kernel_acyc_nfyinfo_table`（および alm 版）
- Produces: `QUEUE free_cyccb` / `QUEUE free_almcb`、
  `tmax_scycid` / `tmax_salmid`、`tnum_scyc` / `tnum_salm`、
  `acycinib_table[]` / `aalminib_table[]` / `acyc_nfyinfo_table[]` / `aalm_nfyinfo_table[]`、
  2レンジ `CYCID(p_cyccb)` / `ALMID(p_almcb)`。Task 6 が全て使う。

**この Task の挙動は不変である**（AID 無し構成では `tnum_scyc == tnum_cyc` となり
動的スロットのループは空振りする）。

- [ ] **Step 1: `kernel/cyclic.h`**

`#include "time_event.h"`（`cyclic.h:53`）の**直後**に `#include <queue.h>` を追加する
（dcre `cyclic.h:53` と同じ。`kernel_impl.h` は `queue.h` を include していないため必要）。

`extern const ID tmax_cycid;` のブロック（`cyclic.h:82-84`）を次に置換：

```c
/*
 *  周期通知IDの最大値（kernel_cfg.c）
 */
extern const ID	tmax_cycid;
extern const ID	tmax_scycid;		/* 静的生成周期通知のID番号の最大値 */

/*
 *  使用していない周期通知管理ブロックのリスト（cyclic.c）
 *
 *  CYCCBの先頭にはキューにつなぐための領域がないため，タイムイベント
 *  ブロック（tmevtb）の領域を用いる（dcre cyclic.c:118-121 の技法）．
 */
extern QUEUE	free_cyccb;
```

`extern const CYCINIB cycinib_table[];` のブロック（`cyclic.h:86-89`）の直後に追加：

```c
/*
 *  動的生成周期通知の初期化ブロックのエリア（kernel_cfg.c・RAM）
 */
extern CYCINIB	acycinib_table[];

/*
 *  動的生成周期通知の通知方法の格納エリア（kernel_cfg.c・RAM）
 */
extern T_NFYINFO	acyc_nfyinfo_table[];
```

`extern CYCCB *const p_cyccb_table[];`（`cyclic.h:93`）の直後、
`initialize_cyclic` の宣言の手前に **CYCID マクロ**を新設する
（★Task 1 Step 3 の記録に従う。FMP3 の CB は**ポインタ表**なので dcre の
`(p_cyccb - cyccb_table)` は使えず、段階1 の `TSKID`（`kernel/task.h:313-318`）と
同型の inib ポインタによる2レンジ判定にする）：

```c
/*
 *  周期通知の数
 */
#define tnum_cyc	((uint_t)(tmax_cycid - TMIN_CYCID + 1))
#define tnum_scyc	((uint_t)(tmax_scycid - TMIN_CYCID + 1))

/*
 *  CYCCBから周期通知IDを取り出すためのマクロ
 *
 *  FMP3のCYCCBはポインタ表（p_cyccb_table）経由で参照される個別の
 *  named staticであり，CYCCB自身の配列位置から番号を引けない．dcreと
 *  異なりCYCINIBへのポインタから求める（段階1のTSKIDと同型）．動的
 *  生成周期通知（p_cycinibがacycinib_tableを指す）と静的生成周期通知
 *  （p_cycinibがcycinib_tableを指す）の2レンジに対応する．
 */
#define CYCID(p_cyccb) \
	((((p_cyccb)->p_cycinib >= acycinib_table) \
		&& ((p_cyccb)->p_cycinib < &acycinib_table[tnum_cyc - tnum_scyc])) \
	  ? ((ID)(((p_cyccb)->p_cycinib - acycinib_table) + TMIN_CYCID + tnum_scyc)) \
	  : ((ID)(((p_cyccb)->p_cycinib - cycinib_table) + TMIN_CYCID)))
```
（`tnum_cyc` は現在 `cyclic.c:101` にあるが、`CYCID` から使うため `cyclic.h` へ移す。
移動に伴い `cyclic.c` 側の重複定義を Step 2 で削除する。）

**★ もし Task 1 Step 3 の記録が「CB ポインタ表ではなく実体配列であり dcre と同型」だった場合は、
上記マクロを置かず `#define CYCID(p_cyccb) ((ID)(((p_cyccb) - cyccb_table) + TMIN_CYCID))` を
そのまま使い、`cyclic.h` の変更は extern 追加のみとする**（計画作成時の実測ではポインタ表側）。

- [ ] **Step 2: `kernel/alarm.h`（cyc と同型。省略せず全文）**

`#include "time_event.h"`（`alarm.h:52`）の直後に `#include <queue.h>`。

`extern const ID tmax_almid;` のブロック（`alarm.h:76-79`）を次に置換：

```c
/*
 *  アラーム通知IDの最大値（kernel_cfg.c）
 */
extern const ID	tmax_almid;
extern const ID	tmax_salmid;		/* 静的生成アラーム通知のID番号の最大値 */

/*
 *  使用していないアラーム通知管理ブロックのリスト（alarm.c）
 *
 *  ALMCBの先頭にはキューにつなぐための領域がないため，タイムイベント
 *  ブロック（tmevtb）の領域を用いる（dcre alarm.c:118-121 の技法）．
 */
extern QUEUE	free_almcb;
```

`extern const ALMINIB alminib_table[];` の直後に追加：

```c
/*
 *  動的生成アラーム通知の初期化ブロックのエリア（kernel_cfg.c・RAM）
 */
extern ALMINIB	aalminib_table[];

/*
 *  動的生成アラーム通知の通知方法の格納エリア（kernel_cfg.c・RAM）
 */
extern T_NFYINFO	aalm_nfyinfo_table[];
```

`extern ALMCB *const p_almcb_table[];` の直後に：

```c
/*
 *  アラーム通知の数
 */
#define tnum_alm	((uint_t)(tmax_almid - TMIN_ALMID + 1))
#define tnum_salm	((uint_t)(tmax_salmid - TMIN_ALMID + 1))

/*
 *  ALMCBからアラーム通知IDを取り出すためのマクロ
 *
 *  FMP3のALMCBはポインタ表（p_almcb_table）経由で参照される個別の
 *  named staticであり，ALMCB自身の配列位置から番号を引けない．dcreと
 *  異なりALMINIBへのポインタから求める（段階1のTSKIDと同型）．動的
 *  生成アラーム通知と静的生成アラーム通知の2レンジに対応する．
 */
#define ALMID(p_almcb) \
	((((p_almcb)->p_alminib >= aalminib_table) \
		&& ((p_almcb)->p_alminib < &aalminib_table[tnum_alm - tnum_salm])) \
	  ? ((ID)(((p_almcb)->p_alminib - aalminib_table) + TMIN_ALMID + tnum_salm)) \
	  : ((ID)(((p_almcb)->p_alminib - alminib_table) + TMIN_ALMID)))
```

- [ ] **Step 3: `kernel/cyclic.c` — `tnum_cyc` の重複削除と `initialize_cyclic` の改造**

`cyclic.c:98-101` の
```c
/*
 *  周期通知の数
 */
#define tnum_cyc	((uint_t)(tmax_cycid - TMIN_CYCID + 1))
```
を**削除**する（Step 1 で `cyclic.h` に移した）。`INDEX_CYC`/`get_cyccb` はそのまま残す。

`#ifdef TOPPERS_cycini` ブロック（`cyclic.c:110-146`）を次で置換する：

```c
#ifdef TOPPERS_cycini

/*
 *  使用していない周期通知管理ブロックのリスト
 *
 *  CYCCBの先頭にはキューにつなぐための領域がないため，タイムイベント
 *  ブロック（tmevtb）の領域を用いる．なお64ビット環境ではQUEUEが
 *  tmevtb.callbackまで覆うため，free-listから取り出した側（acre_cyc）
 *  でcallback/argを再設定する必要がある．
 */
QUEUE	free_cyccb;

void
initialize_cyclic(PCB *p_my_pcb)
{
	uint_t	i, j;
	CYCCB	*p_cyccb;
	CYCINIB	*p_cycinib;

	if (p_my_pcb->p_tevtcb == NULL){
		return;
	}

	for (i = 0; i < tnum_scyc; i++) {
		if(cycinib_table[i].iprcid == p_my_pcb->prcid) {
			p_cyccb = p_cyccb_table[i];
			p_cyccb->p_cycinib = &(cycinib_table[i]);
			p_cyccb->tmevtb.callback = (CBACK) call_cyclic;
			p_cyccb->tmevtb.arg = (void *) p_cyccb;
			p_cyccb->p_pcb = p_my_pcb;
			if ((p_cyccb->p_cycinib->cycatr & TA_STA) != 0U) {
				/*
				 *  初回の起動のためのタイムイベントを登録する［ASPD1035］
				 *  ［ASPD1062］．
				 */
				p_cyccb->cycsta = true;
				p_cyccb->tmevtb.evttim = (EVTTIM)(p_cyccb->p_cycinib->cycphs);
				tmevtb_register(&(p_cyccb->tmevtb), p_my_pcb);
			}
			else {
				p_cyccb->cycsta = false;
			}
		}
	}

	/*
	 *  動的生成用スロットの初期化（マスタプロセッサのみ）
	 *
	 *  動的生成された周期通知はiprcid=TOPPERS_MASTER_PRCID固定で生成
	 *  されるため，スロットの初期化もマスタプロセッサが一括して行う．
	 *  他プロセッサへの可視性は，本関数の呼出し後のbarrier_syncが保証
	 *  する（段階1のfree_tcbと同じ論証）．
	 */
	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		queue_initialize(&free_cyccb);
		for (i = tnum_scyc, j = 0; i < tnum_cyc; i++, j++) {
			p_cyccb = p_cyccb_table[i];
			p_cycinib = &(acycinib_table[j]);
			p_cycinib->cycatr = TA_NOEXS;
			p_cyccb->p_cycinib = ((const CYCINIB *) p_cycinib);
			p_cyccb->cycsta = false;
			p_cyccb->p_pcb = p_my_pcb;
			p_cyccb->tmevtb.callback = (CBACK) call_cyclic;
			p_cyccb->tmevtb.arg = (void *) p_cyccb;
			queue_insert_prev(&free_cyccb, ((QUEUE *) &(p_cyccb->tmevtb)));
		}
	}
}

#endif /* TOPPERS_cycini */
```
（**FIFO**＝`queue_insert_prev` で末尾へ。Constraint 7 のとおり LIFO 化しない。
`p_my_pcb` はここではマスタ自身なので `get_pcb(1)` と同義だが、
マスタ判定の直後であることを読みやすくするため `p_my_pcb` を使う。）

- [ ] **Step 4: `kernel/alarm.c` — 同型の改造（省略せず全文）**

`alarm.c:98-101` の `#define tnum_alm ...` を**削除**する。
`#ifdef TOPPERS_almini` ブロック（`alarm.c:110-138`）を次で置換する：

```c
#ifdef TOPPERS_almini

/*
 *  使用していないアラーム通知管理ブロックのリスト
 *
 *  ALMCBの先頭にはキューにつなぐための領域がないため，タイムイベント
 *  ブロック（tmevtb）の領域を用いる．なお64ビット環境ではQUEUEが
 *  tmevtb.callbackまで覆うため，free-listから取り出した側（acre_alm）
 *  でcallback/argを再設定する必要がある．
 */
QUEUE	free_almcb;

void
initialize_alarm(PCB *p_my_pcb)
{
	uint_t	i, j;
	ALMCB	*p_almcb;
	ALMINIB	*p_alminib;

	if (p_my_pcb->p_tevtcb == NULL) {
		return;
	}

	for (i = 0; i < tnum_salm; i++) {
		if (alminib_table[i].iprcid == p_my_pcb->prcid) {
			p_almcb = p_almcb_table[i];
			p_almcb->p_alminib = &(alminib_table[i]);
			p_almcb->almsta = false;
			p_almcb->p_pcb = p_my_pcb;
			p_almcb->tmevtb.callback = (CBACK) call_alarm;
			p_almcb->tmevtb.arg = (void *) p_almcb;
		}
	}

	/*
	 *  動的生成用スロットの初期化（マスタプロセッサのみ）
	 *
	 *  動的生成されたアラーム通知はiprcid=TOPPERS_MASTER_PRCID固定で
	 *  生成されるため，スロットの初期化もマスタプロセッサが一括して
	 *  行う．他プロセッサへの可視性は，本関数の呼出し後のbarrier_sync
	 *  が保証する（段階1のfree_tcbと同じ論証）．
	 */
	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		queue_initialize(&free_almcb);
		for (i = tnum_salm, j = 0; i < tnum_alm; i++, j++) {
			p_almcb = p_almcb_table[i];
			p_alminib = &(aalminib_table[j]);
			p_alminib->almatr = TA_NOEXS;
			p_almcb->p_alminib = ((const ALMINIB *) p_alminib);
			p_almcb->almsta = false;
			p_almcb->p_pcb = p_my_pcb;
			p_almcb->tmevtb.callback = (CBACK) call_alarm;
			p_almcb->tmevtb.arg = (void *) p_almcb;
			queue_insert_prev(&free_almcb, ((QUEUE *) &(p_almcb->tmevtb)));
		}
	}
}

#endif /* TOPPERS_almini */
```

- [ ] **Step 5: rename 追加・再生成**

`kernel/kernel_rename.def` の `# cyclic.c` 節（`:102-104`）と `# alarm.c` 節（`:106-108`）を
次に置換する：

```
# cyclic.c
initialize_cyclic
call_cyclic
free_cyccb
tmax_scycid
acycinib_table
acyc_nfyinfo_table

# alarm.c
initialize_alarm
call_alarm
free_almcb
tmax_salmid
aalminib_table
aalm_nfyinfo_table
```

```bash
cd kernel && ruby ../utils/genrename.rb kernel && cd ..
git diff --stat kernel/kernel_rename.h kernel/kernel_unrename.h   # 期待: 各 +8 行・削除0
```

- [ ] **Step 6: 全8構成ビルド + 等価性 + QEMU 起動2構成**

```bash
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --build build/$p > /tmp/dcre2-t5-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/dcre2-t5-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
```
期待: 全て rc=0。**挙動は不変**（AID 無しでは `tnum_scyc == tnum_cyc`、
`free_cyccb` は空のまま初期化されるだけ）。

```bash
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/dcre2-t5-run-musca.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre2-t5-run-musca.log     # 期待: 2
grep -c 'cyclic_handler\|Sample program' /tmp/dcre2-t5-run-musca.log  # 参考: サンプル走行の証拠
pgrep -a qemu                                                    # 期待: 何も出ない
timeout -k 5 25 cmake --build build/kria_r5-2core --target run > /tmp/dcre2-t5-run-r5.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre2-t5-run-r5.log        # 期待: 2
pgrep -a qemu                                                    # 期待: 何も出ない
```
**★静的な周期通知が段階2 の変更後も従来どおり発火し続けていることをログで確認する**
（`initialize_cyclic` の静的ループ境界を `tnum_cyc` → `tnum_scyc` に変えた影響が
AID 無し構成に無いことの実証）。

- [ ] **Step 7: 台帳とコミット** — `DIVERGENCE_MAP.md` に
  `kernel/cyclic.h` / `kernel/cyclic.c` / `kernel/alarm.h` / `kernel/alarm.c` /
  `kernel/kernel_rename.def`（+再生成2ファイル）を追記して：

```bash
git add -A && git commit -m "feat(kernel): free_cyccb/free_almcb と動的スロット初期化・2レンジCYCID/ALMID（dcre段階2）"
```

---

### Task 6: acre/del × cyc/alm と E_NOEXS 検査8関数・宣言配線

**推奨モデル:** 中位（sonnet）

**Files:**
- Modify: `include/kernel.h`（`T_RCYC`（`:198`）の手前に `T_CCYC`、`T_RALM`（`:204`）の
  手前に `T_CALM`、`sta_cyc`（`:328`）の手前に4宣言）
- Modify: `kernel/cyclic.c`（`acre_cyc` / `del_cyc` 追加 + `sta_cyc`/`msta_cyc`/`stp_cyc`/`ref_cyc` の E_NOEXS）
- Modify: `kernel/alarm.c`（`acre_alm` / `del_alm` 追加 + `sta_alm`/`msta_alm`/`stp_alm`/`ref_alm` の E_NOEXS）
- Modify: `kernel/allfunc.h`（4行）・`kernel/Makefile.kernel`（cyclic/alarm 行に .o 各2個）
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Consumes: Task 4 の `check_nfyinfo`/`notify_handler`、
  Task 5 の `free_cyccb`/`free_almcb`/`acycinib_table`/`aalminib_table`/
  `acyc_nfyinfo_table`/`aalm_nfyinfo_table`/`tmax_scycid`/`tmax_salmid`/`CYCID`/`ALMID`/
  `tnum_scyc`/`tnum_salm`、Task 3 の `T_NFYINFO`
- Produces: `ER_ID acre_cyc(const T_CCYC *)` / `ER del_cyc(ID)` /
  `ER_ID acre_alm(const T_CALM *)` / `ER del_alm(ID)`（ユーザ API）。Task 7 が使う。

- [ ] **Step 1: `include/kernel.h` にパケット型2つと宣言4つを追加**

`typedef struct t_rcyc {`（`include/kernel.h:198`）の**直前**に：
```c
typedef struct t_ccyc {
	ATR			cycatr;		/* 周期通知属性 */
	T_NFYINFO	nfyinfo;	/* 周期通知の通知方法 */
	RELTIM		cyctim;		/* 周期通知の通知周期 */
	RELTIM		cycphs;		/* 周期通知の通知位相 */
} T_CCYC;
```
`typedef struct t_ralm {`（`:204`、`T_RCYC` の直後）の**直前**に：
```c
typedef struct t_calm {
	ATR			almatr;		/* アラーム通知属性 */
	T_NFYINFO	nfyinfo;	/* アラーム通知の通知方法 */
} T_CALM;
```
`extern ER sta_cyc(ID cycid) throw();`（`:328`）の**直前**に：
```c
extern ER_ID	acre_cyc(const T_CCYC *pk_ccyc) throw();
extern ER		del_cyc(ID cycid) throw();
```
`extern ER sta_alm(ID almid, RELTIM almtim) throw();`（`:333`）の**直前**に：
```c
extern ER_ID	acre_alm(const T_CALM *pk_calm) throw();
extern ER		del_alm(ID almid) throw();
```
（機能コードは Task 1 Step 2 の記録どおり `TFN_ACRE_CYC (-202)` / `TFN_DEL_CYC (-218)` /
`TFN_ACRE_ALM (-203)` / `TFN_DEL_ALM (-219)` が `include/kernel_fncode.h:142,143,154,155` に
**既存**。`kernel_fncode.h` は**変更しない**。
返値型は段階1 の `acre_tsk` と揃えて `ER_ID` にする。dcre の `.c` は `ER_UINT` を使うが
どちらも `int_t` の別名であり、宣言と定義で揃っていれば問題ない — **意図的な逸脱として台帳に書く**。）

- [ ] **Step 2: `kernel/cyclic.c` に `acre_cyc` を追加**（`TOPPERS_cycini` 区画の直後、
  `TOPPERS_sta_cyc` 区画の直前）

```c
/*
 *  周期通知の生成
 */
#ifdef TOPPERS_acre_cyc

#ifndef LOG_ACRE_CYC_ENTER
#define LOG_ACRE_CYC_ENTER(pk_ccyc)
#endif /* LOG_ACRE_CYC_ENTER */

#ifndef LOG_ACRE_CYC_LEAVE
#define LOG_ACRE_CYC_LEAVE(ercd)
#endif /* LOG_ACRE_CYC_LEAVE */

ER_ID
acre_cyc(const T_CCYC *pk_ccyc)
{
	CYCCB		*p_cyccb;
	CYCINIB		*p_cycinib;
	ATR			cycatr;
	RELTIM		cyctim, cycphs;
	T_NFYINFO	*p_nfyinfo;
	ER			ercd;

	LOG_ACRE_CYC_ENTER(pk_ccyc);
	CHECK_TSKCTX_UNL();

	cycatr = pk_ccyc->cycatr;
	cyctim = pk_ccyc->cyctim;
	cycphs = pk_ccyc->cycphs;

	CHECK_VALIDATR(cycatr, TA_STA);
	ercd = check_nfyinfo(&(pk_ccyc->nfyinfo));
	if (ercd != E_OK) {
		goto error_exit;
	}
	CHECK_PAR(0 < cyctim && cyctim <= TMAX_RELTIM);
	CHECK_PAR(cycphs <= TMAX_RELTIM);

	lock_cpu();
	acquire_glock();
	if (tnum_cyc == tnum_scyc || queue_empty(&free_cyccb)) {
		ercd = E_NOID;
	}
	else {
		p_cyccb = ((CYCCB *)(((char *) queue_delete_next(&free_cyccb))
											- offsetof(CYCCB, tmevtb)));
		p_cycinib = (CYCINIB *)(p_cyccb->p_cycinib);
		p_cycinib->cycatr = cycatr;
		if (pk_ccyc->nfyinfo.nfymode == TNFY_HANDLER) {
			p_cycinib->exinf = pk_ccyc->nfyinfo.nfy.handler.exinf;
			p_cycinib->nfyhdr = (NFYHDR)(pk_ccyc->nfyinfo.nfy.handler.tmehdr);
		}
		else {
			p_nfyinfo = &acyc_nfyinfo_table[p_cycinib - acycinib_table];
			*p_nfyinfo = pk_ccyc->nfyinfo;
			p_cycinib->exinf = (EXINF) p_nfyinfo;
			p_cycinib->nfyhdr = notify_handler;
		}
		p_cycinib->cyctim = cyctim;
		p_cycinib->cycphs = cycphs;
		/*
		 *  動的生成周期通知の割付けプロセッサ（Global Constraint 4）．
		 *  affinityはTOPPERS_TEPP_PRC（時間イベント処理プロセッサ集合）．
		 *  全プロセッサにするとmsta_cycでp_tevtcb==NULLのプロセッサへ
		 *  移せてしまうため（静的側はcyclic.py:65-68が同じ制約を課す）．
		 */
		p_cycinib->iprcid = TOPPERS_MASTER_PRCID;
		p_cycinib->affinity = ((uint_t) TOPPERS_TEPP_PRC);

		/*
		 *  free-listのリンクにtmevtb領域を転用しているため，64ビット
		 *  環境ではcallbackが上書きされている．必ず再設定する．
		 */
		p_cyccb->p_pcb = get_pcb(TOPPERS_MASTER_PRCID);
		p_cyccb->tmevtb.callback = (CBACK) call_cyclic;
		p_cyccb->tmevtb.arg = (void *) p_cyccb;

		if ((cycatr & TA_STA) != 0U) {
			p_cyccb->cycsta = true;
			tmevtb_enqueue_reltim(&(p_cyccb->tmevtb), cycphs, p_cyccb->p_pcb);
		}
		else {
			p_cyccb->cycsta = false;
		}
		ercd = CYCID(p_cyccb);
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_ACRE_CYC_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_acre_cyc */
```
（dcre からの適応点は4つだけ：(1) `lock_cpu` の直後に `acquire_glock`／末尾に `release_glock`、
(2) 空判定を `tnum_cyc == 0` → `tnum_cyc == tnum_scyc`（FMP3 は静的分が別レンジのため）、
(3) iprcid/affinity/p_pcb の充填、(4) callback/arg の再設定（訂正D）。
`cycphs` の範囲検査は dcre が `0 <= cycphs && cycphs <= TMAX_RELTIM` と書くが、
`RELTIM` は符号なしで `0 <=` が常真のため FMP3 では上半分だけを書く。意味は同一。）

- [ ] **Step 3: `kernel/cyclic.c` に `del_cyc` を追加**（`acre_cyc` の直後）

```c
/*
 *  周期通知の削除
 */
#ifdef TOPPERS_del_cyc

#ifndef LOG_DEL_CYC_ENTER
#define LOG_DEL_CYC_ENTER(cycid)
#endif /* LOG_DEL_CYC_ENTER */

#ifndef LOG_DEL_CYC_LEAVE
#define LOG_DEL_CYC_LEAVE(ercd)
#endif /* LOG_DEL_CYC_LEAVE */

ER
del_cyc(ID cycid)
{
	CYCCB	*p_cyccb;
	CYCINIB	*p_cycinib;
	ER		ercd;

	LOG_DEL_CYC_ENTER(cycid);
	CHECK_TSKCTX_UNL();
	CHECK_ID(VALID_CYCID(cycid));
	p_cyccb = get_cyccb(cycid);

	lock_cpu();
	acquire_glock();
	if (p_cyccb->p_cycinib->cycatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (cycid <= tmax_scycid) {
		ercd = E_OBJ;
	}
	else {
		/*
		 *  動作中でも削除できる［dcre仕様］．動作中ならタイムイベント
		 *  キューから外してからfree-listへ返却する．dequeueの対象は
		 *  当該周期通知の割付けプロセッサ（stp_cycと同じ手順）．
		 */
		if (p_cyccb->cycsta) {
			p_cyccb->cycsta = false;
			tmevtb_dequeue(&(p_cyccb->tmevtb), p_cyccb->p_pcb);
		}

		p_cycinib = (CYCINIB *)(p_cyccb->p_cycinib);
		p_cycinib->cycatr = TA_NOEXS;
		queue_insert_prev(&free_cyccb, ((QUEUE *) &(p_cyccb->tmevtb)));
		ercd = E_OK;
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_DEL_CYC_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_del_cyc */
```

- [ ] **Step 4: `kernel/alarm.c` に `acre_alm` を追加**（`TOPPERS_almini` 区画の直後）

```c
/*
 *  アラーム通知の生成
 */
#ifdef TOPPERS_acre_alm

#ifndef LOG_ACRE_ALM_ENTER
#define LOG_ACRE_ALM_ENTER(pk_calm)
#endif /* LOG_ACRE_ALM_ENTER */

#ifndef LOG_ACRE_ALM_LEAVE
#define LOG_ACRE_ALM_LEAVE(ercd)
#endif /* LOG_ACRE_ALM_LEAVE */

ER_ID
acre_alm(const T_CALM *pk_calm)
{
	ALMCB		*p_almcb;
	ALMINIB		*p_alminib;
	ATR			almatr;
	T_NFYINFO	*p_nfyinfo;
	ER			ercd;

	LOG_ACRE_ALM_ENTER(pk_calm);
	CHECK_TSKCTX_UNL();

	almatr = pk_calm->almatr;

	CHECK_VALIDATR(almatr, TA_NULL);
	ercd = check_nfyinfo(&(pk_calm->nfyinfo));
	if (ercd != E_OK) {
		goto error_exit;
	}

	lock_cpu();
	acquire_glock();
	if (tnum_alm == tnum_salm || queue_empty(&free_almcb)) {
		ercd = E_NOID;
	}
	else {
		p_almcb = ((ALMCB *)(((char *) queue_delete_next(&free_almcb))
											- offsetof(ALMCB, tmevtb)));
		p_alminib = (ALMINIB *)(p_almcb->p_alminib);
		p_alminib->almatr = almatr;
		if (pk_calm->nfyinfo.nfymode == TNFY_HANDLER) {
			p_alminib->exinf = pk_calm->nfyinfo.nfy.handler.exinf;
			p_alminib->nfyhdr = (NFYHDR)(pk_calm->nfyinfo.nfy.handler.tmehdr);
		}
		else {
			p_nfyinfo = &aalm_nfyinfo_table[p_alminib - aalminib_table];
			*p_nfyinfo = pk_calm->nfyinfo;
			p_alminib->exinf = (EXINF) p_nfyinfo;
			p_alminib->nfyhdr = notify_handler;
		}
		/*
		 *  動的生成アラーム通知の割付けプロセッサ（Global Constraint 4）．
		 *  affinityはTOPPERS_TEPP_PRC（時間イベント処理プロセッサ集合）．
		 */
		p_alminib->iprcid = TOPPERS_MASTER_PRCID;
		p_alminib->affinity = ((uint_t) TOPPERS_TEPP_PRC);

		/*
		 *  free-listのリンクにtmevtb領域を転用しているため，64ビット
		 *  環境ではcallbackが上書きされている．必ず再設定する．
		 */
		p_almcb->p_pcb = get_pcb(TOPPERS_MASTER_PRCID);
		p_almcb->tmevtb.callback = (CBACK) call_alarm;
		p_almcb->tmevtb.arg = (void *) p_almcb;

		p_almcb->almsta = false;
		ercd = ALMID(p_almcb);
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_ACRE_ALM_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_acre_alm */
```

- [ ] **Step 5: `kernel/alarm.c` に `del_alm` を追加**（`acre_alm` の直後）

```c
/*
 *  アラーム通知の削除
 */
#ifdef TOPPERS_del_alm

#ifndef LOG_DEL_ALM_ENTER
#define LOG_DEL_ALM_ENTER(almid)
#endif /* LOG_DEL_ALM_ENTER */

#ifndef LOG_DEL_ALM_LEAVE
#define LOG_DEL_ALM_LEAVE(ercd)
#endif /* LOG_DEL_ALM_LEAVE */

ER
del_alm(ID almid)
{
	ALMCB	*p_almcb;
	ALMINIB	*p_alminib;
	ER		ercd;

	LOG_DEL_ALM_ENTER(almid);
	CHECK_TSKCTX_UNL();
	CHECK_ID(VALID_ALMID(almid));
	p_almcb = get_almcb(almid);

	lock_cpu();
	acquire_glock();
	if (p_almcb->p_alminib->almatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (almid <= tmax_salmid) {
		ercd = E_OBJ;
	}
	else {
		/*
		 *  動作中でも削除できる［dcre仕様］．動作中ならタイムイベント
		 *  キューから外してからfree-listへ返却する．dequeueの対象は
		 *  当該アラーム通知の割付けプロセッサ（stp_almと同じ手順）．
		 */
		if (p_almcb->almsta) {
			p_almcb->almsta = false;
			tmevtb_dequeue(&(p_almcb->tmevtb), p_almcb->p_pcb);
		}

		p_alminib = (ALMINIB *)(p_almcb->p_alminib);
		p_alminib->almatr = TA_NOEXS;
		queue_insert_prev(&free_almcb, ((QUEUE *) &(p_almcb->tmevtb)));
		ercd = E_OK;
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_DEL_ALM_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_del_alm */
```

- [ ] **Step 6: E_NOEXS 検査の挿入（8関数）**

段階1 と同じ **existence-before-state** 規約：`acquire_glock()` の**直後・既存の状態判定の
最初の分岐**として挿入し、既存本体は `else if` / `else` 連鎖へ字下げのまま繰り込む。

**★ `msta_cyc` / `msta_alm` は dcre に存在しない FMP3 固有関数であり、上流に先例が無い。**
`sta_cyc` / `sta_alm` と同位置への**類推適用**である（段階1 の `mig_tsk`/`ras_ter`/`ter_tsk` と
同じ扱い）。レビュー時にこの点を明示すること。

`kernel/cyclic.c`：
- `sta_cyc`（`:156`）: `acquire_glock();` の直後を
```c
	if (p_cyccb->p_cycinib->cycatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		if (p_cyccb->cycsta) {
			tmevtb_dequeue(&(p_cyccb->tmevtb), p_cyccb->p_pcb);
		}
		else {
			p_cyccb->cycsta = true;
		}
		/*
		 *  初回の起動のためのタイムイベントを登録する［ASPD1036］．
		 */
		tmevtb_enqueue_reltim(&(p_cyccb->tmevtb), p_cyccb->p_cycinib->cycphs,
										p_cyccb->p_pcb);
		ercd = E_OK;
	}
```
- `msta_cyc`（`:196`）: 同じく `acquire_glock();` の直後に `if (... == TA_NOEXS) { ercd = E_NOEXS; } else { ...既存本体... }`。
  **注意: `msta_cyc` はロック取得前に `p_cyccb->p_cycinib->iprcid` と `affinity` を
  読んで `CHECK_PRCID`/`CHECK_MIG` している（`cyclic.c:161-169`）。TA_NOEXS スロットでは
  affinity が未定義値になり得るが、段階1 の `mig_tsk` と同じ既知課題（ユーザ誤用経路の
  hardening、段階1最終レビューの deferred #1）として本段階でも触らない。**
  この判断を台帳の理由欄に1行書く。
- `stp_cyc`（`:245`）: 同様。既存本体（`if (p_cyccb->cycsta) {...} ercd = E_OK;`）を else に。
- `ref_cyc`（`:278`）: 同様。

`kernel/alarm.c`：
- `sta_alm`（`:146`）・`msta_alm`（`:183`）・`stp_alm`（`:229`）・`ref_alm`（`:262`）に同型で挿入。
  `msta_alm` も `msta_cyc` と同じ注意が当てはまる。

**除外**: `call_cyclic`/`call_alarm`（内部関数・ID を取らない）、`initialize_*`。

- [ ] **Step 7: 配線**

`kernel/allfunc.h` の `/* cyclic.c */` 節（`:218-224`）の `#define TOPPERS_cycini` の直後に：
```c
#define TOPPERS_acre_cyc
#define TOPPERS_del_cyc
```
`/* alarm.c */` 節（`:226-232`）の `#define TOPPERS_almini` の直後に：
```c
#define TOPPERS_acre_alm
#define TOPPERS_del_alm
```

`kernel/Makefile.kernel:108,110` を次に変更（上流形式の維持。`KERNEL_FCSRCS` は触らない）：
```
cyclic = cycini.o acre_cyc.o del_cyc.o sta_cyc.o msta_cyc.o stp_cyc.o ref_cyc.o cyccal.o
alarm = almini.o acre_alm.o del_alm.o sta_alm.o msta_alm.o stp_alm.o ref_alm.o almcal.o
```
rename 追加は本 Task では**不要**（`acre_cyc` 等は公開名でリネームしない）。

- [ ] **Step 8: 全8構成ビルド + 等価性**

```bash
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --build build/$p > /tmp/dcre2-t6-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/dcre2-t6-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
```
期待: 全て rc=0。AID 無し構成の**挙動は不変**（`free_cyccb` が空なので `acre_cyc` は E_NOID、
既存 API は TA_NOEXS でないため E_NOEXS 分岐を通らない）。

- [ ] **Step 9: QEMU 起動2構成（非退行）**

```bash
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/dcre2-t6-run-musca.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre2-t6-run-musca.log     # 期待: 2
pgrep -a qemu                                                    # 期待: 何も出ない
timeout -k 5 25 cmake --build build/kria_arm64 --target run > /tmp/dcre2-t6-run-arm64.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/dcre2-t6-run-arm64.log    # 期待: 4
pgrep -a qemu                                                    # 期待: 何も出ない
```
（kria_arm64 は 64bit ＝ 訂正D の影響を最も受ける構成なので、ここで静的 cyc/alm の
発火が壊れていないことを確認する意味がある。）

- [ ] **Step 10: 台帳とコミット** — `DIVERGENCE_MAP.md` に
  `include/kernel.h` / `kernel/cyclic.c` / `kernel/alarm.c` / `kernel/allfunc.h` /
  `kernel/Makefile.kernel` を追記（`include/kernel.h` は Task 3 で既に1行あるので
  理由欄に「T_CCYC/T_CALM と4宣言を追加（段階2 Task 6）」を追記する形でよい）：

```bash
git add -A && git commit -m "feat(kernel): acre_cyc/del_cyc・acre_alm/del_alm とE_NOEXS検査8箇所（dcre段階2）"
```

---

### Task 7: QEMU 回帰テスト test_dcre2

**推奨モデル:** 中位（sonnet）

**Files:**
- Create: `test/test_dcre2.c` `test/test_dcre2.cfg` `test/test_dcre2.h`
- Modify: `test/MANIFEST`（`test_dcre1.h` の直後に3行）・`test/testexec.rb`（`"dcre1"` の直後に1行）
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:** Consumes Task 2〜6 の全成果。`syssvc/test_svc.h` の
`test_start` / `check_point`（= `check_point_prc(count, 0)`）/ `check_point_prc` /
`check_ercd` / `check_assert` / `check_finish`。

**★check_point の意味論（段階1 Task 6 で確定した事実。`syssvc/test_svc.c` 実測）:**
`check_point_prc(count, prcid)` は `prcid > 0` のとき `check_count[prcid-1]` を、
`prcid == 0`（= `check_point()`）のとき `check_count[0]` を使う。
すなわち **PRC1 の `check_point()` と PRC2 の `check_point_prc(n,2)` は独立したカウンタ**であり、
**PRC2 側の最初のチェックポイントは `check_point_prc(1, 2)`** になる。
各プロセッサのカウンタは 1 から単調増加でなければならず、崩れると
`## Unexpected check point` が出て失敗する。

- [ ] **Step 1: `syssvc/test_svc.h` を先に読み、存在する検査プリミティブだけで書く**

```bash
sed -n '55,115p' syssvc/test_svc.h
```
`test_start` / `check_point_prc` / `check_finish` / `check_finish_prc` / `check_assert` /
`check_ercd` / `check_point` が存在することを確認する。無いものは使わない。
`sil_get_pid()` を通知ハンドラ内で使うため `#include <sil.h>` の要否も確認する
（`syssvc/test_svc.c` が `sil_get_pid` を使っている形を踏襲する）。

- [ ] **Step 2: `test/test_dcre2.h`**

```c
#include <kernel.h>
#include "target_test.h"

#define HIGH_PRIORITY	9
#define MID_PRIORITY	10
#define LOW_PRIORITY	11

#ifndef STACK_SIZE
#define	STACK_SIZE		4096
#endif /* STACK_SIZE */

/*  動的生成する周期通知／アラーム通知の時間パラメータ（単位: マイクロ秒）  */
#ifndef CYC_TIME
#define CYC_TIME		20000U		/* 周期 20ms */
#endif /* CYC_TIME */

#ifndef ALM_TIME
#define ALM_TIME		20000U		/* アラーム 20ms 後 */
#endif /* ALM_TIME */

/*  通知の発火を待つための時間（周期の数倍を取る）  */
#ifndef TEST_TIME_PROC
#define TEST_TIME_PROC	200000U		/* 200ms */
#endif /* TEST_TIME_PROC */

#ifndef TOPPERS_MACRO_ONLY
extern void	task1(EXINF exinf);
extern void	static_cyclic_handler(EXINF exinf);
extern void	static_alarm_handler(EXINF exinf);
extern void	dcyc_handler(EXINF exinf);
extern void	dcyc_prc2_handler(EXINF exinf);
extern void	dalm_handler(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */
```

- [ ] **Step 3: `test/test_dcre2.cfg`**

```c
/*
 *		動的生成API（acre_cyc/del_cyc/acre_alm/del_alm）のテストの
 *		システムコンフィギュレーションファイル
 *
 *  $Id$
 */
INCLUDE("test_common1.cfg");

#include "test_dcre2.h"

CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	/*
	 *  静的な周期通知／アラーム通知（TA_NULL なので自動では動作しない）．
	 *  del_cyc/del_alm の E_OBJ 対象であると同時に，AID_CYC/AID_ALM が
	 *  「静的オブジェクトが1個以上あること」を要求する（訂正E）ため必須．
	 */
	CRE_CYC(CYC1, { TA_NULL, { TNFY_HANDLER, 0, static_cyclic_handler },
													1000000, 0 });
	CRE_ALM(ALM1, { TA_NULL, { TNFY_HANDLER, 0, static_alarm_handler } });
}

/*  AID_CYC/AID_ALM はクラス外専用（Task 3 の E_RSATR 検査対象）  */
AID_CYC(2);
AID_ALM(2);
```
（`DEF_MPK` は**不要** — cyc/alm はメモリプールを使わない。spec §7。）

- [ ] **Step 4: `test/test_dcre2.c`**（著作権ヘッダは `test/test_dcre1.c` と同形式で付ける）

```c
/*
 *		動的生成API（acre_cyc/del_cyc/acre_alm/del_alm）のテスト
 *
 * 【テストの目的】
 *
 *	(A) acre_cyc（TNFY_HANDLER）→ sta_cyc → 発火 → stp_cyc → del_cyc
 *	(B) 動作中のままの del_cyc が成功すること（dcre 意味論）
 *	(C) 削除済みIDへの sta_cyc/stp_cyc/ref_cyc/msta_cyc が E_NOEXS
 *	(D) スロット枯渇時の E_NOID／不正パラメータの E_PAR
 *	(E) msta_cyc による PRC2 への移動と，PRC2 での発火
 *	(F) 非ハンドラ通知（TNFY_SETVAR）が notify_handler 経由で働くこと
 *	(G) alm 側の acre/sta/再sta/del と E_NOEXS/E_NOID
 *	(H) 静的生成オブジェクトへの del_cyc/del_alm が E_OBJ
 *
 * 【使用リソース】
 *
 *	TASK1: 中優先度タスク，TA_ACT属性（静的・PRC1）
 *	CYC1/ALM1: 静的な周期通知／アラーム通知（TA_NULL・起動しない）
 *	AID_CYC(2)/AID_ALM(2): 動的スロット各2個
 */

#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include "syssvc/test_svc.h"
#include "kernel_cfg.h"
#include "test_dcre2.h"

/* DO NOT DELETE THIS LINE -- gentest depends on it. */

static volatile uint_t		cyc_count;
static volatile uint_t		alm_count;
static volatile intptr_t	nfy_var;
static volatile bool_t		prc2_reported;

/*
 *  静的オブジェクト用のハンドラ（TA_NULL なので発火しない）
 */
void
static_cyclic_handler(EXINF exinf)
{
	check_point(0);			/* 到達したら失敗する */
}

void
static_alarm_handler(EXINF exinf)
{
	check_point(0);			/* 到達したら失敗する */
}

/*
 *  動的周期通知のハンドラ（TNFY_HANDLER）
 */
void
dcyc_handler(EXINF exinf)
{
	check_assert(((intptr_t) exinf) == 0xC1);
	cyc_count++;
}

/*
 *  PRC2 へ移動した周期通知のハンドラ
 *
 *  周期通知は繰り返し発火するので，チェックポイントは初回だけ打つ．
 */
void
dcyc_prc2_handler(EXINF exinf)
{
	ID		prcid;

	sil_get_pid(&prcid);
	check_assert(prcid == 2);
	if (!prc2_reported) {
		prc2_reported = true;
		/*  PRC2 側の check_count[1] は独立カウンタ．本テストで
		 *  PRC2 が打つ最初のチェックポイントなので 1 である．  */
		check_point_prc(1, 2);
	}
}

/*
 *  動的アラーム通知のハンドラ（TNFY_HANDLER）
 */
void
dalm_handler(EXINF exinf)
{
	check_assert(((intptr_t) exinf) == 0xA1);
	alm_count++;
}

void
task1(EXINF exinf)
{
	T_CCYC	ccyc;
	T_CALM	calm;
	T_RCYC	rcyc;
	T_RALM	ralm;
	ER_ID	erid;
	ID		cycid1, cycid2, almid1, almid2;
	uint_t	snapshot;

	test_start(__FILE__);
	check_point(1);

	/*
	 *  1) acre_cyc（TNFY_HANDLER）→ sta_cyc → 発火 → stp_cyc → del_cyc
	 */
	ccyc.cycatr = TA_NULL;
	ccyc.nfyinfo.nfymode = TNFY_HANDLER;
	ccyc.nfyinfo.nfy.handler.exinf = (EXINF) 0xC1;
	ccyc.nfyinfo.nfy.handler.tmehdr = dcyc_handler;
	ccyc.cyctim = CYC_TIME;
	ccyc.cycphs = CYC_TIME;
	erid = acre_cyc(&ccyc);
	check_assert(erid > CYC1);		/* 動的IDは静的レンジの外＝2レンジCYCIDの直接検証 */
	cycid1 = (ID) erid;

	cyc_count = 0;
	check_ercd(sta_cyc(cycid1), E_OK);
	check_ercd(ref_cyc(cycid1, &rcyc), E_OK);
	check_assert(rcyc.cycstat == TCYC_STA);
	check_assert(rcyc.prcid == 1);				/* iprcid=1 固定の検証 */
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);
	check_assert(cyc_count >= 1U);
	check_ercd(stp_cyc(cycid1), E_OK);
	check_ercd(ref_cyc(cycid1, &rcyc), E_OK);
	check_assert(rcyc.cycstat == TCYC_STP);
	check_ercd(del_cyc(cycid1), E_OK);
	check_point(2);

	/*
	 *  2) 動作中のままの del_cyc が成功すること（dcre 意味論）
	 */
	erid = acre_cyc(&ccyc);
	check_assert(erid > CYC1);
	cycid1 = (ID) erid;
	cyc_count = 0;
	check_ercd(sta_cyc(cycid1), E_OK);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);
	check_assert(cyc_count >= 1U);
	check_ercd(del_cyc(cycid1), E_OK);		/* 動作中でも削除できる */
	snapshot = cyc_count;
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);
	check_assert(cyc_count == snapshot);	/* 削除後は発火しない */
	check_point(3);

	/*
	 *  3) 削除済みIDへのサービスコールが E_NOEXS
	 */
	check_ercd(sta_cyc(cycid1), E_NOEXS);
	check_ercd(stp_cyc(cycid1), E_NOEXS);
	check_ercd(ref_cyc(cycid1, &rcyc), E_NOEXS);
	check_ercd(msta_cyc(cycid1, 1), E_NOEXS);
	check_ercd(del_cyc(cycid1), E_NOEXS);
	check_point(4);

	/*
	 *  4) パラメータ検査（E_PAR）とスロット枯渇（E_NOID）
	 */
	ccyc.cyctim = 0;						/* 0 < cyctim を破る */
	check_assert(acre_cyc(&ccyc) == E_PAR);
	ccyc.cyctim = CYC_TIME;
	ccyc.nfyinfo.nfy.handler.tmehdr = NULL;	/* FUNC_NONNULL を破る（check_nfyinfo） */
	check_assert(acre_cyc(&ccyc) == E_PAR);
	ccyc.nfyinfo.nfy.handler.tmehdr = dcyc_handler;
	ccyc.cycatr = TA_STA | 0x04U;			/* 未定義ビット → E_RSATR */
	check_assert(acre_cyc(&ccyc) == E_RSATR);
	ccyc.cycatr = TA_NULL;

	erid = acre_cyc(&ccyc);
	check_assert(erid > CYC1);
	cycid1 = (ID) erid;
	erid = acre_cyc(&ccyc);
	check_assert(erid > CYC1);
	cycid2 = (ID) erid;
	check_assert(cycid1 != cycid2);
	check_assert(acre_cyc(&ccyc) == E_NOID);	/* スロット2個を使い切った */
	check_ercd(del_cyc(cycid2), E_OK);
	check_ercd(del_cyc(cycid1), E_OK);
	check_point(5);

	/*
	 *  5) msta_cyc による PRC2 への移動（affinity = TOPPERS_TEPP_PRC の実証）
	 */
	ccyc.nfyinfo.nfy.handler.exinf = (EXINF) 0;
	ccyc.nfyinfo.nfy.handler.tmehdr = dcyc_prc2_handler;
	erid = acre_cyc(&ccyc);
	check_assert(erid > CYC1);
	cycid1 = (ID) erid;
	prc2_reported = false;
	check_ercd(msta_cyc(cycid1, 2), E_OK);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);
	check_assert(prc2_reported);			/* PRC2 側が cp(1,2) を打った */
	check_ercd(ref_cyc(cycid1, &rcyc), E_OK);
	check_assert(rcyc.prcid == 2);
	check_ercd(stp_cyc(cycid1), E_OK);
	check_ercd(del_cyc(cycid1), E_OK);
	check_point(6);

	/*
	 *  6) 非ハンドラ通知（TNFY_SETVAR）＝ notify_handler トランポリンの実証
	 */
	nfy_var = 0;
	ccyc.cycatr = TA_NULL;
	ccyc.nfyinfo.nfymode = TNFY_SETVAR;
	ccyc.nfyinfo.nfy.setvar.p_var = (intptr_t *) &nfy_var;
	ccyc.nfyinfo.nfy.setvar.value = 0x5A;
	ccyc.cyctim = CYC_TIME;
	ccyc.cycphs = CYC_TIME;
	erid = acre_cyc(&ccyc);
	check_assert(erid > CYC1);
	cycid1 = (ID) erid;
	check_ercd(sta_cyc(cycid1), E_OK);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);
	check_assert(nfy_var == 0x5A);
	check_ercd(del_cyc(cycid1), E_OK);
	check_point(7);

	/*
	 *  7) alm 側：acre → sta → 発火 → 再 sta → 発火 → del → E_NOEXS/E_NOID
	 */
	calm.almatr = TA_NULL;
	calm.nfyinfo.nfymode = TNFY_HANDLER;
	calm.nfyinfo.nfy.handler.exinf = (EXINF) 0xA1;
	calm.nfyinfo.nfy.handler.tmehdr = dalm_handler;
	erid = acre_alm(&calm);
	check_assert(erid > ALM1);		/* 動的IDは静的レンジの外＝2レンジALMIDの検証 */
	almid1 = (ID) erid;

	alm_count = 0;
	check_ercd(sta_alm(almid1, ALM_TIME), E_OK);
	check_ercd(ref_alm(almid1, &ralm), E_OK);
	check_assert(ralm.almstat == TALM_STA);
	check_assert(ralm.prcid == 1);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);
	check_assert(alm_count == 1U);				/* アラームは1回だけ */
	check_ercd(sta_alm(almid1, ALM_TIME), E_OK);	/* 再起動できる */
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);
	check_assert(alm_count == 2U);

	erid = acre_alm(&calm);
	check_assert(erid > ALM1);
	almid2 = (ID) erid;
	check_assert(acre_alm(&calm) == E_NOID);	/* スロット2個を使い切った */
	check_ercd(sta_alm(almid2, ALM_TIME), E_OK);
	check_ercd(del_alm(almid2), E_OK);			/* 動作中でも削除できる */
	check_ercd(del_alm(almid1), E_OK);
	check_ercd(sta_alm(almid1, ALM_TIME), E_NOEXS);
	check_ercd(stp_alm(almid1), E_NOEXS);
	check_ercd(ref_alm(almid1, &ralm), E_NOEXS);
	check_ercd(msta_alm(almid1, ALM_TIME, 1), E_NOEXS);
	check_ercd(del_alm(almid1), E_NOEXS);
	check_point(8);

	/*
	 *  8) 静的生成オブジェクトの削除は E_OBJ
	 */
	check_ercd(del_cyc(CYC1), E_OBJ);
	check_ercd(del_alm(ALM1), E_OBJ);
	check_point(9);

	check_finish(10);
}
```

**注意（実装者へ）:**
- `sil_get_pid` が使えない場合は `get_pid(&prcid)` に置き換える
  （通知ハンドラは非タスクコンテキスト・CPU ロック解除状態で走るので `get_pid` は呼べる。
  `syssvc/test_svc.c` の流儀に合わせること）。
- `E_RSATR` の検査（手順4）は `CHECK_VALIDATR` が `TA_STA` 以外のビットを弾くことに依存する。
  ターゲット固有の追加属性ビットがある場合は使うビットを変える。
- `dly_tsk(TEST_TIME_PROC)` は 200ms。QEMU で周期 20ms が数回発火するのに十分だが、
  ホストが遅い場合は `TEST_TIME_PROC` を大きくする（`check_assert(cyc_count >= 1U)` は
  回数を固定していないので値を増やしても壊れない）。
- **チェックポイント番号は PRC1 が 1..9 + `check_finish(10)`、PRC2 が `(1,2)` の1個だけ。
  各プロセッサ内で単調増加であることを QEMU 出力で確認する。**

- [ ] **Step 5: 登録**
  - `test/MANIFEST` の `test_dcre1.h`（`:40`）の直後に、アルファベット順で
    `test_dcre2.c` `test_dcre2.cfg` `test_dcre2.h` の3行。
  - `test/testexec.rb` の `"dcre1"    => { SRC: "test_dcre1" },`（`:90`）の直後に
    `"dcre2"    => { SRC: "test_dcre2" },`。

- [ ] **Step 6: ビルド・実行**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --preset musca_b1-2core -B build/musca_b1-2core-tdcre2 \
  -DFMP3_APPLNAME=test_dcre2 -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/dcre2-t7-conf.log 2>&1
echo "conf rc=$?"
cmake --build build/musca_b1-2core-tdcre2 > /tmp/dcre2-t7-build.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tdcre2 > /tmp/dcre2-t7-eq.log 2>&1; echo "eq rc=$?"
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre2 --target run \
  > /tmp/dcre2-t7-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre2-t7-run.log
grep -c 'Check point' /tmp/dcre2-t7-run.log
grep 'Check point 1-2 passed' /tmp/dcre2-t7-run.log
pgrep -a qemu
```
期待:
- build rc=0、eq rc=0（**AID_CYC/AID_ALM 有りの実構成で両エンジンがバイト一致**）。
- `TTSP_RESULT: PASS` が実在（rc は見ない）。
- `Check point 1 passed.` 〜 `Check point 9 passed.` の9行 + PRC2 の
  `Check point 1-2 passed.` の1行 = 計10行。
- `pgrep` の出力なし。

- [ ] **Step 7: ★カーネル変異 negative control（生きた経路で）**

`kernel/cyclic.c` の `del_cyc` の
`queue_insert_prev(&free_cyccb, ((QUEUE *) &(p_cyccb->tmevtb)));` を
**一時的にコメントアウト**して再ビルド・再実行する。

```bash
# 変異を入れてから：
cmake --build build/musca_b1-2core-tdcre2 > /dev/null 2>&1
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre2 --target run \
  > /tmp/dcre2-t7-neg.log 2>&1
grep -c 'TTSP_RESULT: PASS' /tmp/dcre2-t7-neg.log   # 期待: 0
grep 'Unexpected\|## ' /tmp/dcre2-t7-neg.log | head
pgrep -a qemu
```
期待: **PASS 行が出ない**こと。手順2で `del_cyc` した後の手順3以降の `acre_cyc` が
`E_NOID` を返し、`check_assert(erid > CYC1)` が失敗して
`## Assertion failed` 相当の行が出る（＝del_cyc の free-list 返却が**生きた経路**である証拠）。

変異を**復元**し、Step 6 を再実行して `TTSP_RESULT: PASS` に戻ることまで確認する。
**`TNUM_PRCID == 1` でしか通らない死んだ分岐への変異は不可**
（段階1 で2度やらかした型。本変異は 2 コア構成で必ず通る経路である）。

- [ ] **Step 8: 非退行 — test_dcre1 と test_int2 の再実行**

```bash
cmake --build build/musca_b1-2core-tdcre1 > /tmp/dcre2-t7-d1-build.log 2>&1; echo "rc=$?"
timeout -k 5 40 cmake --build build/musca_b1-2core-tdcre1 --target run \
  > /tmp/dcre2-t7-d1-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre2-t7-d1-run.log
pgrep -a qemu
timeout -k 5 40 cmake --build build/musca_b1-2core-tint2 --target run \
  > /tmp/dcre2-t7-i2-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre2-t7-i2-run.log
pgrep -a qemu
```
（`build/musca_b1-2core-tdcre1` が無ければ Task 7 Step 6 と同じ流儀で
`-DFMP3_APPLNAME=test_dcre1` で作る。）
期待: 両方とも `TTSP_RESULT: PASS` が実在。

- [ ] **Step 9: 台帳とコミット** — `DIVERGENCE_MAP.md` に
  `test/test_dcre2.c` `test/test_dcre2.cfg` `test/test_dcre2.h` `test/MANIFEST`
  `test/testexec.rb` を追記して：

```bash
git add -A && git commit -m "test(dcre): 動的生成cyc/almの回帰テスト test_dcre2 を追加（2コアQEMU）"
```

---

### Task 8: 最終回帰と台帳整理

**推奨モデル:** 中位（sonnet）

**Files:**
- Modify: `DIVERGENCE_MAP.md`（掃除）・`.superpowers/sdd/progress.md`（記録）

**このタスクではコードを直さない。** 欠陥を見つけたら**記録して報告**し、修正は別コミットに切る。

- [ ] **Step 1: 全9プリセット configure+build**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --preset $p > /tmp/dcre2-t8-conf-$p.log 2>&1; c=$?
  cmake --build build/$p > /tmp/dcre2-t8-build-$p.log 2>&1; b=$?
  echo "$p conf=$c build=$b"
done
```
期待: `polarfire_soc_kit`（実機プリセット）のみ SoftConsole ツールチェーン不在
（`nano.specs`）で**既知の環境ギャップとして fail**。それ**以外の8構成が exit=0**。

- [ ] **Step 2: 全8構成の `tools/cfg_equivalence.sh`**

```bash
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  tools/cfg_equivalence.sh build/$p > /tmp/dcre2-t8-eq-$p.log 2>&1
  echo "$p eq=$?"
done
```
期待: 全て 0。**2 は不合格**（Constraint 11）。

- [ ] **Step 3: QEMU 起動7構成（★プリセットごとに個別実行）**

段階1 Task 7 では `for` ループで全構成を1コマンドに詰めた結果、Bash ツールの
2分タイムアウトに当たり **qemu が孤児化した**。**1プリセット1コマンド**で実行し、
毎回 `pgrep` で残存を確認する。

```bash
timeout -k 5 25 cmake --build build/polarfire_soc_kit-qemu --target run > /tmp/dcre2-t8-run-polarfire.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/dcre2-t8-run-polarfire.log   # 期待: 4
pgrep -a qemu
```
```bash
timeout -k 5 25 cmake --build build/musca_b1 --target run > /tmp/dcre2-t8-run-musca1.log 2>&1
grep -c 'Processor 1 start\.' /tmp/dcre2-t8-run-musca1.log          # 期待: 1
pgrep -a qemu
```
```bash
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/dcre2-t8-run-musca2.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre2-t8-run-musca2.log       # 期待: 2
pgrep -a qemu
```
```bash
timeout -k 5 25 cmake --build build/kria_arm64-1core --target run > /tmp/dcre2-t8-run-arm64-1.log 2>&1
grep -c 'Processor 1 start\.' /tmp/dcre2-t8-run-arm64-1.log         # 期待: 1
pgrep -a qemu
```
```bash
timeout -k 5 25 cmake --build build/kria_arm64 --target run > /tmp/dcre2-t8-run-arm64-4.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/dcre2-t8-run-arm64-4.log     # 期待: 4
pgrep -a qemu
```
```bash
timeout -k 5 25 cmake --build build/kria_r5 --target run > /tmp/dcre2-t8-run-r5-1.log 2>&1
grep -c 'Processor 1 start\.' /tmp/dcre2-t8-run-r5-1.log            # 期待: 1
pgrep -a qemu
```
```bash
timeout -k 5 25 cmake --build build/kria_r5-2core --target run > /tmp/dcre2-t8-run-r5-2.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre2-t8-run-r5-2.log         # 期待: 2
pgrep -a qemu
```
（`rp2350_pico2` は QEMU にマシンモデルが無く `run` ターゲット自体が無い＝設計どおり。）
**各コマンドの後に `pgrep -a qemu` が何も出さないこと。** 出たら
`pkill -f qemu-system` で掃除し、その事実を記録する。

- [ ] **Step 4: テスト3本の再実行**

```bash
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre2 --target run > /tmp/dcre2-t8-tdcre2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre2-t8-tdcre2.log ; pgrep -a qemu
timeout -k 5 40 cmake --build build/musca_b1-2core-tdcre1 --target run > /tmp/dcre2-t8-tdcre1.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre2-t8-tdcre1.log ; pgrep -a qemu
timeout -k 5 40 cmake --build build/musca_b1-2core-tint2 --target run > /tmp/dcre2-t8-tint2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre2-t8-tint2.log ; pgrep -a qemu
```
期待: 3本とも `TTSP_RESULT: PASS` が実在。

- [ ] **Step 5: エラー経路回帰マトリクス（既存8件 + 新規3件）**

```bash
R=tools/cfg_error_tests/run.sh
$R build/polarfire_soc_kit-qemu tools/cfg_error_tests/e_par_creisr_intno_keyerror.cfg E_PAR; echo "1:$?"
$R build/musca_b1-2core tools/cfg_error_tests/musca_b1_e_rsatr_intno_affinity.cfg E_RSATR; echo "2:$?"
$R build/kria_r5-2core  tools/cfg_error_tests/kria_r5_e_rsatr_intno_affinity.cfg  E_RSATR; echo "3:$?"
$R build/musca_b1-2core tools/cfg_error_tests/dcre_aid_in_class.cfg    E_RSATR; echo "4:$?"
$R build/musca_b1-2core tools/cfg_error_tests/dcre_mpk_in_class.cfg    E_RSATR; echo "5:$?"
$R build/musca_b1-2core tools/cfg_error_tests/dcre_mpk_zero.cfg        E_PAR;   echo "6:$?"
$R build/musca_b1-2core tools/cfg_error_tests/dcre_mpk_double.cfg      E_OBJ;   echo "7:$?"
$R build/musca_b1-2core tools/cfg_error_tests/dcre_mpk_misaligned.cfg  E_PAR;   echo "8:$?"
$R build/musca_b1-2core tools/cfg_error_tests/dcre_aid_cyc_in_class.cfg  E_RSATR; echo "9:$?"
$R build/musca_b1-2core tools/cfg_error_tests/dcre_aid_alm_in_class.cfg  E_RSATR; echo "10:$?"
$R build/musca_b1-2core tools/cfg_error_tests/dcre_aid_cyc_no_static.cfg E_OBJ;   echo "11:$?"
```
期待: 11件すべて 0。ファイル名・期待 ercd が実物と食い違ったら
`ls tools/cfg_error_tests/*.cfg` で実在するものに合わせ、**合わせた事実を記録する**
（既存8件の名前は段階1 Task 7 の記録に基づく）。

- [ ] **Step 6: `KERNEL_FCSRCS` 突き合わせ**（AGENTS.md §4。22個のまま不変のはず）

```bash
diff <(awk '/^KERNEL_FCSRCS/{f=1} f{print} f&&!/\\$/{exit}' kernel/Makefile.kernel \
       | grep -oE '[a-z_]+\.c' | sort -u) \
     <(grep -oE 'kernel/[a-z_]+\.c' CMakeLists.txt | sed 's#kernel/##' | sort -u)
echo "diff rc=$?"
```
期待: rc=0（差分なし）。段階2 は既存 `.c` にしか手を入れていないので変化しないはず。

- [ ] **Step 7: `DIVERGENCE_MAP.md` の完全性監査**

```bash
git diff main...HEAD --name-only > /tmp/dcre2-t8-changed.txt
wc -l /tmp/dcre2-t8-changed.txt
while read f; do
  case "$f" in
    docs/*|tools/cfg_error_tests/*|cmake/*|CMakeLists.txt|CMakePresets.json|cfg_py/*|.superpowers/*) continue;;
  esac
  grep -q -- "$f" DIVERGENCE_MAP.md || echo "MISSING: $f"
done < /tmp/dcre2-t8-changed.txt
```
期待: `MISSING:` が**1行も出ない**（pristine の全変更ファイルに1行以上の記録がある）。
段階2 で触った pristine は次のとおり（段階1 分は既に記録済み）：
`arch/arm_m_gcc/common/core_rename.def`＋再生成2、`kernel/kernel_api.def`、
`kernel/kernel.trb`、`kernel/cyclic.trb`、`kernel/alarm.trb`、`include/kernel.h`、
`kernel/check.h`、`kernel/kernel_impl.h`、`kernel/time_manage.c`、`kernel/cyclic.h`、
`kernel/cyclic.c`、`kernel/alarm.h`、`kernel/alarm.c`、`kernel/allfunc.h`、
`kernel/Makefile.kernel`、`kernel/kernel_rename.def`＋再生成2、`test/test_dcre2.*`、
`test/MANIFEST`、`test/testexec.rb`。
漏れが（あってはならないが）見つかったら**理由込みで**追記する。
あわせて「上流報告候補」欄に **arm_m core_rename.def の欠落は段階2 Task 2 で修正済み**である
ことを反映する（段階1最終レビューの候補 b が解消）。

- [ ] **Step 8: `.superpowers/sdd/progress.md` へ段階2完了を記録し、コミット**

記録に含めること（推測と事実を分ける）:
- Task 1 の実装前確認8項目の結論と、それに基づく spec 訂正5件。
- 訂正D（64bit で `tmevtb.callback` が QUEUE に上書きされる）が dcre からの**意図的な逸脱**であること。
- `msta_cyc`/`msta_alm` の E_NOEXS 挿入は**上流に先例が無い類推適用**であること。
- `msta_*` のロック前 inib 読み（TA_NOEXS スロットの affinity 未定義値）は
  **段階1 deferred #1 と同型の残課題**として引き継ぐこと。
- §5.2 の残る未防御窓（`call_cyclic`/`call_alarm` のハンドラ呼出し式が glock 解放後に
  inib を読む）が**受容**であること。
- 段階3（sem/flg/dtq/pdq/mtx/mpf）への引き継ぎ：訂正E で入れた「AID>0 かつ静的0個は
  cfg エラー」は共通枠組みなので他オブジェクトにも自動適用される。dtq/pdq/mpf は
  管理領域の `malloc_mpk` が要るため段階1 の mempool 受容判断（プール再利用の
  理論的重なり）を**着手前に再評価する**こと。

```bash
git add -A && git commit -m "chore(dcre): 段階2の最終回帰と台帳整理"
```

---

## Self-Review 済み事項（計画作成時の検証記録）

**spec 要件 → Task 対応:**

| spec | 内容 | Task |
|---|---|---|
| §1.1 | `T_CCYC`/`T_CALM` パケット型 | T6 Step 1（`T_NFYINFO` 本体は T3 Step 3 ＝訂正B） |
| §1.2 | 4サービスコール宣言・機能コード | T6 Step 1（コードは既存＝T1 Step 2 で確定） |
| §1.3 | エラーコード（E_PAR/E_NOID/E_NOEXS/E_OBJ・E_NOMEM 無し） | T6 Step 2-5、T7 手順3/4/7 |
| §2.1 | `kernel_api.def` の2行 | T3 Step 2 |
| §2.2 | KernelObject 共通枠組み（変更不要）＋★訂正E | T3 Step 6（ガード追加）、T1 Step 8 |
| §2.3 | cyclic/alarm の nfyinfo テーブル出力・サイズトークン | T3 Step 4-5（サイズトークンは共通枠組みが自動） |
| §2.4 | 管理された差分 | T3 Step 8 |
| §3.1 | free-list（tmevtb 転用）＋★訂正D | T5 Step 3-4、T6 Step 2/4 |
| §3.2 | `initialize_cyclic`/`initialize_alarm` | T5 Step 3-4 |
| §3.3 | `acre_cyc`/`acre_alm` | T6 Step 2/4 |
| §3.4 | `del_cyc`/`del_alm` | T6 Step 3/5 |
| §3.5 | ID マクロの2レンジ化 | T1 Step 3（判断）→ T5 Step 1-2（実装） |
| §3.6 | allfunc.h / Makefile.kernel / rename | T4 Step 4-5、T5 Step 5、T6 Step 7 |
| §4 | ランタイム通知機構 | T4（全体） |
| §5.1 | E_NOEXS 8関数 | T6 Step 6 |
| §5.2 | MP 安全性の論証・PRC1 の p_tevtcb 前提 | T1 Step 5/7 → spec §5.2 確定 |
| §6 | arm_m `core_rename.def` 修正 | T2（全体） |
| §7 | test_dcre2 の7シナリオ | T7 Step 4（手順1-8）+ Step 7（変異 control） |
| §8 | 統治（台帳・全構成回帰・段階3への引き継ぎ） | T8 |
| §9 | 実装前確認8項目 | T1 Step 1-8 |

**現物確認済み（計画作成時に実ファイルで確認した事実）:**
- `T_NFYINFO` 系の型が FMP3 `include/kernel.h` に**無い**こと、`TNFY_*`/`TENFY_*` 定数は
  `include/kernel.h:447-465` に**ある**こと。
- `TFN_ACRE_CYC (-202)` / `TFN_ACRE_ALM (-203)` / `TFN_DEL_CYC (-218)` /
  `TFN_DEL_ALM (-219)`（`include/kernel_fncode.h:142,143,154,155`）。
- FMP3 に `CYCID()`/`ALMID()` マクロが**存在せず**、CB は `CYCCB *const p_cyccb_table[]`
  （`kernel/cyclic.h:93`）＝**ポインタ表**であること（dcre は `cyccb_table[]` 実体配列で
  `cyclic.h:107` に `CYCID` を持つ）。
- `TMEVTB`（`kernel/tmevt.h:72-77`）と `QUEUE`（`include/queue.h`）のレイアウト、
  および 64bit で `sizeof(QUEUE)=16 > offsetof(TMEVTB, callback)=8` となること。
- `TOPPERS_TEPP_PRC` が `target/*/target_kernel.h` で定義され `include/kernel.h:69` 経由で
  カーネル C から見えること。musca_b1 は 1コア 0x1 / 2コア 0x3、kria_r5 は
  `(1U << TNUM_PRCID) - 1U`、polarfire/kria_arm64 は 0xf。
- 静的 cyc/alm のクラス affinity を `TOPPERS_TEPP_PRC` で制約する検査が
  `kernel/cyclic.py:65-68` / `kernel/alarm.py:59-62`（および対応する `.trb`）にあること。
- `check_nfyinfo`＝dcre `kernel/time_manage.c:225`（`TOPPERS_chknfy`）、
  `notify_handler`＝同 `:309`（`TOPPERS_nfyhdr`）、宣言は dcre `kernel_impl.h:287,292`、
  rename は dcre `kernel_rename.def:100-101`。FMP3 側は0件。
- `INTPTR_NONNULL` が FMP3 `kernel/check.h` に**無い**（`INTPTR_ALIGN` は `:390-394` にある）。
- `call_cyclic`（`kernel/cyclic.c` の `TOPPERS_cyccal`）と `call_alarm`（`TOPPERS_almcal`）が
  ハンドラ復帰後に CB を再参照**しない**こと。
- 段階1 が `kernel.py:110-253` / `kernel.trb:115-260` で `@aidapi`/`inibList`/`inibSizeToken`/
  予約 CB を**オブジェクト非依存に**一般化済みであること（cyclic/alarm 側は `inibList` への
  1行追加で足りる）。データ構造ガードが `len(cfgData[self.api]) > 0`（静的個数のみ）であること。
- `check_point(count)` = `check_point_prc(count, 0)` = `check_count[0]`、
  `check_point_prc(count, 2)` = `check_count[1]`（`syssvc/test_svc.h:112`、
  `syssvc/test_svc.c` の `check_point_prc`）＝**プロセッサ別独立カウンタ**。
  段階1 の `test/test_dcre1.c` が実際に `check_point_prc(1, 2)` を使って PASS していること。
- `sample/sample1.cfg` が `CRE_CYC`/`CRE_ALM` を含むこと（`:24-25` ほか）＝
  Task 3 の positive control / compile-through control に使えること。
- `kernel/interrupt.py:433-435` / `kernel/interrupt.trb:462-464` が
  `_kernel_sense_lock`/`_kernel_unlock_cpu` のみをリネーム後の名前で出力すること
  （`_kernel_lock_cpu` は出力しない）。`arch/arm64_gcc/common/core_rename.def` の
  `# core_kernel_impl.h` 節が `lock_cpu`/`unlock_cpu`/`sense_lock` を持つこと。
- `offsetof`（`include/t_stddef.h:223-226`）、`TMAX_RELTIM`（`include/t_stddef.h:287`）、
  `E_NOEXS = -42`（`include/t_stddef.h:188`）、`TOPPERS_MASTER_PRCID = PRC1`
  （`target/musca_b1_gcc/target_kernel.h:36`）。

**未検証（実装者が最初に当たること）:**
- **全8プリセットの `p_tevtcb_table[]` 第1要素が非 NULL であること**（T1 Step 5）。
  ここが崩れると Constraint 4 が成立せず設計が無効になる。**確認せずに先へ進まないこと。**
- `sil_get_pid()` が `test/` のアプリから使えるか（T7 Step 1 で `test_svc.c` の流儀を確認）。
- `kernel/time_manage.c` が `check.h` を include しているか（T4 Step 3）。
- Task 3 Step 13 の3つのエラー cfg が「目的のエラーだけ」を出すか
  （`CRE_CYC` の par1 に何を渡すと他のエラーで先に落ちないか）。
- `E_RSATR` テスト（T7 手順4）で使う「未定義の属性ビット」が
  musca_b1 のターゲット固有属性と衝突しないか。
- 既存エラー回帰8件の**正確なファイル名**（T8 Step 5。段階1 Task 7 の記録に基づくが未照合）。
