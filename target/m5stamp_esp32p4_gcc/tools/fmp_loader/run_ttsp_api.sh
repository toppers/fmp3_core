#!/bin/bash
#  ESP32-P4 で本物の TTSP3 API 適合性テスト（自動生成の多群スイート）を実行する runner．
#
#  前提:
#    - phase1（マニフェスト+ディレクトリ）生成済み:
#        cd <ttsp3>
#        export TTSP_TARGET_NAME=m5stamp_esp32p4_gcc
#        printf '1\n1\n3\n<DIV>\n5\nr\nr\nr\nq\n' | bash ttb.sh fmp3 FMP obj_p4_api
#      （DIV は IRAM に収まるよう大きめ．実績: DIV=160 で 1 群=約18ケースが収まる）
#    - IDF 環境: . $IDF_PATH/export.sh
#      （IDF_TOOLS_PATH は導入時と使用時で必ず同じ値にすること．未設定時の既定は
#        ~/.espressif．導入手順は ../../target_user.md「開発環境の準備」を参照）
#    - 基板は ESP32-P4．**/dev/ttyACM* の番号は接続順で変わる**ため，複数ボードを
#      接続している場合はシリアル（= MAC 文字列）で対象を同定してから実行すること．
#        for d in /dev/ttyACM*; do
#          echo "$d $(udevadm info -q property -n $d | grep -oE 'ID_SERIAL_SHORT=[^ ]*' | cut -d= -f2)"
#        done
#
#  各群について TTG（out.c/out.cfg/out.h 生成）→ ttsp_target.sh の simulation()
#  （build_fmp3_lib.sh で libfmp3.a 化 → idf.py build → flash → シリアル捕捉）→
#  "All check points passed." でマーカー判定する．基板は1枚のため直列実行．
#
#  使い方:
#    ./run_ttsp_api.sh [OBJDIR] [group...]
#    例) ./run_ttsp_api.sh obj_p4_api 1            # 群1のみ
#        ./run_ttsp_api.sh obj_p4_api $(seq 1 160) # 全群
#  環境変数:
#    TTSP_RUN_SECS : 1群の最大キャプチャ秒（既定 90）
set -u
#  外部 TTSP3 スイート（FMP3 外．TTSP3_DIR で上書き可）
TT="${TTSP3_DIR:-$HOME/TOPPERS/ttsp3}"
TGT=$TT/library/FMP/target/m5stamp_esp32p4_gcc
TTG=$(realpath "$TT/tools/ttg/bin/ttg.rb")
YAML=$(realpath "$TGT/configure.yaml")
TTG_FLAGS="-c $YAML -f --prc_num 2 --out_file_name out --func_time false --func_interrupt false --func_exception false --irc_arch global"

OBJDIR="${1:-obj_p4_api}"; shift || true
GRPS="$*"; [ -z "$GRPS" ] && GRPS=1

#  simulation() を取り込む
# shellcheck disable=SC1090
source "$TGT/ttsp_target.sh"

SUM="/tmp/ttsp_p4_summary.txt"; : > "$SUM"
for g in $GRPS; do
  DIR="$TT/$OBJDIR/api_test/auto_code_$g"
  if [ ! -d "$DIR" ]; then printf '%-14s %s\n' "group_$g" "NO_DIR" | tee -a "$SUM"; continue; fi
  cd "$DIR" || { printf '%-14s %s\n' "group_$g" "CD_FAIL" | tee -a "$SUM"; continue; }
  #  TTG（未生成なら生成）
  if [ ! -f out.c ] || [ ! -f out.cfg ]; then
    list=$(grep -v '^#' "MANIFEST_AUTO_CODE_$g" | tr '\n' ' ')
    ruby "$TTG" $TTG_FLAGS $list > result_ttg.log 2>&1 || {
      printf '%-14s %s\n' "group_$g" "TTG_FAIL" | tee -a "$SUM"; continue; }
  fi
  #  IDF フロー（build+flash+capture）．simulation() は標準出力にログを出す．
  OUT=$(simulation)
  echo "$OUT" > execute.log
  if echo "$OUT" | grep -qaE "## Unexpected|## Assertion|## Internal inconsistency"; then R=FAIL
  elif echo "$OUT" | grep -qa "All check points passed"; then R=PASS
  elif echo "$OUT" | grep -qaE "IDF BUILD FAIL|FLASH FAIL"; then R=BUILD_FAIL
  else R=NO_MARKER; fi
  printf '%-14s %s\n' "group_$g" "$R" | tee -a "$SUM"
done
echo "=== summary ==="; cat "$SUM"
echo "PASS=$(grep -c ' PASS$' "$SUM") FAIL=$(grep -c ' FAIL$' "$SUM") OTHER=$(grep -cE 'NO_MARKER|BUILD_FAIL|TTG_FAIL|NO_DIR' "$SUM")"
