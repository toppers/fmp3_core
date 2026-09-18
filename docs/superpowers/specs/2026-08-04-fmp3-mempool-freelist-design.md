# FMP3 カーネルメモリプール — サイズ別再利用（完全一致フリーリスト）設計

**Goal:** `feature/dynamic-creation` ブランチ上の既定カーネルメモリプール実装
（`kernel/startup.c:445-562`、`#ifndef OMIT_MEMPOOL_DEFAULT` ブロック）を、
「解放済みブロックの完全一致サイズ再利用」を備えた実装へ**置換**する。
現行実装は dcre 忠実の単純 bump アロケータで、`count`（生存割付数）が
0 に戻ったときにしか `brk` をプール先頭へ戻さない。この結果、**長寿命割付が
1つでも残ると create/delete サイクルでアリーナが単調に痩せる**という病理を持つ。
本 spec はこれを除去する。

**参照:**
- 調査報告 `docs/research-embedded-allocators-20260804.md` 推奨3
  （「置換しない — 現行バンプ＋オブジェクト種別フリーリスト」）の一般化・具体化。
  本設計は推奨3を「オブジェクト種別ごと」ではなく「デフォルト実装内部でサイズ完全一致」
  という形で汎用化したものであり、推奨1（TLSF）・推奨2（o1heap）は不採用
  （後述スコープ外）。
- `docs/dynamic-creation.md` §6（メモリプール）・§7.1（受容済みの残余ウィンドウ2件）
  — 本設計が前提を変える現行記述。
- `docs/qa-esp32s3-20260804.md` R-2（自己削除タスクの資源回収）・
  `docs/qa-esp32s3-20260804-2.md` Q-4（AID_* 予約数の実用上限）
  — いずれも「brk は count==0 でしか戻らない」を前提にした記述を含む。
- `docs/superpowers/specs/2026-08-04-fmp3-dcre-stage3b-dtq-pdq-mpf-design.md` §1
  — mpf ブロック領域ウィンドウ・自終了スタック窓の受容判断（本設計により
  成立確率が変わりうる。§8 で扱う）。

---

## 1. 背景・動機

### 1.1 現行実装の病理

`kernel/startup.c:438-444` の既存コメント（原文）:

> デフォルトのメモリプール管理機能
>
> メモリプール領域の先頭から順に割り当てを行い，すべてのメモリ領域が
> 解放されるまで解放されたメモリ領域を再利用しないメモリプール管理機
> 能．

実装（`kernel/startup.c:550-559`）:

```c
void
free_mempool(MB_T *mempool, void *ptr)
{
	MEMPOOLCB	*p_mempoolcb = ((MEMPOOLCB *) mempool);

	p_mempoolcb->count -= 1;
	if (p_mempoolcb->count == 0) {
		p_mempoolcb->brk = ((char *) mempool) + sizeof(MEMPOOLCB);
	}
}
```

`count` が 0 になった瞬間だけ `brk` が先頭へ戻る。**途中の1個だけを返す
操作ではメモリは戻らない。** 長寿命オブジェクト（例: 常駐 dtq）が1つでも
生存している限り、`brk` は単調に前進し続け、いずれ `E_NOMEM` に到達する。

### 1.2 実測動機

esp32_s3 実機で、**同一タスク ID が1ブート中6回再利用される**パターンが
観測されている（M5Unified の Speaker/Mic の `begin`/`end` → 内部で I2S
タスクの動的 create/delete を繰り返す）。これは「同種・同サイズの割付が
繰り返される」という**実アプリケーションで実際に起きるパターンの証拠**
として本設計の動機になっており、調査報告の推奨3が指摘する状況そのもの
である。現行のバンプ実装では、この create/delete サイクルの最中に他の
長寿命割付（他タスクのスタック、静的でない dtq/pdq/mpf 等）が1つでも
共存していると、サイクルのたびにアリーナが消費されて `E_NOMEM` に到達
しうる。

**帰属の訂正**: esp32_s3 側はこの計測を報告した張本人だが、esp32_s3 の
移植自体は**カーネルメモリプールを一切使っていない**（`_kernel_mpksz = 0`。
mtx/sem は構造的にプール不要、dtq/tsk は `NULL` 供給ではなく shim 側が
用意したバッファを渡す方式を採用しており、これは他ならぬ本プロジェクトが
以前示した「bump は枯渇する」という回答を踏まえた esp32_s3 側の設計判断
である）。したがって esp32_s3 は本設計の**受益者ではなく**、本設計が
入っても esp32_s3 がプール利用へ回帰することもない。本設計の受益者は
`DEF_MPK` を実際に使う一般の FMP3 利用者である。esp32_s3 側からのこの
訂正の詳細は `.steering/20260804-dynamic-object-integration/REPLY3-TO-FMP3CORE.md`
（esp32_s3 側リポジトリ）を参照（内容はここに転記しない）。

