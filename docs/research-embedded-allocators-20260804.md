# 深組込み向け動的メモリ管理ライブラリ調査（FMP3 カーネルプール置換の観点）

- 日付: 2026-08-04
- 目的: FMP3 動的生成拡張のカーネルメモリプール（現状バンプアロケータ：free はカウント減算のみ、
  カウント 0 で break ポインタをリセット。長寿命割付が混ざる create/delete サイクルで単調枯渇）
  の置換／補完候補となる汎用アロケータの実態調査。
- 制約文脈: giant-lock SMP カーネル（呼び出し側でロック済み → アロケータ内部ロック不要）、
  32bit MCU（Cortex-M33/R5、AArch64、RISC-V、Xtensa 下流）、下流 ESP32/ESP32-S3 は
  空き DRAM 約 13.6KB・実測で同一タスク ID が 1 ブート中に 6 回再利用される create/delete サイクルあり、
  Arduino ライブラリとしてのパーミッシブ配布を予定。
- 凡例: 【文書】= 公式文書/README に明記、【論文】= 査読論文、【実測主張】= プロジェクト自身の測定主張、
  【未検証】= 本調査で一次情報を確認できず。推測で数値を埋めていない。

---

## 1. 選定基準（7項目）

1. **決定性**: alloc/free の最悪実行時間が有界か（O(1) か）。リアルタイム適性。
2. **断片化挙動**: 戦略（segregated fit / first-fit / 固定ブロック）、公表された断片化上界の有無。
3. **フットプリント**: コードサイズ、割付あたりオーバーヘッド（B/ブロック）、プール固定費。
4. **ライセンス**: パーミッシブ再配布（Arduino ライブラリ）と両立するか。
5. **可搬性/依存**: pure C か、freestanding か、アラインメント設定、64bit クリーンか。
6. **並行性モデル**: 内部ロックなし（呼び出し側ロック＝当方に適合）か、内蔵ロックか。
7. **成熟度/保守**: 採用実績、最終リリース/コミット、既知問題。

---

## 2. 候補別評価

### 2.1 TLSF（Two-Level Segregated Fit）

**O(1) の機構（決定に直結するので具体的に）**【論文】【文書】:
フリーブロックを 2 段のサイズクラスに分類する。第 1 段は 2 の冪（`fl = fls(size)`）、
第 2 段は各冪区間を線形に等分（`sl = (size >> (fl - SLI_log2)) - SL_count` 相当）。
各段の「空きリストが非空か」をワード幅ビットマップで持ち、
「要求サイズ以上の最小非空クラス」の探索を **ビットマスク＋ffs/fls（CLZ/CTZ 命令）2 回**に置き換える。
探索ループが存在しないため malloc/free/realloc/memalign すべて漸近的に O(1)。
論文の実測は x86 で 200 命令未満【論文】。分類は「good-fit」（best-fit 近似。第 2 段の粒度分だけ過大に丸める）。

| 基準 | 内容 |
|---|---|
| 決定性 | O(1)（malloc/free/realloc/memalign）【論文】【文書】 |
| 断片化 | good-fit。第 2 段線形分割由来の内部断片化は SL 分割数で制御（tlsf-bsd は「大サイズで最大約 3.125% の内部断片化」と主張【文書】）。外部断片化の理論上界の公表はなし（論文は実測で低断片化を報告）【論文】 |
| フットプリント | mattconte 版: 割付あたり 4B、**プール管理 約 3KB**（2 段リストヘッド行列）、コード数 KB【文書】。tlsf-bsd: 割付あたり 1 ワード、コア約 500 行【文書】 |
| ライセンス | mattconte/espressif/sysprog21 系は BSD（tlsf-bsd は BSD-3-Clause）【文書】。**本家 UPV 実装（Masmano 版）は GPL/LGPL 系**（tlsf-bsd README が「仕様から書き起こしたので GPL 制約なし」と明記することの裏返し。UPV 配布条件そのものは本調査では一次確認できず【未検証】） |
| 可搬性 | pure C、freestanding 可、4B アライン前提、64bit 対応（v2.0 以降）【文書】 |
| 並行性 | 非スレッドセーフ（呼び出し側でロック）【文書】。tlsf-bsd は任意でロックラッパあり【文書】 |
| 成熟度 | **mattconte/tlsf は最終コミット 2020-03-29、最終リリース v3.1 は 2016**（事実上停止）【文書】。生きている系譜: **espressif/tlsf**（ESP-IDF が実運用、idf ブランチ）【文書】、**sysprog21/tlsf-bsd**（活発、WCET 測定基盤つき）【文書】 |

