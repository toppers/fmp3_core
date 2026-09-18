# FMP3 動的生成 API（dcre 相当）

本書は，`feature/dynamic-creation` ブランチで実装した動的生成サービスコール群
（ASP3 の動的生成拡張パッケージ `extension/dcre` 相当）の利用者向けリファレンス
である。設計判断の根拠や検証の詳細は各設計書・台帳を参照のこと（各節末に相対パス
で示す）。本書はブランチ内でのみ管理し，**`main` へはマージしない**。

---

## 1. 概要

FMP3（マルチプロセッサ対応 TOPPERS カーネル）に，ASP3 の dcre 拡張パッケージと
同等の動的生成 API を移植したものである。対象オブジェクトは dcre 標準の全種類
（タスク・セマフォ・イベントフラグ・データキュー・優先度データキュー・
ミューテックス・固定長メモリプール・周期通知・アラーム通知）に加え，
割込みサービスルーチン（ISR）を「ハイブリッド方式」で対応した。

| オブジェクト | 静的API | サービスコール |
|---|---|---|
| タスク | `AID_TSK`/`DEF_MPK` | `acre_tsk`/`del_tsk` |
| 周期通知 | `AID_CYC` | `acre_cyc`/`del_cyc` |
| アラーム通知 | `AID_ALM` | `acre_alm`/`del_alm` |
| セマフォ | `AID_SEM` | `acre_sem`/`del_sem` |
| イベントフラグ | `AID_FLG` | `acre_flg`/`del_flg` |
| ミューテックス | `AID_MTX` | `acre_mtx`/`del_mtx` |
| データキュー | `AID_DTQ` | `acre_dtq`/`del_dtq` |
| 優先度データキュー | `AID_PDQ` | `acre_pdq`/`del_pdq` |
| 固定長メモリプール | `AID_MPF` | `acre_mpf`/`del_mpf` |
| ISR | `AID_ISR`/`ENA_DYNISR` | `acre_isr`/`del_isr` |

機構は dcre を忠実に移植している：cfg で予約したスロットを `free_*cb`
（キュー，FIFO）で管理し，実行時に確保・返却する。制御ブロック（CB）はヒープ
確保せず，cfg が生成する named-static 変数＋const ポインタ表の末尾に予約領域を
確保する。API 面も dcre 標準そのまま（サービスコール名・パケット型・静的API名）
であり，**独自の拡張 API は無い**。唯一の例外は ISR の `ENA_DYNISR` で，これは
「動的 ISR の実行コアを intno のクラス affinity で表現する」ための FMP3 拡張
（§2 参照）。

実装は5段階に分けて進めた：段階1（タスク＋`DEF_MPK`）→段階2（周期通知／
アラーム通知）→段階3a（セマフォ／イベントフラグ／ミューテックス）→段階3b
（データキュー／優先度データキュー／固定長メモリプール）→ ISR（専用計画）。
**ISR 段階を含め全段階が Task 7 まで完了しており，全回帰がグリーンで
フラット台帳 `.superpowers/sdd/progress.md` にも完了記録済みである
（`.superpowers/sdd/2026-08-04-fmp3-dcre-isr/progress.md` に作業記録の
詳細がある）。dcre 標準の動的生成オブジェクトは ISR を含めて全種類が
揃っている。**本書はブランチ内でのみ管理するため，以後の更新でも
特定コミットの SHA は追わない方針とする。

設計書：
- 段階1：`docs/superpowers/specs/2026-08-03-fmp3-dynamic-creation-design.md`
- 段階2：`docs/superpowers/specs/2026-08-04-fmp3-dcre-stage2-cyc-alm-design.md`
- 段階3a：`docs/superpowers/specs/2026-08-04-fmp3-dcre-stage3a-sem-flg-mtx-design.md`
- 段階3b：`docs/superpowers/specs/2026-08-04-fmp3-dcre-stage3b-dtq-pdq-mpf-design.md`
- ISR：`docs/superpowers/specs/2026-08-04-fmp3-dcre-isr-design.md`

---

## 2. 静的 API（コンフィギュレーション）

### 2.1 `AID_*` — 動的生成スロットの予約

```
AID_TSK(uint_t notsk);
AID_CYC(uint_t nocyc);   AID_ALM(uint_t noalm);
AID_SEM(uint_t nosem);   AID_FLG(uint_t noflg);   AID_MTX(uint_t nomtx);
AID_DTQ(uint_t nodtq);   AID_PDQ(uint_t nopdq);   AID_MPF(uint_t nompf);
AID_ISR(uint_t noisr);
```

各 `AID_*(n)` は，該当オブジェクト種について動的生成用の ID スロットを `n` 個
予約する。予約された分だけ CB（制御ブロック）が named-static として確保され，
ID 空間の「静的レンジの直後」に割り当てられる（§4）。`n` の値そのものは検査
しない（両エンジンで同一挙動・C コンパイルで破綻するため）。

**`AID_*` はクラス（`CLASS(){}`）の囲みの外に書く専用の API である。** クラス内
に書くと `E_RSATR` になる。根拠は，動的スロットの `iprcid`/`affinity` が固定値
（§5）でありクラス情報を読む場所が無いこと，スタックや管理領域は cfg でなく
実行時に確保すること。`ENA_SPR` が「クラス内なら E_RSATR」であるのと対称な
制約である。

**ガード（訂正E）**：`AID_*(n>0)` を書くには，同じオブジェクト種の静的生成
（`CRE_*`）が cfg 内のどこかに 1 個以上必要である（無ければ cfg エラー
`E_OBJ`）。予約したスロットだけがあって静的な初期化経路が無い構成を防ぐための
検査であり，全オブジェクト種に共通の汎用枠組み（`KernelObject`）が担う。

### 2.2 `DEF_MPK` — カーネルメモリプール

```
DEF_MPK({ size_t mpksz, MB_T *mpk });
```

タスクの自動スタック確保（`stk == NULL`）・データキュー等の管理領域自動確保
（後述 `TA_MBALLOC`）・固定長メモリプールのブロック領域自動確保
（`TA_MEMALLOC`）が使う唯一のメモリ供給源である。`mpk == NULL` なら cfg が
`mpksz` バイトの領域を自動確保し，非 NULL ならユーザ供給領域を使う。
**最大1個**（2個目を書くと `E_OBJ`）。クラス外専用（`AID_*` と同じ規約）。