---

## 2. 決定事項（ユーザ裁定・2件）

### 決定1: 載せ方 = 既定実装を置換

選択制マクロ（`OMIT_MEMPOOL_DEFAULT` の下流実装）ではなく、**既定実装その
ものを置換**する。理由:

- 全ターゲット（現行5ターゲット・8ビルド構成）が恩恵を受ける。
- 既存 QEMU テスト系（`cfg_equivalence.sh`・`test_dcre*`・error 回帰）
  でそのまま回せる。
- dcre からの意図的な逸脱として `DIVERGENCE_MAP.md` に記録する
  （挙動は malloc/free 契約の範囲内の改善であり、API・呼出し側は無変更）。

### 決定2: 再利用方針 = 完全一致のみ

first-fit＋分割・サイズクラス化は**不採用**。**サイズ（とアライン適合）が
完全一致する解放済みブロックだけ**を再利用する。分割・結合は行わない。
異なるサイズの需要には従来どおり bump で応え、`count==0` での全域リセットは
backstop として残す。

裁定理由（ユーザ判断）: first-fit＋分割まで踏み込むなら TLSF 採用
（調査報告 推奨1）の方が筋が良い。完全一致フリーリストは「実装量最小・
決定的挙動・推奨3の精神を保つ」の3点で、分割ありの中間実装より優れる
という判断。

---

## 3. データ構造とレイアウト

すべて既定実装内部（`kernel/startup.c` の `#ifndef OMIT_MEMPOOL_DEFAULT`
ブロック内）に閉じる。フック自体（`initialize_mempool`/`malloc_mempool`/
`aligned_alloc_mempool`/`free_mempool` の4関数、`kernel/kernel_impl.h:316-320`
の宣言）は変更しない。

```c
typedef struct {
    size_t size;              /* 割付時の要求サイズ（無加工） */
} MPHDR;                      /* 各割付の直前に置くヘッダ */

typedef struct free_block {
    struct free_block *next;  /* 解放済みブロックの単方向 LIFO */
} FREEBLK;                    /* 解放されたユーザ領域を転用して格納 */

typedef struct {
    void    *brk;
    void    *limit;
    uint_t  count;            /* 生存割付数（従来どおり） */
    FREEBLK *freelist;        /* 追加: 解放済みリストの先頭 */
} MEMPOOLCB;
```

### レイアウト

```
[padding][MPHDR][ユーザ領域 size バイト]
```

`aligned = align(brk + sizeof(MPHDR), alignment)` — **ユーザ領域側**を
要求アラインメントに合わせ、ヘッダはその直前（`aligned - sizeof(MPHDR)`）
に置く。

**VERIFY: ヘッダ自身のアラインメント成立性**（事実・コード読解）

ヘッダを直前に置く方式が安全であるためには、`alignment` が常に
`alignof(size_t)` の倍数（`alignment >= alignof(size_t)` かつ両者が
2の巾乗）であることが必要。現行コード（`kernel/startup.c:503-504` の
既存コメント「alignmentは2の巾乗であること（呼出し側の責任．alignof(MB_T)
ないしaligned_alloc_mpkの引数）」）の前提を踏まえ、実際の呼出し側を
全数確認した：

| 呼出し | アラインメント | 値 | 呼出し側 |
|---|---|---|---|
| `malloc_mpk`（`kernel/kernel_impl.h:325-334`） | `alignof(MB_T)` | `alignof(uintptr_t)` = `alignof(size_t)` と同一（`MB_T` = `uintptr_t`、`include/t_stddef.h:131`） | dataqueue.c:404, pridataq.c:376, mempfix.c:278,294（malloc_mpk 直接呼出しは4箇所） |
| `aligned_alloc_mpk(alignof(STK_T), stksz)`（`kernel/kernel_impl.h:336-345`） | `alignof(STK_T)` | ターゲット定義（下表） | task_manage.c:201（1箇所。カーネル内で `aligned_alloc_mpk` を呼ぶのはここのみ） |

`TOPPERS_STK_T` の定義（`alignof(STK_T)` を決める型）を全5アーキで確認:

| arch | 定義 | 型のアライン |
|---|---|---|
| arm_m_gcc（musca_b1, rp2350_pico2） | `long long`（`arch/arm_m_gcc/common/core_stddef.h:63`） | 8B |
| arm_gcc（kria_r5） | `long long`（`arch/arm_gcc/common/core_kernel.h:64`） | 8B |
| arm64_gcc（kria_arm64） | `__int128`（`arch/arm64_gcc/common/core_kernel.h:64`） | 16B |
| riscv_gcc（polarfire_soc_kit, esp32p4） | `__int128`（32bit RISC-V）／`toppers_stk_t`（64bit、`arch/riscv_gcc/common/core_kernel.h:64,66`） | 16B（32bit RISC-V） |

いずれも `alignof(size_t)`（32bit ターゲットで4B、64bit ターゲットで8B）
**以上**であり、かつ2の巾乗どうしなので `alignof(size_t)` の倍数である。
`malloc_mpk` 側は `alignof(MB_T) == alignof(size_t)` と厳密に一致（最小ケース）。

これにより、`aligned`（ユーザ領域先頭）は常に `alignment` の倍数、
かつ `alignment` は常に `alignof(size_t)` の倍数なので `aligned` も
`alignof(size_t)` の倍数。`MPHDR` のサイズは `sizeof(size_t)` ちょうど
（フィールド1個）なので、`header_addr = aligned - sizeof(size_t)` も
`alignof(size_t)` の倍数になる（`aligned` が `alignof(size_t)` の倍数から
その1個分を引いても、`sizeof(size_t)` 自体が `alignof(size_t)` の倍数
（通常は等しい）なので剰余は変わらず0のまま）。

**結論（真）**: 「alignment ≥ alignof(size_t) が成り立つのでヘッダ自身の
アラインも成立する」という前提の記述は、**現行の全呼出し側（malloc_mpk
の4箇所＋aligned_alloc_mpk の1箇所、計5箇所）について真**である。
riscv_gcc の 64bit 経路（`toppers_stk_t`）は現行取り込み5ターゲットに
含まれない（esp32p4 はターゲット依存部を別リポジトリ管理、
`CLAUDE.md` 現況節参照）ため実測未確認だが、`toppers_stk_t` が
`alignof(size_t)` 未満になる設計は考えにくく、念のため実装時に
`static_assert` 等での防御を推奨する（下記§8 制約）。

**最小割付のサイズ**: フリーリストのノード自体は解放された**ユーザ領域**
に書き込むため、ユーザ領域が `sizeof(FREEBLK)`（＝ポインタ1個）未満だと
解放時に書き込めない。カーネル内の実際の malloc_mpk 呼出しで生じうる
最小の要求サイズは `sizeof(DTQMB) * dtqcnt`（`dtqcnt>=1`）で、
`DTQMB`（`kernel/dataqueue.h:57-59`）は

```c
typedef struct data_management_block {
	intptr_t	data;			/* データ本体 */
} DTQMB;
```

の1フィールドのみであり `sizeof(DTQMB) == sizeof(intptr_t)` はポインタ幅
そのものである。すなわち最小の malloc_mpk 呼出し（`dtqcnt==1`）でも要求
サイズはポインタ1個分になり、`FREEBLK`（ポインタ1個）が必ず収まる。
32bit ターゲットでは 4B、64bit ターゲット（kria_arm64）では 8B——
数値は変わるが、「ユーザ領域幅 == ポインタ幅」という関係自体が
`DTQMB`/`FREEBLK`/`intptr_t`/ポインタがすべて同じ語幅である限り機種に
依らず成立する（**「4B」という具体数値は32bitターゲット限定**であり、
`intptr_t` 幅に一般化して読むこと）。

**【裁定 2026-08-04】計画 spec-issue 2 — 上記分析の見落としの訂正**:
`kernel/mempfix.c:294` の `malloc_mpk(sizeof(MPFMB) * blkcnt)` を上記の
全数確認から見落としていた。`MPFMB` は `uint_t`（常に4B）のため、
**64bit ターゲットでは `blkcnt==1` の要求が 4B となり、`FREEBLK`
（8B）がユーザ領域に収まらない**——「ユーザ領域幅 ≥ ポインタ幅」は
64bit では偽。ただし解放時の `FREEBLK` 書込みのはみ出し（4B）は、
`sizeof(FREEBLK) == sizeof(MPHDR)` とアライン剰余の関係により
**次の割付のヘッダ直前に必ず存在する死領域（パディング）に収まり、
隣接割付・ヘッダを破壊しない**。この論証は実装のコードコメントに
明記すること（実装計画 Task 1 に含む）。64bit ターゲットでの runtime
実測は現行テスト体制に無い（dcre 機能テストは musca/kria_r5 系で実行）
ことを既知の未実測事項として記録する。