一言: アルゴリズムとしては本命だが、**mattconte 本家は死んでおり、使うなら espressif か sysprog21 のフォーク**。
最大の引っかかりは**プール管理 約 3KB の固定費**（13.6KB アリーナの 22% に相当）。

### 2.2 o1heap（Pavel Kirienko）

**断片化上界（決定に直結）**【文書】:
最悪ケース所要メモリ（WCMC）は

> H(M, n) = 2 M (1 + ⌈log2 n⌉)

（M = ピーク総需要、n = 最大割付サイズ）。精密版として
H_b(M,n,l,a) = a·k + 2·l·n·M_f·(⌈log2 n_f⌉+1)/(l+n) も README に導出つきで掲載。
この上界の代償として割付サイズを 2 の冪に丸める設計（式の因子 2M はそれに由来。丸め挙動自体の
明文はフェッチ範囲で直接確認できていないため、式からの解釈として記す【文書（式）／解釈】）。
つまり **「このアリーナサイズなら絶対に枯渇しない」を計算で保証できる**唯一の候補。

| 基準 | 内容 |
|---|---|
| 決定性 | 定数 WCET。実測主張: RP2350 で alloc ≈120 cycles、free ≤115 cycles、「サイズ・履歴・断片化・使用量に依らず」【実測主張】 |
| 断片化 | 上記 WCMC 上界あり（catastrophic fragmentation 回避を設計目標）【文書】。内部断片化は最悪 ~2 倍（2 の冪丸めの帰結） |
| フットプリント | <500 LoC【文書】。割付あたりオーバーヘッド＝アラインメント＝**2×ポインタ幅（32bit で 8B）**（v3.0.0 で 4× から削減)【文書】 |
| ライセンス | MIT【文書】 |
| 可搬性 | C99/C11、MISRA C:2012 準拠（Clang-Tidy で強制）、8〜64bit アーキ対応を明記【文書】 |
| 並行性 | 内部ロックなし。「並行アクセスを避けよ、必要ならロックせよ」【文書】＝ giant lock と適合 |
| 成熟度 | v3.0.0 = 2026-02-09。作者いわく「完成したので動きが少ない」【文書】。安全重視組込み（Cyphal/UAVCAN 系エコシステム）向けに設計。README に "Used by" の具体名リストは確認できず【未検証】 |

一言: 品質・保証・ライセンス・並行性モデルすべて当方向き。**唯一の弱点は 2 の冪丸めによる内部断片化**
— 例えばスタック 3KB 要求が 4KB フラグメントを消費する。13.6KB アリーナではこれが痛い。
TCB のような小型・準固定サイズのオブジェクトには痛くない。

### 2.3 umm_malloc（rhempel）

| 基準 | 内容 |
|---|---|
| 決定性 | 上界主張なし。既定 UMM_BEST_FIT はフリーリスト全走査（O(フリーブロック数)）、UMM_FIRST_FIT も選択可【文書】 |
| 断片化 | best-fit ＋隣接ブロック結合。8B ブロック粒度で断片化に強い実績（小 RAM 機で長年運用）【文書】 |
| フットプリント | ヒープを 8B ブロック＋15bit インデックスで管理（最大 32767 ブロック ≈ 256KB）。**割付ブロックのオーバーヘッド 4B**（フリーブロックは 8B）。コードは意図的に極小【文書】 |
| ライセンス | MIT【文書】 |
| 可搬性 | pure C。ARM7 起源、8/16/32bit 対応を明記。ポインタでなくインデックスで管理するため RAM 節約【文書】 |
| 並行性 | クリティカルセクションマクロ（UMM_CRITICAL_*）を利用側が定義する方式【文書】＝内部ロックなし相当で適合 |
| 成熟度 | 活発（最終コミット 2026-06-25）。**ESP8266 Arduino core が同梱・実運用**（`cores/esp8266/umm_malloc/` に存在を確認）【文書】。マルチヒープ API あり |
| 追加機能 | UMM_INTEGRITY_CHECK / UMM_POISON_CHECK（ヒープ破壊検出）【文書】 |

