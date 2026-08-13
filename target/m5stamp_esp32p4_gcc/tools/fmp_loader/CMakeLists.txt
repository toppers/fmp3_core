# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

#
#  追加の IDF コンポーネントを外部から取り込むための汎用フック．
#
#  環境変数 FMP_EXTRA_COMPONENT_DIRS に ':' 区切りのディレクトリ列を与えると，
#  そのまま EXTRA_COMPONENT_DIRS へ渡す．未設定なら素の fmp_loader（FMP3 を
#  libfmp3.a として抱えるだけの最小構成）としてビルドされる．
#
#  与えるコンポーネントの名前（＝ディレクトリ名）を IDF 組込みコンポーネント
#  と同名にすれば，そのコンポーネントの上書きにも使える（EXTRA_COMPONENT_DIRS
#  由来のものは __build_init で登録される IDF 組込みより後に登録されるため，
#  IDF のコンポーネント名衝突解決規則により後勝ちになる）．
#
#  用途例（本ファイルはどの用途にも依存しない）:
#    FMP_EXTRA_COMPONENT_DIRS=/path/to/foo_component:/path/to/esp_event
#
if(DEFINED ENV{FMP_EXTRA_COMPONENT_DIRS} AND NOT "$ENV{FMP_EXTRA_COMPONENT_DIRS}" STREQUAL "")
    string(REPLACE ":" ";" EXTRA_COMPONENT_DIRS "$ENV{FMP_EXTRA_COMPONENT_DIRS}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fmp_loader)