**【裁定 2026-08-04・最終レビュー】計画 spec-issue 3 — FREEBLK はみ出し論証の
末尾（プール終端）の穴**: 上記のはみ出し論証（「次の割付けのヘッダ直前の
死領域に収まる」）は「次の割付けが存在する」ことを暗黙に仮定しており、
プール終端（`limit`）の扱いを別途検証していなかった。自動生成プール
（`kernel.py` の `ROUND_MB_T`）は `sizeof(MB_T)` の倍数に丸められるため
安全だが、**ユーザ供給 `mpk`**（`DEF_MPK` の `mpk` 引数）は
`CHECK_MB_ALIGN`（=4、`kernel/kernel.py:813`）にしか丸められておらず、
64bit ターゲット（arm64、`sizeof(MPHDR)==8`）では `mpksz ≡ 4 (mod 8)`
になりうる。この場合，丸め無しでは `limit` ちょうどの位置に 4B 割付け
（`sizeof(MPFMB)*1`）が着地でき（`size > limit - aligned` の等号側で
通過），解放時の 8B の `FREEBLK` 書込みが `limit` を 4B 越えてプール外
（他のカーネル構造体やスタック等）を破壊しうる——実装計画レビューで
発見。**解決（構造的な修正、Task 1 fix round で実装）**:
`initialize_mempool` で `limit` を `sizeof(MPHDR)` の倍数へ切り下げる
（`uintptr_t` 算術，`& ~(sizeof(MPHDR)-1)`）。導出：`alloc_mempool` が
生成するすべてのユーザ領域先頭は `sizeof(MPHDR)` の倍数（§3 上記の
アラインメント論証）であり，`limit` も `sizeof(MPHDR)` の倍数なら
`user < limit` から `limit - user >= sizeof(MPHDR)`，すなわち
`user + sizeof(MPHDR) <= limit` が常に成り立つ——これは「次の割付けの
ヘッダが存在するか」に依存しない構造的な保証であり，末尾ケースを
含めてはみ出しが必ずプール内に収まることを閉じる。丸めに伴い，
`initialize_mempool` の最小サイズ検査も丸め後の実効容量で判定するよう
一致させた（丸め前の `size` でなく `limit - mempool >= sizeof(MEMPOOLCB)`）。
在来の挙動は不変：既存の全 `MPK_SIZE` 値・自動生成プールはすでに
`sizeof(MB_T)`（したがって `sizeof(MPHDR)`）の倍数に整列済みのため，
この丸めで実効容量が変わらない（ROM 差分は丸め判定コード自体のみ）。

**サイズ0の割付**: `size==0` の malloc_mpk 呼出しがもし発生すると、
ユーザ領域が0バイトになり `FREEBLK` を書き込む場所が無くなる。現行の
呼出し側（6箇所）を確認した限り、`dtqcnt`/`pdqcnt`/`blkcnt`/`blksz`
はいずれも `CHECK_PAR` で非0が保証されたうえ、乗算・丸めのオーバーフロー
検査（`kernel/mempfix.c:227-257`、`kernel/dataqueue.c`・`kernel/pridataq.c`
の同型検査）が先に効くため、**現行コードには size==0 で malloc_mpk に
到達する経路は無い**（事実・コード読解）。ただし将来の呼出し側追加で
この不変が崩れる可能性はあるため、§8 制約に明記する。

管理領域自体のオーバーヘッド増: `MEMPOOLCB` に `freelist` フィールドが
追加されるため、`sizeof(MEMPOOLCB)` は 32bit ターゲットで
12B（`brk`+`limit`+`count`）→ 16B（+`freelist`）に増える。

---

## 4. アルゴリズム

- **alloc(alignment, size)**: `freelist` を先頭から線形走査し、
  「`MPHDR.size == 要求 size`」かつ「`(uintptr_t)ptr & (alignment-1) == 0`
  （ポインタ実アライン適合）」の**最初のエントリ**を unlink して返す
  （`count++`）。該当なしなら従来の bump。あふれ検査は既存のガード列
  （`kernel/startup.c:506-536`）を拡張するが、ヘッダ直前配置では
  `size + sizeof(MPHDR)` という加算は式の上に現れない——実際にあふれうる
  加算は `brk + sizeof(MPHDR)`（ヘッダ分の前進）とアライン調整加算であり、
  **この2つを順にガードする**（【裁定 2026-08-04】計画 spec-issue 1:
  当初の「size + sizeof(MPHDR) のあふれ検査」という字面は不正確で、
  計画側の式を正とする。意図＝「ヘッダ分を含む全加算があふれないこと」
  は同一）。
