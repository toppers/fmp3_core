# FMP3 動的生成API（dcre 相当）サポート 設計書

**Goal:** ASP3 の動的生成拡張パッケージ（`extension/dcre`）と同等の動的生成
サービスコール群を FMP3（マルチプロセッサ）へ移植し、`feature/dynamic-creation`
ブランチ上で実装・検証する。

**Architecture:** dcre の機構（cfg 予約スロットの free-list ＋ RAM 側
初期化ブロック `atinib_table[]`）を忠実に移植し、FMP3 固有の3点 —
ジャイアントロック、TINIB の `iprcid`/`affinity` フィールド、
「named-static CB ＋ const ポインタ表」レイアウト — を局所的に適応させる。
cfg は Ruby（オラクル）・Python（製品）の両エンジンを同時に拡張し、
差分等価性検査を主検査として維持する。

**Tech stack:** C（カーネル）、Python/Ruby（cfg テンプレート）、
CMake、QEMU 回帰テスト。

---

## Global Constraints（ユーザ決定事項。実装中に再解釈しない）

1. ブランチ `feature/dynamic-creation`。**main へはマージしない。**
   pristine への改変はブランチ上でも `DIVERGENCE_MAP.md` に記録する。
2. 段階スコープ：**段階1** = `acre_tsk`/`del_tsk` + `DEF_MPK`。
   **段階2** = `acre_cyc`/`del_cyc`・`acre_alm`/`del_alm`・`acre_isr`/`del_isr`。
   **段階3**（後続計画・本設計の実装範囲外だが互換な形にする）= sem/flg/dtq/pdq/mtx/mpf。
3. API 面は dcre 標準のみ：サービスコール・パケット型（`T_CTSK` 等）・
   静的API `AID_*`/`DEF_MPK`。**独自APIなし。`acre_*` にクラス引数なし。**
4. MP 意味論：動的生成オブジェクトは **`iprcid = 1`（PRC1）、
   `affinity = 全プロセッサ**（`(1 << TNUM_PRCID) - 1`）をカーネルが固定で埋める。
   `del_*` は休止（quiescent）状態のみ（dcre 意味論）。
5. 検証 = **F-1**：dcre の `.trb` 変更を FMP3 の pristine Ruby テンプレートにも
   移植し、`tools/cfg_equivalence.sh`（Ruby-vs-Python バイト比較）を主検査として
   維持する。Ruby `.trb` の編集は pristine 編集 → 台帳行。
6. 機構は dcre 忠実移植：free-list + RAM `atinib_table[]`。CB はヒープ確保しない。
   スタックは呼び出し側供給か `DEF_MPK` プールから。

---

## 1. 背景と現状（調査で確定した事実）

- **FMP3 に dcre の配線は一切ない。** `acre_*`／`del_*`／`AID_*`／`DEF_MPK` は
  `include/kernel.h`・`kernel/kernel_api.def`（18行、`CRE_*`/`DEF_*`/`ATT_*` のみ）の
  どこにもない。`T_CTSK` 等のパケット型も未定義。
- 例外が2つ、**むしろ追い風**：
  - `kernel/kernel_impl.h:199` に `#define TA_NOEXS ((ATR)(-1))` が**未使用のまま
    存在**する（ASP3 系譜の名残）。dcre と同名・同値なのでそのまま使う。
  - `include/kernel_fncode.h:134-141` に **`TFN_ACRE_TSK (-193)` 等の機能コードが
    既に定義済み**。動的生成サービスコールは FMP3 の機能コード空間に元から
    予約されている＝「標準API」であることの傍証。
- **生成物は余白ゼロ**：`build/musca_b1-2core/generated/kernel_cfg.c:121` で
  `const TINIB _kernel_tinib_table[TNUM_TSKID]`（ROM・ぴったりサイズ）、
  TCB は個別の named static（同:136-147 `static TCB _kernel_tcb_TASK1_1;` …）、
  `TCB *const _kernel_p_tcb_table[TNUM_TSKID]`（同:149-162、const ポインタ表）。
