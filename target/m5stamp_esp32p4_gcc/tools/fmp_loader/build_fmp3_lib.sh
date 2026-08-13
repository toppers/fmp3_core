#!/bin/bash
#
#  方式(a)用: FMP3(sample1) を configure.rb/make でビルドし，全 obj を
#  静的ライブラリ libfmp3.a にまとめる．IDF の main コンポーネントがこれをリンクする．
#  - PRC_NUM=2（SMP: HP 2コア）
#  - TOPPERS_OMIT_BSS_INIT/DATA_INIT: .data/.bss は IDF 起動が初期化するため
#    start.S の自前初期化を抑止する．
#  - ABI は Makefile.chip で ilp32f（IDF と一致）．
#
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
#  FMP3 ルートはツリー内の本スクリプト位置から導出する（自己完結）．
#  本スクリプトは <fmp3>/target/m5stamp_esp32p4_gcc/tools/fmp_loader/ に置かれる．
#  FMP3 環境変数で上書きも可．
FMP="${FMP3:-$(cd "$HERE/../../../.." && pwd)}"
B="$HERE/fmp3_build"
#  アプリ: 最小周期タスクアプリ(tools/fmp_app)．dly_tsk による周期 syslog で
#  タイマ tick 割込み・スケジューラ・logtask を end-to-end 検証する．
#  (sample1 は対話駆動で serial RX 未実装では動かないため差し替え)
APP="${FMP_APP:-$HERE/../fmp_app}"
APPNAME="${FMP_APPNAME:-fmp_app}"

#  FMP_EXTRA_APPDIRS: 追加の appldirs（空白区切り、-a へ $APP と連結して渡す）。
#  アプリ本体以外のディレクトリ（OS 適応層やデバイスドライバ等）を併せて
#  ビルドしたい外部アプリ向けの汎用フック。既定は空（従来どおり $APP のみ）。
#  FMP_EXTRA_OBJS: 追加でリンクする obj（-U）。既定は空（従来どおり -U 無し）。
EXTRA_APPDIRS="${FMP_EXTRA_APPDIRS:-}"

#  cfg ファイル名が APPNAME と異なるテスト(例: test_cpuexc* は共通の
#  test_cpuexc.cfg を使う)向けに FMP_CFG で .cfg を明示指定できる（下の
#  CFG_ARGS 構築時に反映）．

rm -rf "$B"; mkdir -p "$B"; cd "$B"
#  configure.rb の各オプション値（-I を多数含む EXTRA_COPTS 等）はスペースを
#  含みうるため、単純な文字列展開（$COPTOPT 等の非クオート展開）だと
#  シェルに単語分割されて configure.rb の OptionParser が個々の -I... を
#  未知のトップレベルオプションと誤認する（実際に発生した不具合）。
#  bash 配列で構築し、各要素をクオート付きで ruby へ渡すことで防ぐ．
CFG_ARGS=(ruby "$FMP/configure.rb" -T m5stamp_esp32p4_gcc -D "$FMP" -w
  -S "syslog.o banner.o serial.o serial_cfg.o logtask.o chip_serial.o ${EXTRA_SOBJS:-}"
  -a "$APP ${EXTRA_APPDIRS}" -A "$APPNAME")
if [ -n "${FMP_CFG:-}" ]; then CFG_ARGS+=(-c "${FMP_CFG}.cfg"); fi
if [ -n "${FMP_EXTRA_OBJS:-}" ]; then CFG_ARGS+=(-U "${FMP_EXTRA_OBJS}"); fi
CFG_ARGS+=(PRC_NUM="${PRC_NUM:-2}")
if [ -n "${EXTRA_COPTS:-}" ]; then CFG_ARGS+=(-o "${EXTRA_COPTS}"); fi
if [ -n "${EXTRA_LDFLAGS:-}" ]; then CFG_ARGS+=(-b "${EXTRA_LDFLAGS}"); fi
CFG_ARGS+=(-O "-DTOPPERS_OMIT_BSS_INIT -DTOPPERS_OMIT_DATA_INIT")
"${CFG_ARGS[@]}"

#  obj 毎に単一 .text にする(Makefile.chip の SECTION_OPTS を上書き)．
#  後段の objcopy で .text→.iram1.fmptext へ一括リネームし，FMP3 の全 text を
#  内部 RAM(IRAM)へ集める．これで
#  (1) flash MMU 窓(.flash_rodata_dummy)との overlap を回避，
#  (2) start.S 等 asm の jal(±1MB) が同一 IRAM 内に収まる．
#  C の libgcc(__udivdi3)等への呼出しは -mcmodel=medany により遠距離可．
#  PIE_LAZY=1 を環境で渡すと PIE を遅延オーナ方式(lazy)でビルドする(Makefile.chip の
#  ifdef PIE_LAZY → -DTOPPERS_PIE_LAZY)．既定(未設定)は eager．lazy ではアプリ cfg に
#  DEF_EXC(EXCNO_IINST, pie_exc_handler) の登録が必要．
make SECTION_OPTS="-fno-function-sections" ${PIE_LAZY:+PIE_LAZY=1} ${NO_COPROC:+NO_COPROC=1}

#
#  FMP3 独自セクションを標準 .data.* にリネームする．
#  IDF の sections.ld は *(.data .data.*) で全入力を拾うため，リネームすれば
#  外部 lib でも IDF の .data 配置・初期化(flash→RAMコピー)に自動的に乗る．
#  (これをしないと .kernel_data_CLS_*/.stack_CLS_* が orphan となり overlap する)
#
#  さらに .text を .iram1.fmptext へリネームし，内部 RAM(IRAM)で実行させる．
#  FMP3 の巨大な .text を IDF の .flash.text(XIP)へ足すと flash MMU 窓
#  (.flash_rodata_dummy)と overlap するため，RAM 実行に回す(FMP3 は元々 RAM 実行前提)．
#  PRC数(クラス数)に依存しないよう，各 obj の実セクションを走査して
#  .kernel_data_CLS_* / .stack_CLS_* を一意な .data.* へ，.text を IRAM へリネーム．
#  (PRC_NUM=2 では CLS_PRC2/CLS_ALL_PRC2 のセクションも生じるため動的対応)
for o in objs/*.o; do
  args="--rename-section .text=.iram1.fmptext"
  secs=$(riscv32-esp-elf-objdump -h "$o" | awk '/\.kernel_data_CLS_|\.stack_CLS_/{print $2}')
  for s in $secs; do
    san=$(echo "$s" | sed 's/[^A-Za-z0-9]/_/g')
    args="$args --rename-section $s=.data.fmp$san"
  done
  riscv32-esp-elf-objcopy $args "$o" "$o"
done

#  cfg1_out*.o は cfg pass1 の中間生成物で，sta_ker/_kernel_istkpt_table 等の
#  スタブ定義を含む．最終リンクには kernel_cfg.o(pass2) を使うため，archive から
#  除外しないと "multiple definition" になる．
OBJS=$(ls objs/*.o | grep -vE "/cfg1_out(_c)?\.o$")
riscv32-esp-elf-ar crs libfmp3.a $OBJS
echo "built: $B/libfmp3.a ($(echo "$OBJS" | wc -l) objs)"