- **free(ptr)**: ユーザ領域先頭に `FREEBLK` を書いて `freelist` へ push
  （O(1)、LIFO）、`count--`。ヘッダ（`MPHDR.size`）は触らず残す。
- **count==0**: 従来どおり `brk` を全リセット、かつ `freelist = NULL`
  （backstop 維持。`free()` の同一呼出し内で、push 後に `count` が0へ
  落ちた場合はこのリセットが直後に効き、直前に push した1件も含めて
  `freelist` は空になる）。
- **計算量**: `free` は O(1)。`alloc` は O(freelist 長)（上界はプールに
  収まる最大割付数であり、決定的に有界）。
- **RAM コスト**: 生存割付1件あたり `+sizeof(size_t)`（32bitで4B）の
  ヘッダオーバーヘッド。プール制御ブロック自体も `+sizeof(void*)`
  （32bitで4B）増える。

---

## 5. 既存機構との整合

- **`count==0` backstop**: 変更しない。全解放されたときは従来どおり
  `brk` を先頭へ戻す。これにより「全滅→ 先頭から再利用」という既存の
  観測経路（§7・§8で扱う `test_dcre4.c` の巻き戻し検査）は保存される。
- **`mpk_valid`**（`kernel/kernel_impl.h:296,325-353`）: 無変更。
  `malloc_mpk`/`aligned_alloc_mpk`/`free_mpk` の3 Inline 関数は
  `mpk_valid` を見てから `malloc_mempool`/`aligned_alloc_mempool`/
  `free_mempool` を呼ぶ既存の薄いラッパのままで、フリーリスト機構は
  すべて呼び出し先（既定実装内部）に閉じる。
- **`OMIT_MEMPOOL_DEFAULT` フック**: 無変更。下流が独自実装（TLSF 等）
  へ差し替える経路として温存する（§9 スコープ外）。
- **`kernel_rename.def`**: 新規の外部シンボルは増えない。既定実装が
  外部公開する4関数（`initialize_mempool`/`malloc_mempool`/
  `aligned_alloc_mempool`/`free_mempool`）は既に `kernel/kernel_rename.def:7-10`
  に登録済みで、シグネチャも変わらない。`MPHDR`/`FREEBLK`/`MEMPOOLCB`
  はいずれも `kernel/startup.c` 内で完結する型であり（`MEMPOOLCB` は
  現行実装でも既にファイルローカル）、rename 対象に追加は不要
  （事実・確認済み: `grep` で `kernel_rename.def` に `MEMPOOLCB` 等が
  無いことを確認）。
- **`allfunc.h`/`TOPPERS_kermem`**: 無変更（既定実装関数の集合が
  変わらないため）。

---

## 6. 波及効果

- `kernel/startup.c:438-444` の冒頭コメント（「すべてのメモリ領域が
  解放されるまで解放されたメモリ領域を再利用しない」）を、完全一致
  再利用の説明に書き換える。
- `docs/dynamic-creation.md` §6（メモリプール）の bump 説明・単調枯渇の
  注意（`docs/dynamic-creation.md:415-425`）を更新する。特に
  「途中の1個だけ返す操作ではメモリは戻らない」という現行の断定は、
  完全一致サイズであれば途中の1個の返却でも再利用対象になる、という
  形へ訂正が要る。
- `docs/qa-esp32s3-20260804.md` R-2・`docs/qa-esp32s3-20260804-2.md` Q-4
  への追記。両文書とも「brk は count==0 でしか戻らない／単調に痩せる」
  ことを前提に、ユーザ供給スタック等の運用方針を助言している
  （`docs/qa-esp32s3-20260804.md:27-29`、`docs/qa-esp32s3-20260804-2.md:137`
  付近）。本設計により前提が変わるため、変更予告を追記する
  （ユーザ供給スタック方針の再考材料になり得る、という位置づけであり、
  本 spec の範囲では文書追記のみで、実装は別タスク）。
- `DIVERGENCE_MAP.md` へ意図的逸脱を1行追加（対象: `kernel/startup.c`
  の既定メモリプール実装、種別: 機能拡張、理由: 上記§1、上流報告候補への
  追加はしない — 不具合修正ではなく拡張のため）。

---

## 7. 検証戦略

### 7.1 新規シナリオ