一言: 「数十 KB RAM の Arduino 配布」という当方下流の条件をそのまま生きてきたライブラリ。
決定性の保証はないが、**カーネルプールのようにブロック数が少ない小アリーナでは走査長も実質有界**。

### 2.4 FreeRTOS heap_1〜heap_5（事実上のベースライン）

| 基準 | 内容 |
|---|---|
| 決定性 | heap_1 のみ決定的（割付のみ・解放不可）。heap_4/heap_5 は first-fit＋結合で**非決定的（上界主張なし）**【文書】 |
| 断片化 | heap_2: best-fit・**結合なし**（レガシー）。heap_4: first-fit＋隣接結合。heap_5: heap_4＋複数非連続リージョン【文書】 |
| フットプリント | ブロックヘッダは 32bit で 8B 相当（BlockLink_t: next＋size）— ソースからの読み取りで、公式文書の数値としては【未検証】 |
| ライセンス | MIT【文書】 |
| 可搬性 | C だが FreeRTOS の port 層（portBYTE_ALIGNMENT、vTaskSuspendAll 等）に結合しており、単体切り出しには改造が要る |
| 並行性 | pvPortMalloc 内部でスケジューラ停止/クリティカルセクション＝**内蔵ロックあり**（当方には冗長） |
| 成熟度 | 組込み業界最大級の配備実績。ただしアルゴリズムとしては 1990 年代水準の first-fit |

一言: 「これで足りる場面が多い」ことの証明として重要だが、当方が新規に採用する理由は薄い
（決定性なし・port 層結合・内蔵ロック）。heap_2 の「結合なし」は当方の現状バンプ枯渇と同型の病理を持つ。

### 2.5 Zephyr sys_heap

| 基準 | 内容 |
|---|---|
| 決定性 | 「全 API が定数時間で完了、典型 1〜200 cycles」と文書は主張。ただしバケット内探索は `CONFIG_SYS_HEAP_ALLOC_LOOPS`（既定 3）で**打ち切りにより有界化**（断片化耐性と引き換えの予測可能性）【文書】 |
| 断片化 | 8B チャンク粒度、2 の冪バケットのフリーリスト（3-4, 5-8, … チャンク）、「最小・最断片化ブロックから割付」、解放時に隣接結合【文書】。上界の公表なし |
| フットプリント | メタデータは全てヒープ内（ヘッダ: 長さ・前チャンク長・使用ビット・フリーリストポインタ）。外部には sys_heap 構造体のみ【文書】。コードサイズ数値の公表は確認できず【未検証】 |
| ライセンス | Apache-2.0（Zephyr 本体）【文書】 |
| 可搬性 | C。ただし Zephyr ツリー内実装であり、単体抽出には zephyr ヘッダの置換が要る |
| 並行性 | sys_heap 自体は**非同期化（呼び出し側で直列化）**【文書】＝適合。k_heap がロック付きラッパ |
| 成熟度 | Zephyr の現行標準ヒープとして活発に保守。複数リージョンは sys_multi_heap ラッパ【文書】 |

一言: 設計は「TLSF の思想を 1 段バケットに簡略化＋探索打ち切りで有界化」。品質は高いが、
抽出コストがあり、当方が TLSF/o1heap でなくこれを選ぶ積極理由は薄い。

### 2.6 ESP-IDF heap コンポーネント（multi_heap）

