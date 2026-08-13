#!/bin/bash
#  ESP32-P4 で FMP3 のカーネルテスト(test/ の test_*.c)を実行する runner．
#  (注: これは FMP3 本体の test/ であり，別物の正式 TTSP3 適合性スイート ttsp3/ ではない．
#   マーカー TTSP_RESULT/All check points passed は FMP3 の test_svc.c が出力する共有基盤．)
#  各テストを build_fmp3_lib.sh override でビルド→flash→serial capture→マーカー判定．
#  使い方: ./run_fmp_test.sh [PRC_NUM] test_name...
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; cd "$HERE"
#  FMP3 ルートは本スクリプト位置から導出（<fmp3>/target/m5stamp_esp32p4_gcc/tools/fmp_loader/）．
FMP="${FMP3:-$(cd "$HERE/../../../.." && pwd)}"
PRC="${1:-1}"; shift
#  ログ出力先はセッション非依存の安定パス(TTSP_LOGD で上書き可)．
#  旧版は特定セッションの scratchpad をハードコードしており，別セッション/エージェント
#  実行時にログ取りこぼし→NO_MARKER 誤判定の原因になっていたため固定パスにする．
LOGD="${TTSP_LOGD:-/tmp/ttsp3_logs}"
mkdir -p "$LOGD"
SUM="$LOGD/ttsp_summary.txt"
: > "$SUM"
for t in "$@"; do
  L="$LOGD/ttsp_$t.txt"
  #  cpuexc 系は専用 cfg を持たず共通 test_cpuexc.cfg を使う
  CFG=""
  case "$t" in test_cpuexc*) CFG="test_cpuexc";; esac
  #  perf 系(perf0..5)は histogram サービスで実行時間を計測する(P4 は mcycle 直読み
  #  = target_syssvc.h の HIST_GET_TIM 上書き)．histogram.o のリンクが要る．
  SOBJS="test_svc.o"
  case "$t" in perf*) SOBJS="test_svc.o histogram.o";; esac
  rm -rf fmp3_build >/dev/null 2>&1
  FMP_APP=$FMP/test FMP_APPNAME=$t FMP_CFG=$CFG PRC_NUM=$PRC EXTRA_SOBJS="$SOBJS" \
    idf.py build >/tmp/b.log 2>&1
  if [ $? -ne 0 ]; then printf '%-16s %s\n' "$t" "BUILD_FAIL" | tee -a "$SUM"; continue; fi
  idf.py -p /dev/ttyACM0 flash >/dev/null 2>&1 || { printf '%-16s %s\n' "$t" "FLASH_FAIL" | tee -a "$SUM"; continue; }
  #  マーカー検出で早期終了する適応キャプチャ(最大 ${CAPT:-90}s)．
  #  遅いテスト(dlynse/hrt1/MP系)は固定13sでは出力が間に合わず NO_MARKER 化するため．
  MAXT="${CAPT:-90}"
  : > "$L"
  script -qfc "idf.py -p /dev/ttyACM0 monitor" /dev/null > "$L" 2>&1 &
  SPID=$!
  #  test_svc.c は機械可読マーカー TTSP_RESULT: PASS/DONE/FAIL を出力する．
  #  PASS=check_point付き完走 / DONE=計測系の完走(check_finish(0)) / FAIL=エラー．
  for ((i=0; i<MAXT; i++)); do
    sleep 1
    if grep -qaE "TTSP_RESULT:|All check points passed|## Unexpected|## Assertion|## Internal|This test program is not necessary" "$L" 2>/dev/null; then break; fi
    kill -0 "$SPID" 2>/dev/null || break
  done
  kill "$SPID" 2>/dev/null
  pkill -f "esp_idf_monitor" 2>/dev/null; pkill -f "idf_monitor" 2>/dev/null
  sleep 1
  C=$(sed 's/\x1b\[[0-9;]*m//g' "$L" | tr -d '\0')
  if echo "$C" | grep -qaE "TTSP_RESULT: FAIL|## Unexpected|## Assertion|## Internal"; then R=FAIL
  elif echo "$C" | grep -qaE "TTSP_RESULT: PASS|All check points passed"; then R=PASS
  elif echo "$C" | grep -qa "TTSP_RESULT: DONE"; then R=DONE
  elif echo "$C" | grep -qa "This test program is not necessary"; then R=SKIP
  else R=NO_MARKER; fi
  printf '%-16s %s\n' "$t" "$R" | tee -a "$SUM"
done
echo "=== summary ==="; cat "$SUM"
echo "PASS=$(grep -cE ' PASS$' "$SUM") DONE=$(grep -cE ' DONE$' "$SUM") SKIP=$(grep -cE ' SKIP$' "$SUM") FAIL=$(grep -cE ' FAIL$' "$SUM") OTHER=$(grep -cE 'NO_MARKER|BUILD_FAIL|FLASH_FAIL' "$SUM")"