### 2.3 `ENA_DYNISR` — 動的 ISR の opt-in（FMP3 拡張）

```
ENA_DYNISR(INTNO intno);
```

ISR だけは他オブジェクトと異なる二段構えになっている。`AID_ISR(n)` は
「動的 ISR 用の CB を n 個，グローバルに予約する」という点は他オブジェクトと
同じだが，**動的 ISR がどの割込み番号（intno）で発生しうるかは，別途
`ENA_DYNISR(intno)` で明示的に opt-in した intno に限る**。

- `ENA_DYNISR(intno)` は **`CFG_INT(intno)` と同一クラスの囲みの中**に書く
  （`CRE_ISR`／`CFG_INT` と同じ「クラス内API」規約。クラスが無ければ
  `E_RSATR`）。
- 対応する `CFG_INT(intno)` が無い intno への `ENA_DYNISR` はエラー。
- `DEF_INH` が競合している intno への `ENA_DYNISR` はエラー（`CRE_ISR` と
  `DEF_INH` が同一 intno で共存できないのと同じ排他則）。
- ENA_DYNISR された intno は，静的 `CRE_ISR` の有無にかかわらず，ディスパッチが
  現行の平坦なインライン連鎖から**ランタイムキュー方式**に切り替わる（§5）。
  opt-in していない intno は現行のインライン連鎖のまま**一切変わらない**
  （生成コードもバイト単位で不変。§7 参照）。
- **動的 ISR の実行コアを指定する引数は API に存在しない**。「どの intno を
  選ぶか」＝「どのクラス（＝どのプロセッサ affinity）の割込みで動かすか」で
  コアを表現する，というのが本方式の設計である（案B-2。dcre の `AID_ISR`
  意味論をグローバルプールのまま保つため）。

**ガード（訂正E，2件）**：
1. `AID_ISR(n>0)` は `ENA_DYNISR` が cfg 内に 1 個以上必要（`E_OBJ`）。
   適格な intno が無いと `acre_isr` が必ず失敗し，予約 CB が死蔵されるため。
2. `ENA_DYNISR(intno)` は静的 `CRE_ISR` が cfg 内のどこかに 1 個以上必要
   （`E_OBJ`）。静的 ISR が 0 個だと `initialize_isr` の登録自体が起きず，
   `isr_queue_table` が未初期化のまま `call_isr` が走ってしまうため。
   `syssvc` 経由の `CRE_ISR(ISR_SIO)` があるため通常の構成では自動的に満たされる。

### 2.4 制約まとめ

| 検査 | 条件 | 結果 |
|---|---|---|
| `AID_*`/`DEF_MPK`/`AID_ISR` がクラス内 | 常に | `E_RSATR` |
| `ENA_DYNISR` がクラス外 | 常に | `E_RSATR` |
| `AID_*(n>0)` かつ同種の静的オブジェクトが 0 個 | tsk 以外全種 | cfg エラー `E_OBJ` |
| `DEF_MPK` を 2 個以上 | 常に | `E_OBJ` |
| `AID_ISR(n>0)` かつ `ENA_DYNISR` が 0 個 | 常に | `E_OBJ` |
| `ENA_DYNISR(intno)` かつ静的 `CRE_ISR` が 0 個（cfg 全体で） | 常に | `E_OBJ` |
| `ENA_DYNISR(intno)` に対応する `CFG_INT(intno)` が無い | 常に | cfg エラー `E_OBJ`（`kernel/interrupt.py:397-399`） |
| `ENA_DYNISR(intno)` に `DEF_INH(intno)` が競合 | 常に | cfg エラー `E_OBJ`（`kernel/interrupt.py:414-422`） |

参照：`kernel/kernel_api.def`（文法の実物），
`docs/superpowers/specs/2026-08-03-fmp3-dynamic-creation-design.md` §3.2，
`docs/superpowers/specs/2026-08-04-fmp3-dcre-isr-design.md` §1.1・訂正E。

---

## 3. サービスコール一覧

以下の全サービスコールは `kernel_impl.h`/`check.h` の検査マクロで守られており，
`E_ID`（不正な ID の型・番号）・`E_PAR`（パラメータ不正）・`E_RSATR`（未定義
属性ビット）・`E_CTX`（呼出しコンテキスト不正，非タスクコンテキストからの
呼出し）は個別には記載しない（通常のサービスコールと同じ検査規約）。

### 3.1 タスク

```c
ER_ID acre_tsk(const T_CTSK *pk_ctsk);
ER    del_tsk(ID tskid);

typedef struct t_ctsk {
    ATR     tskatr;     /* タスク属性（TA_ACT/TA_NOACTQUE/TA_MEMALLOC のみ有効） */
    EXINF   exinf;
    TASK    task;
    PRI     itskpri;
    size_t  stksz;
    STK_T   *stk;        /* NULL なら DEF_MPK から自動確保（TA_MEMALLOC が立つ） */
} T_CTSK;
```

- `acre_tsk`：free-list 空なら `E_NOID`。`stk == NULL` で自動確保に失敗すれば
  `E_NOMEM`（**`E_NOID` の判定が先**——スロットも尽きていれば `E_NOID` になる）。
  返値は動的レンジの ID。生成直後は休止状態，`TA_ACT` なら即起動。
- `del_tsk`：**削除できるのは休止状態のタスクのみ**。休止でなければ `E_OBJ`。
  静的生成タスク（`tskid <= tmax_stskid`）への適用も `E_OBJ`。`TA_MEMALLOC`
  付きならスタックを解放する。

### 3.2 周期通知・アラーム通知

```c
ER_ID acre_cyc(const T_CCYC *pk_ccyc);  ER del_cyc(ID cycid);
ER_ID acre_alm(const T_CALM *pk_calm);  ER del_alm(ID almid);

typedef struct t_ccyc {
    ATR         cycatr;
    T_NFYINFO   nfyinfo;   /* 通知方法（後述） */
    RELTIM      cyctim;
    RELTIM      cycphs;
} T_CCYC;

typedef struct t_calm {
    ATR         almatr;
    T_NFYINFO   nfyinfo;
} T_CALM;
```

- `acre_*`：`E_NOID` のみ（メモリプールを使わないため `E_NOMEM` 経路は無い）。
- `del_*`：**削除に前提条件が無い**。動作中（`sta_cyc`/`sta_alm` 済み）でも
  削除できる（dcre 意味論。停止処理を内部で行ってから解放する）。待ちタスクは
  存在しないオブジェクトなので `E_DLT` の概念は無い。静的生成への適用は `E_OBJ`。