- **`iprcid`/`affinity` 問題**：`kernel/task.h` の TINIB 末尾2フィールド
  （初期割付プロセッサ・割付可能ビットマップ）は、cfg の `CLASS(){}` →
  `target/*/target_class.py` の静的表からしか埋まらない
  （`cfg_py/pass2.py:376-377` が CLSIDX→`params["class"]` を注入、
  `kernel/task.py:93-99` が `cls['initPrc']`/`cls['affinityPrcBitmap']` を
  TINIB 初期化子に出力）。実行時にこの経路は存在しない → 本設計は
  Global Constraint 4 の固定値で解決する。
- ASP3 dcre の機構（確認済み）：`AID_TSK(n)` で予約 → `TNUM_TSKID` が
  静的+動的の総数になり、`TNUM_STSKID`（静的数）が別に出る。CB は
  free-list（`free_tcb`）管理、動的スロットの TINIB は RAM の
  `atinib_table[]`。`TSKID(p_tcb)` は ASP3 では TCB 連続配列ベースのため
  無改変で済んだ（dcre の task.trb 差分は torder_table のサイズ変更
  **1行だけ**）。FMP3 は TINIB ベース＋非連続 TCB なのでここに適応が要る（§4.3）。

---

## 2. API 仕様

### 2.1 サービスコール（段階1）

```c
ER_ID acre_tsk(const T_CTSK *pk_ctsk);
ER    del_tsk(ID tskid);
```

```c
typedef struct t_ctsk {
    ATR     tskatr;     /* タスク属性 */
    EXINF   exinf;      /* タスクの拡張情報 */
    TASK    task;       /* タスクのメインルーチンの先頭番地 */
    PRI     itskpri;    /* タスクの起動時優先度 */
    size_t  stksz;      /* タスクのスタック領域のサイズ */
    STK_T   *stk;       /* タスクのスタック領域の先頭番地 */
} T_CTSK;
```

dcre の `include/kernel.h` 定義をそのまま使う（フィールド追加なし。
クラス／プロセッサ指定は**持たせない**）。`TA_MEMALLOC (0x8000)` を
`kernel_impl.h` に追加（dcre DIFF:2201-2203 と同一）。

### 2.2 `acre_tsk` の意味論

1. `CHECK_PAR`：`tskatr` の有効性（`TA_ACT|TA_NOACTQUE|TA_MEMALLOC` 以外の
   ビットは E_RSATR）、`itskpri` 範囲（E_PAR）、`stksz` 最小値・アライン（E_PAR）。
2. `lock_cpu(); acquire_glock();`（§4.1）。
3. `free_tcb` が空なら **E_NOID**。
4. TCB を pop、対応する `atinib_table` スロットへ `pk_ctsk` の内容と
   **固定値 `iprcid = 1`・`affinity = (1 << TNUM_PRCID) - 1`** を書く。
5. `stk == NULL` なら `malloc_mpk(stksz)`（プール未登録／枯渇なら **E_NOMEM**）、
   `tskatr |= TA_MEMALLOC`。
5.5. **ARM-M: `USE_TSKINICTXB` 対応**
   `arch/arm_m_gcc/common/core_kernel_impl.h:113` が `USE_TSKINICTXB` を定義し、
   TINIB は `stksz`/`stk` でなく `TSKINICTXB { uint32_t *stk_top; uint32_t *stk_bottom; }`
   （同:115-118）を持つ。主検証ターゲット musca_b1 がまさに ARM-M。
   → `acre_tsk`/`del_tsk` は dcre と同じ `#ifdef USE_TSKINICTXB` 分岐を持ち、
   ARM-M 側に `init_tskinictxb()` と `tskinictxb_memalloc_ptr()`（内部ヘルパ、Task 4）
   を追加する。ASP3 3.7 で USE_TSKINICTXB を使う arch は posix のみ
   （`posix_kernel_impl.h:204`）でスタック実体を持たないため、
   **この組み合わせに上流前例は無い**。