- **枯渇しないことの実証**: 長寿命割付（dtq 1本等）を保持したまま、
  同一サイズの `create/delete × N` 反復が `E_NOMEM` に到達しないこと。
  現行実装ではこの手順は必ず `E_NOMEM` になる（bump が長寿命割付の分
  だけ毎回目減りする）ため、**現行実装での失敗と新実装での成功という
  対比そのものが positive control** になる。
- **サイズ不一致は再利用されない**ことの観測: 異なるサイズの要求では
  `freelist` がヒットせず `brk` が前進し続けることを確認する。
- **mutation control**: 再利用パス（`freelist` からの unlink）を
  意図的に無効化し、N回目で `E_NOMEM` に戻ることを確認する。
- 配置先（`test_dcre4` の拡張か新設 `test_dcre6` か）は実装計画で確定する
  （本 spec ではどちらかを裁定しない）。

### 7.2 既存回帰の監査点（実装計画に明記すべき3点）

1. **`test_dcre4` シナリオB の `E_NOMEM` 期待**
   （`test/test_dcre4.c:229,243` 等、E_NOID との順序実証）。
   これらは「スロット未使用のまま過大な `dtqcnt` で `E_NOMEM`」という
   検査であり、要求サイズが `freelist` に載っている解放済みブロックと
   偶然一致しない限り新設計でも同じ挙動になる。過大サイズはそもそも
   `freelist` 上のどのエントリとも一致しないため影響なし
   （事実・コード読解によるサイズ非一致の確認）。

2. **hardening Task 2 の巻き戻し観測**（`count==0` 経由の間接観測）
   — 下記 §7.3 に机上トレースを記載。

3. **`MPK_SIZE=2048` の negative control 成立条件** — `test_dcre4.c` の
   オーバーフロー系 negative control（例: 587行目の `blksz=UINT_MAX`
   丸めあふれ検査）は、`ROUND_MPF_T`/乗算のあふれ自体を突く検査であり
   `MPHDR` オーバーヘッドの有無に依存しない（あふれの入力は `UINT_MAX`
   などの極端値で、ヘッダ4B/8Bの増減では成立条件が動かない）。

### 7.3 机上トレース: hardening Task 2（`test_dcre4.c` 494-543行）の巻き戻し観測

**対象**: `docs/superpowers/specs/2026-08-04-fmp3-dcre-stage3b-dtq-pdq-mpf-design.md`
に記載の「①は入るが①+②は入らない」サイジングで `acre_mpf` の2段確保
巻き戻しを実証する検査（ケースA: `blkcnt=400,blksz=4` → `E_NOMEM`、
ケースB: `blkcnt=200,blksz=4` → 成功）。

**現状の根拠（コメント原文の算術、`test/test_dcre4.c:506-520`）**:
プール実効容量 = `MPK_SIZE(2048) - sizeof(MEMPOOLCB)(12) = 2036`。
`ROUND_MPF_T(4) == sizeof(MPF_T) == 4`、`sizeof(MPFMB) == 4`。
ケースA: ①`4*400=1600 ≤ 2036`（成功）／②`4*400=1600 > 2036-1600=436`
（失敗）→ `E_NOMEM`。ロールバックで①のみが解放される。
ケースB: ①`4*200=800`＋②`4*200=800`＝`1600 ≤ 2036`（成功）。
「ケースBが成功すること」＝「①の解放が実際に `brk` を戻したこと」の
間接証拠、という設計。

**新実装での机上トレース（事実 = コード読解による経路確認 / 推測 = 数値のシミュレーション）**:

前提の確認（事実）: `test/test_dcre4.c` 全体を `acre_dtq`/`acre_pdq`/
`acre_mpf`/`del_dtq`/`del_pdq`/`del_mpf` の呼出し順で追跡すると
（169〜492行）、**この行までのすべての `acre_*` 呼出しに対応する
`del_*` が、次の `acre_*` より前に必ず呼ばれている**（1:1 ペアリング、
失敗した `acre_*`（`E_NOID`/`E_NOMEM`/`E_PAR`/`E_RSATR`）は巻き戻しに
より `count` を変化させない）。したがって行494（`E_NOMEM` 単発検査、
①段階で失敗するため巻き戻し不要）の直前で、プールの `count` は
新旧どちらの実装でも 0 である。

ケースA（推測・数値シミュレーション、`sizeof(size_t)==4` の32bit
ターゲット前提）:
- `sizeof(MEMPOOLCB)` が `freelist` 追加で 12→16 になるため、
  実効容量は `2048-16=2032`。