`T_NFYINFO` は dcre 標準の通知方式共用体で，`TNFY_HANDLER`（ハンドラ直接呼出し）
に加えて非ハンドラ通知（変数設定・フラグセット・データキュー送信等）をサポート
する。非ハンドラ通知は，本移植で新設したトランポリン関数 `notify_handler` が
中継する（§5.2 参照，`docs/superpowers/specs/2026-08-04-fmp3-dcre-stage2-cyc-alm-design.md`
§4）。

### 3.3 セマフォ・イベントフラグ・ミューテックス

```c
ER_ID acre_sem(const T_CSEM *pk_csem);  ER del_sem(ID semid);
ER_ID acre_flg(const T_CFLG *pk_cflg);  ER del_flg(ID flgid);
ER_ID acre_mtx(const T_CMTX *pk_cmtx);  ER del_mtx(ID mtxid);

typedef struct t_csem { ATR sematr; uint_t isemcnt; uint_t maxsem; } T_CSEM;
typedef struct t_cflg { ATR flgatr; FLGPTN iflgptn; } T_CFLG;
typedef struct t_cmtx { ATR mtxatr; PRI ceilpri; } T_CMTX;
```

- `acre_*`：`E_NOID` のみ（メモリプール不使用）。`acre_sem` は
  `isemcnt <= maxsem` かつ `1 <= maxsem` を検査。`acre_mtx` は `TA_CEILING`
  のときのみ `ceilpri` の範囲を検査する（`TA_TPRI` なら `ceilpri` は無視）。
- `del_*`：**状態を問わず削除できる**。待ちタスクがいれば `init_wait_queue`
  （FMP3 既存の MP 対応機構）で全員 `E_DLT` により強制的に起こしてから解放する。
  `del_mtx` は**ロック中でも成功**する。ロック保持タスクの現在優先度は，
  ロック取得前のベース優先度へ復帰する（ceiling ミューテックスの場合）。
  静的生成への適用は `E_OBJ`。

### 3.4 データキュー・優先度データキュー・固定長メモリプール

```c
ER_ID acre_dtq(const T_CDTQ *pk_cdtq);  ER del_dtq(ID dtqid);
ER_ID acre_pdq(const T_CPDQ *pk_cpdq);  ER del_pdq(ID pdqid);
ER_ID acre_mpf(const T_CMPF *pk_cmpf);  ER del_mpf(ID mpfid);

typedef struct t_cdtq {
    ATR dtqatr; uint_t dtqcnt; void *dtqmb;   /* NULL なら DEF_MPK から自動確保 */
} T_CDTQ;
typedef struct t_cpdq {
    ATR pdqatr; uint_t pdqcnt; PRI maxdpri; void *pdqmb;
} T_CPDQ;
typedef struct t_cmpf {
    ATR mpfatr; uint_t blkcnt; uint_t blksz; MPF_T *mpf; void *mpfmb;
} T_CMPF;
```

この3種は，dcre 忠実に「管理領域」（`DTQMB`/`PDQMB`/`MPFMB` の配列）を
`DEF_MPK` プールから確保する。`mpf` はさらに「ブロック領域」自体も自動確保
できる（2段確保。§6）。

- `acre_dtq`/`acre_pdq`：`dtqmb`/`pdqmb` が `NULL` なら管理領域を
  `malloc_mpk` で確保し `TA_MBALLOC` を立てる。確保に失敗すれば `E_NOMEM`
  （**`E_NOID` 判定の後**——スロットに空きが無ければ `E_NOID` が先に返る）。
  非 NULL なら受理する（`TA_MBALLOC` は立てない。cfg の静的生成が同じ引数を
  `E_NOSPT` で弾くのとは非対称だが，dcre 忠実な仕様である）。
- `acre_mpf`：①`mpf == NULL` ならブロック領域を確保（`TA_MEMALLOC`），
  ②管理領域を確保（`TA_MBALLOC`）の2段階。②が失敗したら①で確保した分を
  巻き戻してから `E_NOMEM`。
- `del_*`：**状態を問わず削除できる**。待ちタスク（送信待ち／受信待ち，
  `dtq`/`pdq` は両方，`mpf` は取得待ちのみ）を `E_DLT` で解放してから，
  確保していた領域（`TA_MBALLOC`/`TA_MEMALLOC`）を `free_mpk` で返却する。
  `dtq`/`pdq` に滞留していたデータは破棄される（dcre 意味論）。
  **`del_mpf` は，貸出中（未返却）のブロックがあってもプールごと解放する**
  （§7 の「受容済みウィンドウ」参照）。静的生成への適用は `E_OBJ`。

### 3.5 割込みサービスルーチン（ISR）

```c
ER_ID acre_isr(const T_CISR *pk_cisr);
ER    del_isr(ID isrid);

typedef struct t_cisr {
    ATR   isratr;
    EXINF exinf;
    INTNO intno;
    ISR   isr;
    PRI   isrpri;
} T_CISR;
```

- `acre_isr`：`isratr`/`isr`（アライン・非NULL）/`isrpri` の検査に加え，
  `intno` の**適格性検査**を行う。適格性は「グローバル適格 intno 表」の
  二分探索（`search_isr_queue`）のみで判定し，以下の4ケースは**すべて
  `E_OBJ`** にまとめられる：範囲外の intno／`CFG_INT` の無い intno／
  `CFG_INT` はあるが `ENA_DYNISR` されていない intno／`DEF_INH` が競合して
  いる intno。（dcre は「範囲外」と「表に無い」を区別するが，FMP3 では
  区別する手段が構造的に無い。§9 参照）。free-list 空なら `E_NOID`。
- `del_isr`：**削除に前提条件は無い**（動作中の intno に紐づく ISR でも
  削除できる）。**`del_isr` が E_OK を返した時点で，対象 ISR は実行中でなく，
  以後実行されない**ことを保証する（quiesce 方式）。他コアで当該 ISR 本体が
  実行中なら，`del_isr` はその完了まで待ってから戻る。待ち時間は ISR 本体の
  実行時間で有界（TOPPERS の「ISR は短時間で終える」規約に従う限り）。
  静的生成 ISR への適用は `E_OBJ`。**`E_NOEXS`（他タスクが削除中／削除済み）
  で戻った場合にはこの保証は無い**——quiesce 完了を必要とする資源解放は，
  `E_OK` を確認してから行うこと（`kernel/interrupt.c:665` 付近のコード側
  コメントが正）。