6. `p_tcb->p_pcb` を PRC1 の PCB に設定し `make_dormant()`。
   `TA_ACT` なら `make_active(PRC1のPCB, p_tcb)`。
7. 返値 = `TSKID(p_tcb)`（動的レンジの ID、§4.3）。

### 2.3 `del_tsk` の意味論（E_OBJ 条件の確定）

- `VALID_TSKID` でない → E_ID。
- `tskid <= tmax_stskid`（静的タスク）→ **E_OBJ**。
- `p_tinib->tskatr == TA_NOEXS` → **E_NOEXS**。
- `!TSTAT_DORMANT(p_tcb->tstat)` → **E_OBJ**。
- 上記を通れば：`TA_MEMALLOC` 付きならスタックを `free_mpk`、
  `tskatr = TA_NOEXS`、TCB を `free_tcb` へ push。

**休止状態の保証（MP 安全性、`kernel/task.c`/`task_term.c` で確認）**：
FMP3 の休止タスクはどのコアにも実行文脈・レディキュー登録・タイマイベントを
持たず、`p_lastmtx == NULL`。actque 付きで終了したタスクは終了処理内で即
再起動されるため、**ジャイアントロック下で DORMANT を観測した時点で
`actque == false` が保証される**。したがって dcre と同一の条件で MP でも安全。
追加検査は不要（すべての状態遷移がジャイアントロックで直列化される、§4.1）。

**受容する残余ウィンドウ（自終了直後のスタック使用と del_tsk/プール再利用の理論的な重なり）**：
`ext_tsk`（`task_term.c:159-164`）は giant lock 下で DORMANT を設定した後、
`release_glock(); exit_and_dispatch();` の間、終了中のコアが自タスクのスタック上で
数命令実行を続ける。他コアはこの間に `TSTAT_DORMANT` を観測して `del_tsk` → `free_mpk`
でき、プールの全割当が解放されると（dcre アロケータは `count == 0` で先頭から再利用）
次の `acre_tsk` が同一メモリを新タスクへ渡し得る。この有効化条件は pristine FMP3 の
自終了経路そのもの（`mact_tsk` による静的タスク再活性化でも同型の窓がある）に由来し、
窓は CPU ロック下の数命令。段階1では受容する（テストからは到達不能）。段階2
（dtq/pdq/mpf の管理領域でプール往復が増える）までに、受容の継続かプール再利用の
遅延等の hardening かを判断する。

### 2.4 既存サービスコールへの E_NOEXS 検査追加

dcre と同型：`get_tcb()` 後に
`if (p_tcb->p_tinib->tskatr == TA_NOEXS) { ercd = E_NOEXS; }` を、
段階1ではタスク系サービスコール（`task_manage.c`／`task_refer.c`／
`task_sync.c`／`task_term.c`／`time_manage.c` の該当関数）に追加する
（dcre の task_manage.c:224,274,327,367,437 等と同位置）。

---

## 3. cfg 仕様

### 3.1 静的API

```
AID_TSK(uint_t notsk);          /* 動的生成用タスクスロットを notsk 個予約 */
DEF_MPK({ size_t mpksz, MB_T *mpk });  /* カーネルメモリプール（最大1個、E_OBJ） */
```

`kernel/kernel_api.def` へ dcre と同一の行を追加（dcre kernel_api.def:2,25 の実物）：
`AID_TSK .notsk` ／ `DEF_MPK { .mpksz &mpk? }`。

### 3.2 `CLASS(){}` との関係（決定：クラス外専用）

**`AID_*`／`DEF_MPK` はクラスの囲みの外に書く。囲み内に書かれたら E_RSATR。**

根拠（最小変更の選択）：
- cfg フレームワークはクラスを**任意注入**する：`cfg_py/pass1.py:677` が
  CLASS 内の API にだけ `CLSIDX` を付け、`cfg_py/pass2.py:376-377` が
  `params["class"]` を条件付きで足す。クラス外 API はフレームワーク変更
  **ゼロ**で通る。