| 基準 | 内容 |
|---|---|
| 決定性 | **v4.3 以降 TLSF ベース**（espressif/tlsf、mattconte フォーク）で「ほぼ定数時間」を主張。v4.2 以前はフリーリスト方式【文書】 |
| 断片化 | TLSF に準ずる |
| フットプリント | TLSF のプール管理固定費が現実に効いた実例あり: **v4.3 移行で ESP32 の報告空きヒープが約 10KB 減った**調査記事（Nature Engineering Blog）と、空きサイズ計算の不具合 issue #8270【文書/第三者調査】 |
| ライセンス | Apache-2.0（ESP-IDF）【文書】 |
| 可搬性 | ESP 専用（マルチヒープ・capability 付き割付・ポイズニング等が IDF に結合） |
| 並行性 | **内蔵ロックでスレッドセーフ**【文書】（当方 giant lock とは二重ロックになる） |
| 成熟度 | ESP32 全製品で実運用。破壊検出（heap_caps_check_integrity 等）充実【文書】 |

一言: 下流 ESP32 ポートでは**既にシステム側に TLSF が居る**という事実が重要。
ESP32 側だけカーネルプールを heap_caps に委譲する選択肢は「コード増ゼロ」で成立し得る
（ただし IDF ヒープのロックと giant lock の重なり、および決定性は IDF の主張水準「ほぼ定数」止まり）。

### 2.7 newlib / dlmalloc（アンチベースライン：デフォルトで付いてくるもの）

- newlib の malloc は **dlmalloc v2.6.5 系**の移植【文書/第三者】。newlib-nano は縮小版 nano-malloc を持つ。
- スレッド安全は `__malloc_lock/__malloc_unlock` のリターゲットに依存し、**再帰的に呼ばれることがある**
  仕様【文書】。過去には newlib-nano でこのフックがコメントアウトされていた時期がある（後に修正）【文書】。
- 決定性・断片化上界なし。sbrk 相当の実装も要求。フル newlib はコードサイズが nano 比で倍近くになる報告【第三者】。
- ライセンスは newlib 全体として BSD 系の寄せ集め（コンポーネントごとに異なる）。
- 結論: **カーネルプール用途には全項目で不適**。比較の物差しとしてのみ有用。

### 2.8 固定ブロックプール（CMSIS-RTOS2 osMemoryPool / µC/OS-III OS_MEM）

- CMSIS-RTOS2 osMemoryPool: **固定 block_size 単位でのみ割付/解放**。「ヒープより高速で断片化しない」と明記。
  必要メモリは最低 `block_count × block_size`（解放済みブロックをリンクリスト管理に再利用）。
  Alloc(timeout=0)/Free 等は ISR 安全【文書】。CMSIS_5 は Apache-2.0【文書】。
- µC/OS-III の OS_MEM（メモリパーティション）も同クラス（固定サイズ・O(1)）。現行は
  weston-embedded/uC-OS3 として Apache-2.0 公開【文書。O(1) 特性の明文は本調査では未フェッチ【未検証】】。
- 本質: **サイズが既知のオブジェクトには決定性・断片化ゼロ・オーバーヘッド最小の最適解**。
  可変サイズ（ユーザ指定スタック）には使えない、が唯一にして決定的な制約。

### 2.9 その他（新興）

- **sysprog21/tlsf-bsd**: 上記 2.1 に統合済み（生きている TLSF として推奨系譜）。
- Rust 系組込みアロケータ（emballoc 等）は C RTOS カーネルへの採用実績を確認できず、対象外とした【未検証】。

---

## 3. 横断比較表