- `acre_isr`/`del_isr`とも **ISR コンテキストから呼び出すと `E_CTX`** になる
  （通常のサービスコールと同じ制約。`test_dcre5` の手順7で実証）。
- **同一 intno の割込み処理が実行中（キュー走査中）に `acre_isr` した場合**：
  新しい ISR の `isrpri` が走査中の ISR より低優先（数値が大きい）か同一なら，
  同一の割込み起動内で拾われて実行される。走査中の ISR より高優先（`isrpri`
  が小さい）な ISR を acre した場合は，走査が既にその位置を通過しているため
  本起動では実行されず，**次回の割込み発生から有効**になる（二重実行や
  取りこぼしにはならない）。

---

## 4. ID 空間

全オブジェクト種で共通の「2レンジ方式」を採用する。cfg は各オブジェクト種
について次の3値を生成する：

- `TNUM_S<OBJ>ID`（静的生成の個数）
- `TNUM_<OBJ>ID`（静的＋動的の総数。`AID_<OBJ>(n)` があれば `+n`）
- `tmax_s<obj>id` / `tmax_<obj>id`（それぞれの最大 ID 値）

ID は次のように2つの連続範囲に分かれる：

```
TMIN_<OBJ>ID .. tmax_s<obj>id     ← 静的生成（.cfg の CRE_* が確保）
tmax_s<obj>id+1 .. tmax_<obj>id   ← 動的生成（AID_<OBJ>(n) が予約）
```

`AID_<OBJ>` を書かなければ `TNUM_<OBJ>ID == TNUM_S<OBJ>ID` となり，動的
レンジは幅ゼロ（既存構成の挙動は完全に不変）。

CB（制御ブロック）は FMP3 では「named-static ＋ const ポインタ表」で管理
されており（ASP3 のような連続配列ではない），ID は「CB のポインタ表内の位置」
からではなく，**INIB（初期化ブロック）へのポインタ差分**から求める。動的
生成タスクの `p_tinib` は静的 `tinib_table[]` ではなく RAM の `atinib_table[]`
を指すため，全オブジェクト種で次の型のマクロを使う（`TSKID` の実例）：

```c
#define TSKID(p_tcb) \
    ((((p_tcb)->p_tinib >= atinib_table) \
        && ((p_tcb)->p_tinib < &atinib_table[tnum_tsk - tnum_stsk])) \
      ? ((ID)(((p_tcb)->p_tinib - atinib_table) + TMIN_TSKID + tnum_stsk)) \
      : ((ID)(((p_tcb)->p_tinib - tinib_table) + TMIN_TSKID)))
```

同型のマクロが `CYCID`/`ALMID`/`SEMID`/`FLGID`/`MTXID`/`DTQID`/`PDQID`/
`MPFID`/`ISRID` にもある（`kernel/task.h`・`kernel/cyclic.h`・
`kernel/alarm.h`・`kernel/semaphore.h`・`kernel/eventflag.h`・
`kernel/mutex.h`・`kernel/dataqueue.h`・`kernel/pridataq.h`・
`kernel/mempfix.h`・`kernel/interrupt.h`）。

**スロット枯渇**：`free_*cb`（QUEUE で実装した free-list）が空なら，`acre_*`
は例外なく `E_NOID` を返す。

**再利用の順序は FIFO**：`del_*` は `queue_insert_prev`（末尾へ挿入），
`acre_*` は `queue_delete_next`（先頭から取り出し）で統一している。dcre の
機構をそのまま移植した結果であり，直感的な「LIFO（後入れ先出し）」ではない
点に注意（詳細は `docs/superpowers/specs/2026-08-03-fmp3-dynamic-creation-design.md`
§6.2 および `test/test_dcre1.c` のコメント）。空きスロットが1個だけの状態
での「削除→再生成」は，FIFO/LIFO のどちらであっても同一 ID が決定的に返る
ため，多くのテストはこの形で検証している。

---

## 5. プロセッサ割付け

動的生成オブジェクトの MP 上の扱いは，オブジェクト種によって3通りに分かれる。

### 5.1 タスク（コア親和あり，固定初期値）

生成直後は常に **`iprcid = 1`（PRC1）**・**`affinity = 全プロセッサ`**
（`(1 << TNUM_PRCID) - 1`）に固定される。`acre_tsk` にコアを指定する引数は
無い。`TA_ACT` なら PRC1 上で即起動する。他コアへ移したい場合は，生成後に
`mact_tsk(id, prcid)`（休止中の初期割付け変更）や `mig_tsk`（動作中の
マイグレーション）を通常のタスクと同じ要領で呼ぶ。affinity が全プロセッサ
なので，どのコアへの移動も許可される。

### 5.2 周期通知・アラーム通知（コア親和あり，target 定義値）

生成直後は **`iprcid = 1`（PRC1）**・**`affinity = TOPPERS_TEPP_PRC`**
（ターゲットごとに定義される「TEP（Timer Event Processor）対応コア」の
ビットマップ）に固定される。`(1 << TNUM_PRCID) - 1`（全コア）ではない点が
タスクと異なる——時間イベント処理ができないコアへ動かせても発火しないため，
target が定義する妥当な集合に限定している。全8プリセットで PRC1 は
`TOPPERS_TEPP_PRC` の bit 0 が立つ（＝PRC1 は必ず TEP 対応）ことを実測で
確認済み（`docs/superpowers/specs/2026-08-04-fmp3-dcre-stage2-cyc-alm-design.md`
§10 の検証結果表）。他コアへ移すには `msta_cyc(id, prcid)`/
`msta_alm(id, tmo, prcid)`（dcre に無い FMP3 固有の起動コール。既存の
`sta_cyc`/`sta_alm` を拡張した形）を使う。

### 5.3 セマフォ・イベントフラグ・ミューテックス・データキュー・
    優先度データキュー・固定長メモリプール（非親和）

**これら6種の INIB には `iprcid`/`affinity`/`p_pcb` に相当するフィールドが
そもそも存在しない。** タスクのような「初期割付けプロセッサ」の概念がない
同期・通信オブジェクトであり，動的生成でも静的生成でも扱いは同じである。
充填コードは1行も無い（書いたらバグ）。

### 5.4 ISR（コア親和あり，intno のクラスに従う）