- クラス必須なのはオブジェクト生成 API 側の検査
  （`kernel/kernel.py:412-414`「%apiname must be within a class」）であって、
  枠組みの制約ではない。**クラス外専用 API の前例**が既にある：
  `ENA_SPR` は逆に「クラス内なら E_RSATR」と検査している
  （`kernel/task.py:141-142`）。`AID_*` はこの前例に従う。
- クラスに入れても使い道がない：動的スロットの `iprcid`/`affinity` は
  固定値（Constraint 4）、スタックは cfg が確保しない（実行時供給）、
  予約 TCB/atinib はセクション属性なしの平置き static（§3.3。既存 TCB が
  そうであることを `kernel_cfg.c:136-147` で確認済み）。クラス情報を
  読む場所が存在しない。

### 3.3 生成コードの差分（`kernel_cfg.c`/`kernel_cfg.h`）

`AID_TSK(2)` があるとき（現行生成物 `kernel_cfg.c:107-167` からの差分）：

```c
/* kernel_cfg.h：総数（静的+動的）に意味が変わる */
#define TNUM_TSKID    14        /* 12(静的) + 2(AID_TSK) */

/* kernel_cfg.c */
#define TNUM_STSKID   12        /* 静的タスク数（新設、dcre と同名） */
const ID _kernel_tmax_tskid  = (TMIN_TSKID + TNUM_TSKID - 1);
const ID _kernel_tmax_stskid = (TMIN_TSKID + TNUM_STSKID - 1);   /* 新設 */

const TINIB _kernel_tinib_table[TNUM_STSKID] = { ... };  /* サイズが静的数に */
TINIB _kernel_atinib_table[2];                            /* 新設（RAM・非const） */

static TCB _kernel_tcb_TASK1_1;  /* 既存の named static はそのまま */
...
static TCB _kernel_atcb_1;       /* 新設：予約スロット用 named static */
static TCB _kernel_atcb_2;

TCB *const _kernel_p_tcb_table[TNUM_TSKID] = {
    ...既存12個...,
    &_kernel_atcb_1, &_kernel_atcb_2      /* 末尾に予約分（ID順で動的レンジ） */
};

const ID _kernel_torder_table[TNUM_STSKID] = { ... };  /* 静的分のみ（dcre 同） */
```

- `AID_TSK` が無い場合：`TNUM_TSKID == TNUM_STSKID`、
  `TOPPERS_EMPTY_LABEL(TINIB, _kernel_atinib_table);`（dcre の
  EMPTY_LABEL パターンを踏襲）。**ただし本リポジトリのカーネルは
  `ALLFUNC` で全関数コンパイルされる**（`CMakeLists.txt:560-562`）ため、
  `initialize_task`（task.c）・`del_tsk` が参照する `_kernel_tmax_stskid`・
  `_kernel_atinib_table`・`_kernel_mpksz`・`_kernel_mpk` は
  **AID/DEF_MPK の有無に関わらずリンクに必要**。dcre 自身も無条件に新形式を
  出力する（dcre DIFF:2129 以降の kernel.trb 差分は `TNUM_#{OBJ}ID` の定義や
  inib サイズトークンを無条件に変更している）。→ **代替検査**（§6.1 参照）：
  AID 無し構成の生成物の変更前後 diff が「Task 2 Step 7 の許容リスト」と
  完全一致すること（それ以外の差分は不合格）。Ruby-vs-Python の等価性検査
  （真のゲート）は全構成 exit=0 を維持する。
- 静的タスク0個は元から cfg エラー（`kernel/task.py:104-105`）なので
  tinib_table の EMPTY_LABEL 化は起きない。

### 3.4 変更するテンプレート（両エンジン対）

| 変更内容 | Python（製品） | Ruby（F-1 オラクル、pristine 編集→台帳） |
|---|---|---|
| AID 集計・TNUM_S*/tmax_s*/atinib/予約TCB/ポインタ表 | `kernel/kernel.py`（KernelObject 枠組み）・`kernel/task.py` | `kernel/kernel.trb`・`kernel/task.trb` |
| DEF_MPK → `mpksz`/`mpk` 出力 | `kernel/kernel.py` | `kernel/kernel.trb` |
| パス3 検査（シンボル） | `kernel/kernel_check.py` | `kernel/kernel_check.trb` |