| 候補 | 最悪時間 | 断片化上界 | 割付オーバーヘッド | プール固定費 | コード | ライセンス | ロック | 保守 |
|---|---|---|---|---|---|---|---|---|
| TLSF (mattconte 系) | O(1)【論文】 | なし（実測低）| 4B（tlsf-bsd: 1 ワード） | **約 3KB** | 数 KB | BSD | 呼出側 | 本家停止 (2020)・フォーク活発 |
| o1heap | O(1) WCET【実測主張】 | **H=2M(1+⌈log2 n⌉)** | 8B @32bit | 小（数値未公表） | <500 LoC | MIT | 呼出側 | v3.0.0 (2026-02) |
| umm_malloc | 上界なし（best-fit 走査） | なし（実績良） | 4B | 小 | 極小 | MIT | 呼出側(マクロ) | 活発 (2026-06) |
| FreeRTOS heap_4/5 | 上界なし | なし | ~8B【未検証】 | 小 | 小 | MIT | 内蔵 | 活発・最大配備 |
| Zephyr sys_heap | 打ち切りで有界【文書】 | なし | 8B チャンク粒度 | バケットヘッド配列 | 未公表 | Apache-2.0 | 呼出側 | 活発 |
| ESP-IDF multi_heap | 「ほぼ定数」(TLSF) | なし | TLSF 準拠 | TLSF 準拠（~10KB 減の実例） | IDF 同梱 | Apache-2.0 | **内蔵** | 活発 |
| newlib malloc | 上界なし | なし | 8B+ | 大 | 数 KB〜 | BSD 系混在 | フック依存 | 保守中 |
| 固定ブロックプール | **O(1)** | **ゼロ** | ~0（リスト再利用） | ~0 | 極小 | Apache-2.0 (CMSIS) | 実装次第 | 活発 |

---

## 4. 文脈への当てはめ（FMP3 カーネルプール）

前提の再確認: giant lock 下で呼ばれるので**内部ロックは不要どころか有害**（ESP-IDF heap の内蔵ロックは減点）。
割付対象はカーネルオブジェクト（TCB 等の**準固定サイズ少品種**）＋ユーザ指定**スタック（可変サイズ）**。
下流最小構成はアリーナ十数 KB。

**推奨 1: TLSF（tlsf-bsd または espressif/tlsf を vendor import、コアの汎用プールとして）**
O(1)・4B/ブロック・BSD・呼出側ロックと、7 基準中 6 つで最良クラス。唯一の懸念「プール管理約 3KB」は、
FL 段の最大インデックスをアリーナ上限（例: 64KB）に合わせて縮めればリストヘッド行列は大幅に減る
（mattconte 版の ~3KB は 4GB 級ヒープ前提の既定値）。ESP32 下流では espressif フォークが既に実績を持つ
アルゴリズムそのものであり、挙動の一貫性も得やすい。本家が停止しているため**フォーク選定と pin が必須**
（当プロジェクトの vendor import 流儀にそのまま乗る）。

**推奨 2: o1heap（決定性と「枯渇しない保証」を最優先するなら）**
WCMC 式でアリーナ設計を机上検証できる唯一の候補で、MISRA/MIT/<500行/呼出側ロックと
カーネル同梱に最も向く品質。ただし 2 の冪丸めのため、**スタックのような大きく半端なサイズが主客の
13.6KB アリーナでは実効容量が最悪半分**になる。「TCB 等の管理ブロックは o1heap、スタックは別枠」と
分ければ弱点は消える。

**推奨 3（構成次第で最有力）: 「置換しない」— 現行バンプ＋オブジェクト種別フリーリスト（固定ブロック化）**
実測事実（同一 TSKID が 6 回再利用）は、**同種・同サイズの割付が繰り返される**ことを意味する。
TCB などサイズが型ごとに一定のものは、解放時に型別フリーリストへ返すだけで断片化ゼロ・O(1)・追加コードほぼゼロ
（CMSIS osMemoryPool / µC/OS OS_MEM が数十年やってきた定石そのもの）。残る問題は可変サイズのスタックのみで、
ここだけ (a) 生成時パラメタでサイズクラス化して同様にフリーリスト化するか、(b) 上記 TLSF/o1heap を
スタック専用アリーナに使うか、の二段構えにできる。**変更量・リスク・決定性の総合では、まずこれを検討すべき**。