ISR だけが「コア指定は intno 経由」という独自の扱いになる。`acre_isr` の
`T_CISR` にコアを指定するフィールドは無い——**動的 ISR がどのコアで実行
されるかは，`intno`（＝どのクラスの `CFG_INT`/`ENA_DYNISR` を選ぶか）で
一意に決まる**（§2.3）。affinity が複数コアに跨るクラスの intno を選べば，
その全コアで（独立に）実行されうる。

**同一 intno に対する同一 ISR が複数コアで同時実行されることは許容される**
（静的インライン連鎖でも従来から起きていた FMP3 既存の意味論をそのまま
継承しているだけで，本機能が新たに導入したものではない）。走査（キュー
方式のディスパッチ）自体は各コアが独立に `(isrpri, isrseq)` を進めるため，
互いに干渉しない（§7 の quiesce の項参照）。

---

## 6. メモリプール

`DEF_MPK` が定義するカーネルメモリプールは，**バンプ確保＋完全一致サイズ
再利用（freelist）**のアロケータである（TLSF 等の汎用アロケータではない。
dcre 原形の「全解放まで再利用しない」単純 bump を，本プロジェクトで機能拡張
した——`DIVERGENCE_MAP.md` の「mempool freelist 化」行参照）。`malloc_mpk`/
`free_mpk`（`kernel/kernel_impl.h`）経由で使う。

- 確保は，まず解放済みブロックの単方向リスト（freelist）を先頭から走査し，
  **要求サイズが解放時の要求サイズと完全に一致し，かつ番地が要求アライン
  メントに適合する**最初のブロックがあればそれを再利用する。無ければ従来
  どおり「現在のブレークポイントからヘッダ＋必要バイト数を切り出して前進
  させる」バンプ確保になる。**ブロックの分割・結合は行わない**。
- **「途中の1個だけ返す」操作でも，同一サイズの次の要求はそのブロックを
  再利用する**。したがって `del_*` → `acre_*` を同一サイズで繰り返す典型的
  な使い方では，他の長寿命割付が生存していてもアリーナは痩せない
  （`test_dcre6` が実演している。旧実装ではこの手順は必然的に `E_NOMEM` に
  到達した）。サイズの異なる要求には再利用が効かず，ブレークポイントが
  前進する点は従来どおり。
- **返却された確保数（`count`）がゼロになったときは，ブレークポイントを
  プール先頭へ戻し freelist も空にする**（従来の全域リセットを backstop
  として維持）。
- 各割付の直前に `size_t` 1個分のヘッダが付くため，割付1件あたり
  4B（32bit）／8B（64bit）のオーバーヘッドがある。プール制御ブロックも
  12B→16B（32bit）に増えた。`mpksz` の見積りにはこの分を積むこと。
- 確保の計算量は O(freelist 長)（上界はプールに収まる最大割付数で決定的に
  有界），解放は O(1)。ギャント・ロック下でしか呼ばれないため，アロケータ
  自体は排他制御を持たない。

**確保元と立つ属性ビット**：

| ビット | 意味 | 立つ状況 |
|---|---|---|
| `TA_MEMALLOC` (0x8000) | 主要領域（タスクスタック／mpf ブロック領域）を自動確保 | `stk == NULL`（tsk）／`mpf == NULL`（mpf） |
| `TA_MBALLOC` (0x4000) | 管理領域（`DTQMB`/`PDQMB`/`MPFMB` 配列）を自動確保 | `dtqmb`/`pdqmb`/`mpfmb == NULL`（dtq/pdq/mpf） |

サイズ見積りは `include/kernel.h` の `COUNT_MB_T(sz)`/`ROUND_MB_T(sz)`
マクロで `MB_T`（プールの管理単位）粒度に切り上げる。`DEF_MPK` の `mpksz`
を決める際は，動的生成する全オブジェクトが要求する最大同時確保量（スタック
＋管理領域＋ブロック領域の総和）を見積もる必要がある。テストでは
`MPK_SIZE = STACK_SIZE + 1024`（段階1）のように，用途ごとに余裕を積んで
決め打ちしている（`test/test_dcre1.h` 等）。

**注意（hardening 済み）**：`acre_dtq`/`acre_pdq`/`acre_mpf`/`acre_tsk` の
管理領域・ブロック領域・スタックのサイズ計算（乗算・丸め）のオーバーフロー
は，hardening パスで追加された `CHECK_PAR`（計6検査）が `E_PAR` で塞いでいる
（確定内容は `docs/qa-esp32s3-20260804.md` 末尾の追記を参照）。§7.5 も参照。

---

## 7. 注意事項・既知の制約

包み隠さず記録する。

### 7.1 受容済みの残余ウィンドウ（2件）

いずれも「giant lock の外でアプリケーションが直接触れる資源」と「アロケータ
の再利用（`count==0` の全域リセット，または完全一致サイズ再利用——後者は
メモリプール freelist 化で追加された経路）」が絡む，理論上の競合ウィンドウ
である。実害が到達可能であることをテストで実証したものではないが，発生条件を
明示した上で受容する判断をしている。

**(1) 自終了タスクのスタック残余ウィンドウ**（段階1で発見・受容）：
`ext_tsk()` はギャントロック下で `TSTAT_DORMANT` に遷移させた後，
`release_glock(); exit_and_dispatch();` の間，終了中のコアが自タスクの
スタック上で数命令だけ実行を続ける。この数命令の間に，別コアが
`TSTAT_DORMANT` を観測して `del_tsk` → `free_mpk` でき，
プールの全確保数が0になる（全域リセット）か，**同一 `stksz` の割付要求が来る
（完全一致サイズ再利用）**かのいずれかで，次の `acre_tsk` が同一メモリを
新タスクへ渡し得る。窓は CPU ロック下の数命令であり，pristine FMP3 の
自終了経路そのものに由来する（`mact_tsk` による静的タスク再活性化にも
同型の窓がある）。**回避策＝アプリの資源規律**（終了処理と削除処理を
タイミング的に密結合させない一般的な設計上の注意）。