`kernel/kernel_api.def` は両エンジン共用（cfg_py も pristine の .def を読む）。
pass1/pass2 フレームワーク（`cfg_py/pass1.py`/`pass2.py`）は**変更しない**
（§3.2 の設計により不要）。

---

## 4. カーネル実装方式

### 4.1 ロック規約（確認済みの既存パターンに従う）

FMP3 のサービスコールは `lock_cpu(); acquire_glock(); … release_glock();
unlock_cpu();` のジャイアントロック方式
（`kernel/task_manage.c:152-153` の `act_tsk` で確認。
`acquire_glock()` は `acquire_lock(&giant_lock)`、
`arch/riscv_gcc/common/core_kernel_impl.h:524`）。
`acre_tsk`/`del_tsk`／free-list 操作／`atinib_table` 書き込み／mpk 操作は
すべてこの規約下で行う。ASP3 dcre の `lock_cpu()` 単独をこれに置換するのが
唯一のロック適応。

### 4.2 初期化（`initialize_task` の分割、MP 適応）

FMP3 の `initialize_task(PCB *p_my_pcb)` は各プロセッサで走り、
`tinib_table[j].iprcid == 自分の prcid` のタスクだけ初期化する
（`kernel/task.c:58-89`）。動的スロットは TINIB が未定なので：

- **PRC1 の `initialize_task` だけ**が（`p_my_pcb->prcid == 1` のとき）
  予約スロットを初期化する：`p_tcb->p_tinib = &atinib_table[i]`（恒久対応付け、
  del しても切らない）、`atinib_table[i].tskatr = TA_NOEXS`、
  `p_tcb->p_pcb = PRC1 の PCB`、`free_tcb` へ push。
- `free_tcb` キュー自体の初期化も PRC1 が行う（`queue_initialize`）。
  他プロセッサが `initialize_task` 中に free_tcb を触ることはない
  （torder_table は静的分のみ＝動的スロットを走査しない）。

`sta_ker` の mpk 初期化（dcre startup.c:111-116 と同じ
`mpk_valid = initialize_mempool(mpk, mpksz)`）は**マスタプロセッサのみ**、
`initialize_object()` 群より前・バリア同期の内側で1回行う。

### 4.3 `TSKID` マクロの適応（FMP3 固有・要注意箇所）

現行 `kernel/task.h:313` は
`#define TSKID(p_tcb) ((ID)(((p_tcb)->p_tinib - tinib_table) + TMIN_TSKID))`
で **TINIB オフセットから ID を出す**。動的タスクの `p_tinib` は
`atinib_table` を指すためこの式は使えない（ASP3 は TCB 連続配列ベースで
無傷だった。dcre task.trb 差分が1行で済んだ理由）。適応：

```c
#define TSKID(p_tcb) \
    ((((p_tcb)->p_tinib >= atinib_table) \
        && ((p_tcb)->p_tinib < &atinib_table[tnum_tsk - tnum_stsk])) \
      ? ((ID)(((p_tcb)->p_tinib - atinib_table) + TMIN_TSKID + tnum_stsk)) \
      : ((ID)(((p_tcb)->p_tinib - tinib_table) + TMIN_TSKID)))
```

atinib_table と tcb（予約分）は §4.2 で恒久1:1対応なのでこれで一意。
（異配列間ポインタ比較は規格上未定義だが、TOPPERS 既存コードの慣行に従い
リンカ配置前提で許容する。気になる場合は `tskatr == TA_NOEXS` 判定と
組み合わせず、この式単独で閉じる。）
`get_tcb()`／`VALID_TSKID` は `p_tcb_table[TNUM_TSKID]`／`tmax_tskid`（総数）
経由なので**無改変で動的 ID に対応**する。

### 4.4 メモリプール（dcre 忠実移植）