- ①: 要求 `size=1600`。`freelist` は直前の全解放で空（`count==0`
  リセット済み）なのでヒットなし → bump。フットプリントは
  `sizeof(MPHDR)(4) + 1600 = 1604`（ヘッダ直前配置でパディング無し、
  §3 のアライン成立性より）。`1604 ≤ 2032` → 成功。残り `428`。
- ②: 要求 `size=1600`（`sizeof(MPFMB)*400`）。`freelist` は
  まだ空（①はまだ解放されていない）→ bump。フットプリント `1604`。
  `1604 > 428` → 失敗 → `E_NOMEM`。
- ロールバック: `pk_cmpf->mpf==NULL` なので `free_mpk(mpf)` が①を解放。
  このとき `count` は「①のみが生存」だった状態から `1→0` になるため、
  `free()` は push 後ただちに `count==0` backstop へ入り、`brk` を
  先頭へリセットし `freelist` を `NULL` に戻す（§4 のとおり、push した
  直後のエントリも含めて消える）。**結果は旧実装の全域リセットと
  ビット単位で同じ状態**（`brk` = プール先頭、`count=0`）になる。

ケースB（推測・数値シミュレーション、続き）:
- ①: 要求 `size=800`。`freelist` は空（直前でリセット済み）→ bump。
  フットプリント `4+800=804`。実効容量 `2032` に対し `804 ≤ 2032` →
  成功。残り `1228`。
- ②: 要求 `size=800`。`freelist` はまだ空（①はまだ生存中で解放されて
  いない）→ bump。フットプリント `804`。`804 ≤ 1228` → 成功。

**結論（推測に基づくが、定性的な成立可否は堅牢）**: ケースAは②で失敗、
ケースBは成功、という**定性的な結果は新実装でも保存される**。理由は
以下の2点:

1. ケースAのロールバックが `count==0` backstop を経由することは
   §5 で無変更と確認済みの機構であり、新実装でも旧実装と**同一の**
   全域リセット（`brk`=先頭、`freelist`=空）を引き起こす。これは
   `freelist` の完全一致再利用という新機構を経由せず、旧来の
   バックストップがそのまま効くケースである。
2. ヘッダオーバーヘッド（+4B/割付）と `MEMPOOLCB` 増分（+4B）を
   加味しても、ケースAの②不足分（`428` 対 要求 `1604`）・ケースBの
   総消費（`1608` 対 容量 `2032`）のいずれも判定を覆すほどの僅差では
   ない（マージンは数百バイト単位）。

**ただし**、`test/test_dcre4.c:506-520` の **「【算術】」コメント本文の
数値**（`sizeof(MEMPOOLCB)=12`・実効容量`2036`・`2036-1600=436` 等）は
**旧レイアウト固有の値であり、新実装では字面として不正確になる**
（新実装での対応値は概算 `16`・`2032`・`428`）。テストの `check_assert`
行そのもの（528, 533, 535行）は変更不要（結果が変わらないため）だが、
**実装時にこのコメント本文の数値を新レイアウトに合わせて書き換える
必要がある**——さもないと算術の説明とコードが食い違ったドキュメントに
なる。これは実装計画のタスクとして明記すること。

### 7.4 その他の全回帰

- 全既存機能テスト（`test_dcre1`〜`test_dcre5`・`test_dcre_mix` 系）・
  エラー行列（`cfg_error_tests/`）・`cfg_equivalence.sh` の全回帰を通す。
- ROM/RAM 増分は同一ビルドパスでの A/B 比較で測る。hardening Task 5 の
  教訓（ビルドパス長が Thumb2 コードサイズへ影響するため worktree 別
  パスでの比較は不可）を踏襲し、同一チェックアウト内でのコミット前後
  比較とする。
- **cfg 側の変更はゼロ**: `DEF_MPK` の処理は `cfg_py/` 側（kernel.py/
  テンプレート）にあり、`mpksz`/`mpk` のサイズ計算・出力ロジックに触れる
  変更ではない（本設計は `kernel/startup.c` のカーネル C 実装のみを
  変更し、cfg が出力するトークン・構造体レイアウトの見積り方には影響
  しない）。よって `cfg_equivalence.sh` への影響なし
  （事実・スコープ確認）。

---

## 8. 制約・受容事項

- **アライン成立性の前提**: §3 の「ヘッダ直前配置が常にアラインする」
  という性質は「呼出し側の `alignment` が常に2の巾乗であり、かつ
  `alignof(size_t)` 以上（またはその倍数）である」ことに依存する。
  現行5ターゲット・全6呼出し箇所で成立を確認したが、**将来カーネル内に
  `aligned_alloc_mpk` の新規呼出しが追加された場合、この前提が自動では
  保証されない**。実装時に `assert`（デバッグビルド限定で可）等での
  防御を検討すること。