**(2) 固定長メモリプールのブロック領域ウィンドウ**（段階3bで発見・受容）：
`get_mpf`/`pget_mpf`/`tget_mpf` が返す生ポインタは，**カーネルロックの外で
アプリケーションが直接読み書きする**（μITRON/dcre の意味論そのもの）。
`del_mpf` は貸出中のブロックがあってもプールごと解放するため（§3.4），
次の具体的な競合列が成立しうる：タスクAが `pget_mpf` でブロックを保持
→ タスクBが `del_mpf`（プールの確保数が0になり全域リセットされるか，解放された
領域が freelist に載る）→ タスクBが別の `acre_*`（全域リセット後の再確保，
または**同一サイズ要求の完全一致再利用**により，新しい管理領域が同一バイト列
に確保される）
→ タスクAが（del_mpf 完了を知らないまま）保持していたブロックへ書込み
→ 他オブジェクトの管理領域を破壊する。E_NOEXS ゲートは API 呼出しは守るが，
**既に得た生ポインタへの stale 参照までは守らない**。dcre 忠実・
μITRON 系のアプリケーション契約上の設計であり，**回避策＝アプリの資源
規律**（`del_mpf` される可能性のあるプールから得たブロックは，返却前に
プールが削除されないことをアプリ側で保証する）。

いずれも `docs/superpowers/specs/2026-08-03-fmp3-dynamic-creation-design.md`
§2.3 および
`docs/superpowers/specs/2026-08-04-fmp3-dcre-stage3b-dtq-pdq-mpf-design.md`
§1 に詳細な論証がある。

**【2026-08-04 追記・裁定】** メモリプールの完全一致フリーリスト化
（`docs/superpowers/specs/2026-08-04-fmp3-mempool-freelist-design.md` §8）に
より，上記2件の窓は「`count==0` の全域リセット」を待たず「同一サイズの再要求」
という局所条件だけで到達しうるようになった（**機序自体は不変・到達確率が
上がる方向の変化**。§1.2 の動機——同一スタックサイズのタスクの反復
create/delete——はまさにこの新経路が最も起こりやすい形である）。
ユーザはこれを受容した。根拠：(a) 解放済みメモリの再利用を行うアロケータ
（TLSF/o1heap 含む）は本質的に同じ性質を持つ，(b) 窓2件は段階1/3b で
「アプリ契約」（休止確認後の del・未返却ブロック持ち del は契約違反）として
裁定済みであり，契約を守る限り窓は開かない，(c) 本節に成立経路として明記した
（この追記）。

### 7.2 未生成スロット ID への `mact_tsk`/`mig_tsk`

`mact_tsk`/`mig_tsk` は，ロック取得**前**に `p_tinib`（INIB）を読んで
`VALID_MIG` 等の検査を行う。動的レンジの ID のうち，一度も `acre_tsk`
されていない（＝`atinib_table` の初期値のままの）スロットに対してこれらを
呼ぶと，`INDEX_PRC(0)` の左シフトが未定義動作になったり，本来 `E_NOEXS`
であるべきところが `E_PAR` になったりする経路が存在する（段階1最終
レビューで発見・deferred 扱いの既知課題）。**正しい ID 管理はアプリケーション
の責務**である——`acre_tsk` の返値以外の ID をこれらのサービスコールに
渡さないこと。

### 7.3 ISR の同一 ISR 複数コア並行実行

§5.4 のとおり，同一 ISR が複数コアで同時に実行されることは許容される
（既存の FMP3 意味論の踏襲であり，本機能による新規の挙動ではない）。

### 7.4 `del_isr` の quiesce 保証と待ち時間

`del_isr` は，他コアで当該 ISR 本体が実行中である間，ギャントロックと
CPU ロックを解放して待つ（`delay_for_interrupt()` を挟む `wait_tmout` 系と
同型のイディオム）。**待ち時間は ISR 本体の実行時間で有界**——具体的な
数値上限は測定していないが，TOPPERS の「ISR は短時間で終える」という
一般規約が成立している限り，`del_isr` の呼出し元が長時間ブロックすることは
無い。

### 7.5 サイズ乗算オーバーフロー検査（hardening 済み）

§6 末尾のとおり，`acre_dtq`/`acre_pdq`/`acre_mpf`/`acre_tsk` のサイズ計算の
乗算・丸めオーバーフローは，hardening パスで追加された `CHECK_PAR`（4 API・
6検査）が `E_PAR` で塞いだ（確定内容・64bit での各検査の生死は
`docs/qa-esp32s3-20260804.md` 末尾の追記を参照）。`kernel/startup.c` の
`malloc_mempool` が持っていた符号混在比較（§9 上流報告候補 c）も同パスで
一体修正済みである（本ブランチでは修正済み・上流へは未報告）。
なお本節の旧記述（「未検査・hardening 予定」）は hardening パス完了後も
本文書だけ更新されずに残っていたもので，メモリプール freelist 化の文書更新
（2026-08-04）で是正した。

### 7.6 AID 系の恒常出力による ROM 増

`AID_ISR` は他の `AID_*` と異なり，**opt-in（`ENA_DYNISR`）が1つも無い
構成でも ROM を増やす**。理由は，`kernel_api.def` に `AID_ISR` を登録した
時点で，cfg フレームワークが「静的 `CRE_ISR` を1個以上持つ全構成」に対して
`ISRINIB`/`ISRCB`/ポインタ表／`isrorder_table`／`initialize_isr` 呼出し等を
恒常的に生成するようになるためである（ISR には従来ランタイムオブジェクトが
無かったので，段階1〜3aの「AID 無しなら完全に無出力」という前例が成立
しない）。`syssvc` の `CRE_ISR(ISR_SIO)` があるため，事実上すべての構成が
該当する。

ディスパッチ経路・実行時挙動そのものは不変（`_kernel_inthdr_<intno>` は
バイト単位で不変）であり，増えるのはデータと起動時 O(N) ループのみである
ため受容している。**最終実測値**（`musca_b1-2core`，ISR段階 Task 7，
`AID_ISR`/`ENA_DYNISR` の実装導入前 `f1f1d53` 後 HEAD 比較）：

```
text +1796／ data +0／ bss +72
```

（`DIVERGENCE_MAP.md` の「★ROM増分の最終確認（ISR段階 Task 7）」参照。
bss は Task 3 完了時点（`ISRCB`/`free_isrcb` 等のデータ構造確定）から
不変であり，Task 4（`call_isr` の MP対応版への全面書き直し）・Task 5
（`acre_isr`/`del_isr` の追加）で増えた分はすべて text 側のコード本体で
ある）。