`initialize_mempool`/`malloc_mempool`/`free_mempool` を `kernel/startup.c` へ、
`malloc_mpk`/`free_mpk` inline と `mpk_valid` 宣言を `kernel/kernel_impl.h` へ
（dcre DIFF:2238-2266 の通り）。アロケータは dcre のもの（TLSF 等ではない
単純 first-fit）をそのまま。ジャイアントロック下でのみ呼ばれるため
それ自体の排他は不要。

### 4.5 段階3への互換性

dcre の枠組み（`@aidapi`/`@noobj`/`@inibList` の一般化、DIFF:kernel.trb 部）を
kernel.py/kernel.trb の**オブジェクト共通基盤**として実装する（タスク専用に
書かない）。sem/flg 等は同期オブジェクトで TCB のような per-processor 初期化が
なく、CB も named static + ポインタ表の同型なので、段階1の型がそのまま使える。
ISR（段階2）のみ `iprcid`/`affinity` を interrupt.h 側 inib に持つ点で
タスクと同型の固定値埋めが要る。

---

## 5. サービスコール配線（新規コールが触るファイルの全列挙）

`act_tsk` の配線を追跡して確定した手順。新規コール1個あたり：

1. `include/kernel.h` — 宣言＋パケット型（fncode は
   `include/kernel_fncode.h:134-` に**既存**、追加不要）。
2. `kernel/task_manage.c` — 実装。`#ifdef TOPPERS_acre_tsk` で囲む。
3. `kernel/allfunc.h` — `TOPPERS_acre_tsk`/`TOPPERS_del_tsk` を追加。
   本リポジトリの CMake は `ALLFUNC` 定義で全関数コンパイル
   （`CMakeLists.txt:560-562`）のため、これだけで組み込まれる。
   `kernel/Makefile.kernel` の per-function .o リストも dcre に倣い更新する
   （CMake は参照しないが上流形式維持のため。`KERNEL_FCSRCS` は**不変**＝
   新規 .c ファイルなし、AGENTS.md §4 の突き合わせに影響しない）。
4. `kernel/kernel_rename.def` — 新規内部シンボル（`free_tcb`・`atinib_table`・
   `tmax_stskid`・`mpk`・`mpksz`・`mpk_valid`・`initialize_mempool`・
   `malloc_mempool`・`free_mempool`）を追加し、
   **`utils/genrename.rb` で `kernel_rename.h`/`kernel_unrename.h` を再生成**
   （両ファイル先頭に「generated from kernel_rename.def by genrename」とあり、
   手書きではない）。
5. `kernel/kernel_sym.def` — cfg パス3 が参照するシンボルの追加（dcre
   DIFF:2451-2474 に倣う）。
6. `kernel/check.h` — `ALIGNED`/`STKSZ_ALIGN` 等の検査マクロ追加
   （dcre DIFF:569-600）。

---

## 6. 検証

### 6.1 差分等価性（主検査・F-1）

**真のゲート**（Ruby-vs-Python バイト一致）：
- 全既存8ビルド構成で `tools/cfg_equivalence.sh` **exit=0 を維持**。
- 新設プリセットではなく既存構成の `.cfg` に `AID_TSK(2); DEF_MPK(...);` を
  足した派生 `.cfg` で Ruby/Python の生成物バイト一致を確認する
  （`tools/cfg_error_tests/run.sh` の流儀で、ビルド外で両エンジンを回す）。

**AID 無し構成の生成物**（管理された差分による検査）：
既存構成（`AID_TSK` なし）の生成物の変更前後 diff が「Task 2 Step 7 の
許容リスト」**と完全一致**することで検査する（それ以外の差分は不合格）。
本リポジトリのカーネルは `ALLFUNC` で全関数コンパイルされるため、
動的生成機構の記号（`_kernel_tmax_stskid` 等）は AID/DEF_MPK の有無に関わらず
リンクに必要であり、§3.3 の通り代替検査とする。

### 6.2 QEMU 回帰テスト（`test/test_dcre1` — `test_int2` の形式に倣う）