- **サイズ0割付は非対応**: `size==0` の割付が発生すると、解放時に
  `FREEBLK` を書き込む領域が無い。現行コードでは全呼出し箇所が
  `CHECK_PAR` とオーバーフロー検査により `size>0` を保証しており到達
  しないが（§3 で確認済み）、この不変はコードの構造ではなく検査の
  存在に依存している。将来の呼出し側追加時にはこの不変を維持すること。
- **`freelist` 走査の最悪計算量**: O(freelist 長)。上界はプールに
  収まる最大割付数で決定的に有界だが、実際に長い `freelist`
  （多品種・多サイズの割付を頻繁に繰り返す使い方）では線形走査の
  コストが無視できなくなる可能性がある。本設計はこれを受容する
  （決定2の裁定どおり、分割・サイズクラス化は不採用）。
- **既存の受容済み残余ウィンドウ2件への影響（追加所見・要検討）**:
  `docs/dynamic-creation.md` §7.1 が記す (1) 自終了タスクのスタック
  残余ウィンドウ・(2) mpf ブロック領域ウィンドウは、いずれも
  「`count==0` になって `brk` がリセットされ、直後の別の割付が同一
  番地に着地する」という機序に依存している。**本設計はこの機序自体は
  変更しないが、それとは別に「完全一致サイズなら `count==0` を待たず
  即座に同一番地が再利用される」という新しい経路を追加する。**
  §1.2 の動機（同一スタックサイズのタスクが繰り返し create/delete
  される）はまさにこの新経路が最も起こりやすい形であり、窓(1)は
  「プール全体が0件になる」という比較的稀な全域条件を待たずに、
  「同じ `stksz` の再要求」という局所条件だけで到達しうるようになる
  （事実: 機序は変更なし。推測: 到達確率は上がる方向——定量評価は
  していない）。窓(1)(2)自体の受容可否は段階1/段階3bで既に裁定済みで
  あり本 spec が覆すものではないが、**§6 の波及効果（qa 文書・
  `docs/dynamic-creation.md` §6 更新）と合わせて、実装計画で
  `docs/dynamic-creation.md` §7.1 の記述更新（「count==0」だけでなく
  「完全一致再利用」も窓の成立経路として明記）を検討すること**を
  ここに記録する。これは本 spec 起草時にコードから導いた追加所見である。
  **【裁定 2026-08-04】ユーザは本所見（窓の到達確率が上がる方向の変化）を
  受容した。**根拠: (a) 解放済みメモリの再利用を行うアロケータ（TLSF/o1heap
  含む）は本質的に同じ性質を持つ、(b) 窓2件は段階1/3b で「アプリ契約」
  （休止確認後の del・未返却ブロック持ち del は契約違反）として裁定済みで
  あり、契約を守る限り窓は開かない、(c) `docs/dynamic-creation.md` §7.1 へ
  成立経路として明記する更新を本 spec §6 が既に要求している。
- **ROM/RAM 増分**: 生存割付あたり `+sizeof(size_t)`（ヘッダ）、プール
  あたり `+sizeof(void*)`（`MEMPOOLCB.freelist`）。実測は §7.4 のA/B
  比較で行う。
- **（記録のみ・修正しない）`MEMPOOLCB` 自身のアラインメント**: 4-aligned
  なユーザ供給 `mpk` ベース番地（64bit ターゲット、`CHECK_MB_ALIGN`=4 が
  `sizeof(void*)`=8 未満）では、`MEMPOOLCB`（プール先頭に直置きされる、
  `brk`/`limit`/`freelist` の3ポインタメンバを持つ）自身が誤アライン
  になりうる。これは freelist 化で新たに生じた問題ではなく、
  `MEMPOOLCB` をプール先頭に直置きする配置自体が dcre の原形から
  継承した既存パターンであり、本 spec のスコープ外として修正しない
  （2026-08-04 最終レビューで指摘・記録のみ）。

---

## 9. スコープ外

- TLSF（調査推奨1）・o1heap（調査推奨2）の採用。将来
  `OMIT_MEMPOOL_DEFAULT` フックを使って下流実装として追加することは
  引き続き可能（本設計はこの経路を塞がない）。
- スタック専用アリーナの分離。
- first-fit・サイズクラス化（決定2で不採用と裁定済み）。
- `docs/dynamic-creation.md`・qa 2文書・`DIVERGENCE_MAP.md` の実際の
  編集（§6 波及効果に記載した内容は、実装タスクとして別途行う）。