参考として，Task 3 完了時点（`call_isr` 暫定版のみ）の中間スナップショットは
`text +492／ data +0／ bss +72` であった（型と初期化コード導入直後の値で，
上記の最終値には含まれていない Task 4/5 分のコード増（+1304 text バイト）が
後続する。`DIVERGENCE_MAP.md` の「★ROM増分の「後」実測（Task 3 Step 9）」
参照）。

他の `AID_*`（tsk/cyc/alm/sem/flg/mtx/dtq/pdq/mpf）は，`AID_*` を1個も
書かない構成では出力が完全に不変である（各段階の「管理された差分」検査で
確認済み）。

---

## 8. テスト

いずれも `musca_b1-2core`（Cortex-M33，2コア）で QEMU 実行し，
`TTSP_RESULT: PASS` を確認している。

| テスト | 対象範囲 |
|---|---|
| `test/test_dcre1.c` | `acre_tsk`/`del_tsk`：生成→起動→自然終了，free-list の FIFO 再利用，スロット枯渇 `E_NOID`，`del_tsk` のエラー系（静的/非休止で `E_OBJ`），強制終了後の削除，削除済みIDへの `E_NOEXS`，自動スタック確保の `E_NOMEM`/成功，`mact_tsk` による全コア affinity の実証 |
| `test/test_dcre2.c` | `acre_cyc`/`del_cyc`・`acre_alm`/`del_alm`：ハンドラ通知の発火，動作中削除の成功，削除済みIDの `E_NOEXS`，`E_PAR`/`E_NOID`，`msta_cyc` による PRC2 への移動と発火，非ハンドラ通知（`TNFY_SETVAR`）の実証，alm の再起動・状態遷移，静的オブジェクトへの `E_OBJ` |
| `test/test_dcre3.c` | `acre_sem`/`del_sem`・`acre_flg`/`del_flg`・`acre_mtx`/`del_mtx`：基本動作，同一/別プロセッサでの `E_DLT` 実証（`init_wait_queue` の MP 経路），スロット枯渇・静的オブジェクトの `E_OBJ`，ロック中 `del_mtx` と優先度復帰の実測，カーネル変異 negative control |
| `test/test_dcre4.c` | `acre_dtq`/`del_dtq`・`acre_pdq`/`del_pdq`・`acre_mpf`/`del_mpf`：実通信，`E_NOMEM`と`E_NOID`の順序実証，両方向の送受信待ち `E_DLT`，滞留データ破棄，優先度順配送，ユーザ供給管理領域，mpf ブロックの読み書きと反復確保によるプール解放の実証 |
| `test/test_dcre5.c` | `acre_isr`/`del_isr`：静的のみディスパッチの不変性，静的/動的混在の isrpri 順呼出し，同一 isrpri 内の isrseq 順呼出し，走査中の他コア del/acre の取りこぼし・二重実行の不成立，PRC2 実行中 ISR への quiesce 実証，各種エラー系，ISR コンテキストからの呼出しの `E_CTX` |
| `test/test_dcre_mix.c` | 機能テストではなく，`AID_TSK`/`AID_CYC`/`AID_SEM`/`AID_MTX`/`AID_DTQ`/`AID_PDQ`/`AID_MPF`/`AID_ISR` を書き `AID_ALM`/`AID_FLG` を書かない「混在構成」で，cfg の Ruby オラクルと Python 製品がバイト一致することを検査する（QEMU では実行しない。ビルドと `cfg_equivalence.sh` のみ） |

非退行確認は各段階の完了時に `test_int2`（既存の静的 ISR 多重連鎖テスト）を
含めて実施しており，opt-in していない intno のディスパッチが不変であることを
バイト単位で確認している（§7.6）。

---

## 9. 上流との関係

### 9.1 dcre からの意図的な逸脱

いずれも `DIVERGENCE_MAP.md` に記録済み。代表的なもの：

- **`CHECK_ID`/`CHECK_PAR` の統一**：dcre 自身が `del_flg`（`E_PAR`）・
  `del_dtq`（`E_PAR`）だけ他の `del_*`（`del_sem`/`del_mtx`/`del_pdq`/
  `del_mpf`/`del_isr` は `CHECK_ID`＝`E_ID`）と不整合な検査マクロを使って
  いる。FMP3 版は全 `del_*` を `CHECK_ID`（`E_ID`）に統一した（上流報告
  候補 d，§9.2）。
- **`del_mtx` の `p_loctsk` クリア**（訂正F）：ロック中ミューテックスを
  削除する際，`p_mtxcb->p_loctsk` をローカル変数へ退避してから `NULL` を
  書き込み，`remove_mutex`/`mutex_drop_priority` の順に処理する。dcre は
  このクリアを省くが，FMP3 既存の `ini_mtx` の手順に倣って追加した。
- **64bit 環境での `callback`/`arg` 再設定**（訂正D）：周期通知／アラーム
  通知の free-list リンクは `tmevtb` 領域を転用する（dcre と同じ技法）が，
  64bit 環境では `QUEUE` の `p_prev`（offset 8）と `TMEVTB.callback`
  （offset 8）が重なり，free-list 化した瞬間に `callback`/`arg` が
  上書きされる。`acre_cyc`/`acre_alm` は確保のたびにこれらを明示的に
  再設定する（dcre はこの重なりが起きない環境が前提だったための逸脱）。
- **`isrseq` の単調カウンタ化**：ISR 呼出しキューの走査安定キー
  `isrseq` は，当初「キューが空になったら 0 にリセットする」設計だったが，
  レビューで「走査中にキューが一時空になり，その隙に生成された新規 ISR が
  同一起動内で拾われなくなる」という仕様違反が判明し，**リセットを撤廃して
  単調増加**に変更した（`u32` の wrap は2^32回の enqueue を要し実用上
  到達不能として受容）。dcre にこの機構自体が無い（単一プロセッサ前提の
  素朴な単方向走査だったため）ので，全体が FMP3 の新規設計である。
  ★単調化が回復する保証は，走査位置 `cur` と同じかそれより後の `isrpri`
  位置に入る新エントリに限る（`ISR_KEY_GT` が `isrpri` 第一キーの辞書式
  比較のため）。走査中に `cur` より高優先（`isrpri` が小さい）な ISR を
  acre した場合，その ISR は本起動では実行されず，次回の割込みから有効に
  なる——「起動開始時点の ISR 集合を処理する」という意味論（静的インライン
  連鎖と同じ）の帰結であり，仕様である（Codex 外部レビュー指摘によりこの
  スコープを明確化，コードの変更はない）。