**決定的に除外**: newlib/dlmalloc（決定性なし・ロックフック依存・サイズ過大 — 全基準で不適合）、
UPV 本家 TLSF 実装（GPL/LGPL 系で Arduino パーミッシブ配布と不整合。アルゴリズム自体は BSD 実装で使える）、
FreeRTOS heap_2（結合なし＝現行バンプと同型の枯渇病理）。
mattconte/tlsf「本家そのまま」も保守停止（最終コミット 2020-03-29）のため、採るならフォーク経由。

---

## 5. 出典一覧

- TLSF 論文: Masmano et al., "TLSF: a New Dynamic Memory Allocator for Real-Time Systems", ECRTS 2004 — http://www.gii.upv.es/tlsf/files/papers/ecrts04_tlsf.pdf ／ 詳細版 SPE 2008, Vol.38 No.10 pp.995-1026 — https://www.wide-dot.com/tlsf/paper/spe_2008.pdf
- mattconte/tlsf（README: BSD、O(1)、4B/alloc、プール管理 ~3KB、64bit v2.0〜、v3.1=2016；commits: 最終 2020-03-29）— https://github.com/mattconte/tlsf
- sysprog21/tlsf-bsd（BSD-3、1 ワード/alloc、コア ~500 行、内部断片化 ~3.125% 主張、WCET 測定基盤）— https://github.com/sysprog21/tlsf-bsd
- espressif/tlsf（ESP-IDF が使用する mattconte フォーク、idf ブランチ）— https://github.com/espressif/tlsf
- o1heap（README: MIT、C99/C11・MISRA C:2012、<500 LoC、オーバーヘッド 2×ポインタ、WCET 実測主張、H(M,n)=2M(1+⌈log2 n⌉)）— https://github.com/pavel-kirienko/o1heap ／ releases（v3.0.0=2026-02-09、2.2.0=2025-05-12、2.1.1=2023-04-15）— https://github.com/pavel-kirienko/o1heap/releases
- rhempel/umm_malloc（README: MIT、8B ブロック・15bit インデックス、4B/alloc、BEST_FIT/FIRST_FIT、クリティカルセクションマクロ；commits: 最終 2026-06-25）— https://github.com/rhempel/umm_malloc
- ESP8266 Arduino core の umm_malloc 同梱 — https://github.com/esp8266/Arduino/tree/master/cores/esp8266/umm_malloc
- FreeRTOS ヒープ文書（heap_1〜heap_5 のアルゴリズム・決定性記述）— https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/01-Memory-management ／ 書籍版 — https://freertos.gitbook.io/mastering-the-freertos-tm-real-time-kernel/mastering.ch03
- Zephyr sys_heap 文書（8B チャンク、2 の冪バケット、CONFIG_SYS_HEAP_ALLOC_LOOPS=3、非同期化）— https://docs.zephyrproject.org/latest/kernel/memory_management/heap.html
- ESP-IDF Heap Memory Allocation（スレッドセーフ明記、整合性チェック）— https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/mem_alloc.html
- ESP-IDF v4.3 TLSF 移行の告知（フォーラム）— https://www.esp32.com/viewtopic.php?t=15322 ／ v4.3 空きヒープ計算不具合 issue #8270 — https://github.com/espressif/esp-idf/issues/8270 ／ v4.3 で空きヒープ ~10KB 減の調査（Nature Engineering Blog）— https://engineering.nature.global/entry/esp-idf-v4.3-allocator
- CMSIS-RTOS2 Memory Pool 文書（固定ブロック、ISR 安全性、必要メモリ）— https://arm-software.github.io/CMSIS_5/RTOS2/html/group__CMSIS__RTOS__PoolMgmt.html
- µC/OS-III（Apache-2.0 公開リポジトリ）— https://github.com/weston-embedded/uC-OS3
- newlib malloc のロック仕様・nano-malloc（launchpad Q&A）— https://answers.launchpad.net/gcc-arm-embedded/+question/229509 ／ newlib vs newlib-nano のサイズ比較 — https://mcuoneclipse.com/2023/01/28/which-embedded-gcc-standard-library-newlib-newlib-nano/ ／ dlmalloc 2.6.5 系である旨（第三者解説）— https://blog.fluentech.info/2018/11/newlibs-mallocs.html
