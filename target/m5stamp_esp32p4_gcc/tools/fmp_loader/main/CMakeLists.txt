idf_component_register(SRCS "fmp_loader.c"
                    INCLUDE_DIRS ".")

#  PRC_NUM=1切り分け実験用: 環境変数 FMP_PRC_NUM=1 の時のみ
#  core1 起動コードを無効化する（FMP_SINGLE_CORE を定義）。
if(DEFINED ENV{FMP_PRC_NUM} AND "$ENV{FMP_PRC_NUM}" STREQUAL "1")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FMP_SINGLE_CORE=1)
endif()

#
#  main コンポーネントへ，外部（アプリ側のビルドスクリプト）から
#  追加ソース・追加リンクフラグを与えるための汎用フック．
#
#    FMP_EXTRA_SRCS    : 空白区切りの .c パス（IDF 側でコンパイルするもの）
#    FMP_EXTRA_LDFLAGS : 空白区切りのリンクフラグ（-Wl,--wrap=... 等）
#
#  いずれも未設定なら何もしない（素の fmp_loader と同一のリンク結果になる）．
#  FMP3 側 make のリンクに渡す EXTRA_LDFLAGS（build_fmp3_lib.sh）とは別物で，
#  こちらは IDF 側の最終リンクに効く．
#
if(DEFINED ENV{FMP_EXTRA_SRCS} AND NOT "$ENV{FMP_EXTRA_SRCS}" STREQUAL "")
    separate_arguments(_fmp_extra_srcs UNIX_COMMAND "$ENV{FMP_EXTRA_SRCS}")
    target_sources(${COMPONENT_LIB} PRIVATE ${_fmp_extra_srcs})
endif()
if(DEFINED ENV{FMP_EXTRA_LDFLAGS} AND NOT "$ENV{FMP_EXTRA_LDFLAGS}" STREQUAL "")
    separate_arguments(_fmp_extra_ldflags UNIX_COMMAND "$ENV{FMP_EXTRA_LDFLAGS}")
    target_link_options(${COMPONENT_LIB} PUBLIC ${_fmp_extra_ldflags})
endif()

#
#  FMP3 を静的ライブラリ(libfmp3.a)として取り込む（方式(a)）．
#  build_fmp3_lib.sh が configure.rb/make で FMP3(sample1, PRC_NUM=1)をビルドし，
#  全 text を IRAM へ回すリネームを施して fmp3_build/libfmp3.a を生成する．
#  app_main が toppers_start を参照することで start.o → kernel_cfg.o → … と
#  必要メンバが芋づる式にリンクされる．
#
set(FMP3_DIR  "${CMAKE_CURRENT_LIST_DIR}/..")
set(FMP3_LIB  "${FMP3_DIR}/fmp3_build/libfmp3.a")

#  idf.py build の一部として FMP3 ライブラリをビルドする．
add_custom_command(
    OUTPUT  "${FMP3_LIB}"
    COMMAND "${FMP3_DIR}/build_fmp3_lib.sh"
    WORKING_DIRECTORY "${FMP3_DIR}"
    COMMENT "Building FMP3 static library (libfmp3.a)"
    VERBATIM)
add_custom_target(fmp3_lib DEPENDS "${FMP3_LIB}")
add_dependencies(${COMPONENT_LIB} fmp3_lib)

target_link_libraries(${COMPONENT_LIB} PUBLIC "${FMP3_LIB}")
#  toppers_start を確実に取り込む（保険）
target_link_libraries(${COMPONENT_LIB} PUBLIC "-u toppers_start")