- **`acre_isr` の intno 範囲検査省略**：dcre の `VALID_INTNO_CREISR(intno)`
  相当の検査を持たない。FMP3 の `VALID_INTNO` は `(prcid, intno)` の2引数
  であり，呼出しコアに依存しうるため，コア非依存に「範囲外」と「未登録」を
  区別する手段が無い。適格性検査をグローバル表の二分探索のみに統一し，
  該当する4ケースをすべて `E_OBJ` にまとめた（§3.5）。
- **`isrorder_table` の生成順序**（訂正I）：dcre は「.cfg 記述順」で
  静的 ISR を挿入するが，FMP3 は「isrid 昇順」で生成する。理由は，
  ENA_DYNISR の有無で ISR の呼出し順序が変わってはならないという不変量
  （§7.6 のゼロ影響原則）を保つため——通常のビルドでは ID は記述順に
  割り当てられるので両者は一致するが，ID を明示するビルドでは差が出る。

### 9.2 上流報告候補（4件）

| 候補 | 内容 | 状態 |
|---|---|---|
| a | `arch/arm_m_gcc/common/core_kernel_impl.h` の `TSKINICTXB` メンバコメントが `stk_top`/`stk_bottom` の意味を実際と逆に記述している | 未報告（コメント修正のみで pristine コードは無改変） |
| b | `arch/arm_m_gcc/common/core_rename.def` に `sense_lock`/`unlock_cpu` のリネームエントリが無く，多重ISR連鎖を含むテスト（`test_int2` 等）が musca_b1 でリンク不能（`main` でも再現確認済み） | 本ブランチでは段階2で修正済み（`DIVERGENCE_MAP.md`）。上流へは未報告 |
| c | `kernel/startup.c` 移植元の `malloc_mempool`/`aligned_alloc_mempool` が持つ符号混在比較（`(ptrdiff_t) >= (size_t)`）。dcre 現行ソースとバイト一致＝上流由来のバグ。misaligned な非NULL `mpk` で境界外「成功」の可能性がある | 本ブランチでは修正済み（fb5e369）・上流へは未報告 |
| d | dcre 自身の `del_flg`/`del_dtq` が，兄弟関数（`del_sem`/`del_mtx`/`del_pdq`/`del_mpf`）と異なり `CHECK_PAR`（`E_PAR`）を使っている（本来は `CHECK_ID`/`E_ID` であるべき）。証拠は行番号つきで完備 | 未報告（FMP3 版は§9.1のとおり統一済み） |

候補 b は本ブランチ内では既に修正済みだが，「pristine 側の既存ギャップ
だった」という事実自体は上流へ未報告のまま残っている。送付するかは
ユーザ判断（`DIVERGENCE_MAP.md` の記載方針）。

---

## 10. 参照

- 設計書一式：`docs/superpowers/specs/2026-08-0{3,4}-fmp3-dcre*.md`
- 乖離台帳：`DIVERGENCE_MAP.md`（`mod (dcre-port)` で検索）
- 実装記録：`.superpowers/sdd/progress.md`（段階1〜3b），
  `.superpowers/sdd/2026-08-04-fmp3-dcre-isr/progress.md`（ISR 段階）
- テスト本体：`test/test_dcre{1,2,3,4,5}.{c,cfg,h}`，`test/test_dcre_mix.{c,cfg,h}`

## 11. 移植者向け: arch 層の要件

動的生成 API を新しい arch/target へ持ち込む際に arch 層が用意すべきものの全リスト
（esp32_s3 移植側からの照会 R-1 を受けて恒久化。詳細は `docs/qa-esp32s3-20260804.md`）。

### 11.1 必須（該当 arch のみ）

- **`USE_TSKINICTXB` を定義する arch**（arm_m・Xtensa 等）は次の2つを
  `core_kernel_impl.h` に用意する（未定義だと `kernel/task_manage.c` の
  acre_tsk/del_tsk がコンパイルエラーになる）:

```c
Inline void init_tskinictxb(TSKINICTXB *p_tskinictxb, size_t stksz, STK_T *stk);
    /* 規約: stk_top = stk（先頭番地）／ stk_bottom = stk + stksz（末尾番地）。
       stksz は呼出し側で ROUND_STK_T 済み。静的生成（cfg の TINIB 初期化子）と
       同一規約であること（arm_m 実装 core_kernel_impl.h を参照） */
Inline void *tskinictxb_memalloc_ptr(TSKINICTXB *p_tskinictxb);
    /* TA_MEMALLOC で確保したスタックの先頭番地（free_mpk へ渡す値）を返す */
```

- `USE_TSKINICTXB` を使わない arch（TINIB が stksz/stk を直接持つ riscv 等）は**不要**。

### 11.2 新規要求なし（確認のみ）

- **sem/flg/mtx/dtq/pdq/mpf**: arch 依存関数の新規要求なし。
- **cyc/alm**: 既存の `TOPPERS_TEPP_PRC`（target_kernel.h）を参照するのみ。
- **ISR（ENA_DYNISR）**: 新規関数なし。既存の `INT_ENTRY`/`INTHDR_ENTRY` マクロ経由。
  **`_kernel_inthdr_N` のシグネチャは queue-mode でも `void(void)` を維持**し、
  `INTINIB`（5要素）/`INHINIB`（4要素）の構造・生成は不変（変更時は事前通知する）。

### 11.3 フォールバック（arch 定義が優先・未定義でも動く）

- `TARGET_TSKATR`・`TARGET_MIN_STKSZ`（`kernel/kernel_impl.h` の `#ifndef` 既定）
- `CHECK_*_ALIGN` / `CHECK_FUNC_NONNULL` / `CHECK_MPK_ALIGN` 等
  （`kernel/check.h` — 未定義なら該当検査は恒真に落ちる。厳密化したい arch は定義する）

### 11.4 pin 更新時に生成物へ現れる恒常出力

AID 系 API の登録により、**動的 API を使わない構成でも** kernel_cfg.c に
`_kernel_` 帰属の追加シンボル（TNUM_S*ID 系・EMPTY_LABEL 群・mpk 系・
CRE_ISR を持つ構成では ISRINIB/ISRCB 系）が恒常出力される（設計裁定 —
各 spec の「管理された差分」参照）。ディスパッチコードと実行時挙動は不変。
実測 ROM 影響（musca_b1 sample1）: text +1796 / data +0 / bss +72 バイト。