シナリオ（1本のテストプログラムで順に）：
1. `acre_tsk` → 返却 ID が動的レンジ（`> tmax_stskid`）であること。
2. `act_tsk` → 実行 → 自然終了 → `del_tsk` → E_OK。
3. 再 `acre_tsk` → **同じ ID が再利用される**こと（free-list LIFO）。
4. スロット数+1 回目の `acre_tsk` → **E_NOID**。
5. `del_tsk`(静的タスクID) → **E_OBJ**、`del_tsk`(実行中の動的タスク) → **E_OBJ**、
   削除済み ID へのサービスコール → **E_NOEXS**。
6. マルチコア構成（musca_b1-2core 等）：PRC1 で `acre_tsk` →
   `mact_tsk(id, 2)` で **PRC2 起動** → PRC2 上で走行したことを
   `get_pid` で確認 → 終了 → `del_tsk`。affinity=全コアの実証。
7. `stk=NULL` + `DEF_MPK` あり → 自動確保で E_OK／`DEF_MPK` なし → E_NOMEM。

### 6.3 検証の作法（本プロジェクトの規律）

- **positive control**：6.2 の各エラーケースは「正しく E_XXX が返る」ことを
  値で検査（`check_ercd`）。
- **negative control**：`AID_TSK` を書かない構成で生成物の差分が許容リスト一致
  （§6.1）。壊れた検査対策として、テンプレートに意図的な off-by-one を
  仕込み cfg_equivalence が exit=1 になることを一度実演してから戻す
  （kria_r5 でやったのと同じ手順）。
- rc=124 単独を成功判定に使わない（出力の実在を grep で確認）。

---

## 7. 段階計画

- **段階1**（本設計の主実装）：§2〜§6 のタスク＋mpk。
  完了条件 = 6.1〜6.3 全通過。
- **段階2**：cyc/alm（**ISR を除外** — 2026-08-04 訂正）。cyclic/alarm は inib に
  `iprcid`/`affinity` 同型フィールドあり、段階1の共通基盤（§4.5）に各1オブジェクト分の
  型当てはめ。E_NOEXS 検査は `cyclic.c`/`alarm.c` の参照系（msta_* 含む8関数）に追加。
  詳細は `2026-08-04-fmp3-dcre-stage2-cyc-alm-design.md`。
  **訂正（2026-08-04）**: 本項の旧記述「ISR は `interrupt.h:82-86` の inib 同型」は
  誤り。当該行の実体は INTINIB（割込み線の管理ブロック）であり、FMP3 には
  ランタイム ISR オブジェクト（ISRINIB/ISRCB/isr_queue）が存在しない（ISR は
  cfg 生成時に intno ごとの平坦な呼出し列 `_kernel_inthdr_<intno>` へ消し込まれる）。
  dcre の `acre_isr`/`del_isr` の移植は「intno ごとのランタイム優先度付きキュー +
  ISRCB + free-list + call_isr 走査」というサブシステムの新規構築（かつ dcre が
  持たない MP 対応の新規設計）に相当するため、段階2から外し、着手時に専用 spec を
  書く（別計画）。
- **段階3**（後続計画・別 spec 不要、本 spec §4.5 を根拠に計画のみ書く）：
  sem/flg/dtq/pdq/mtx/mpf。dtq/pdq/mpf は管理領域の `malloc_mpk` 確保
  （dcre DIFF:1022,2658 の型）が加わる。

## 8. 統治

- pristine 編集（`kernel/*.{c,h}`・`*.trb`・`kernel_api.def`・
  `kernel_rename.def`・`include/kernel.h`・`check.h` 等）は**1ファイル1行以上**
  `DIVERGENCE_MAP.md` に記録。種別は `mod (dcre-port)` で統一し、
  「上流報告」欄は `-`（本ブランチ限りの拡張のため）。
- 本ブランチへの上流追従は `git merge upstream` を main と同様に行える。
  衝突時は台帳の `dcre-port` 行が根拠になる。
- `.py`（cfg_py テンプレート）は派生ファイルなので台帳不要（従来通り）。
