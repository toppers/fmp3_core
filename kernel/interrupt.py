# -*- coding: utf-8 -*-
#
#   TOPPERS/FMP Kernel
#       Toyohashi Open Platform for Embedded Real-Time Systems/
#       Flexible MultiProcessor Kernel
# 
#   Copyright (C) 2015 by FUJI SOFT INCORPORATED, JAPAN
#   Copyright (C) 2015-2020 by Embedded and Real-Time Systems Laboratory
#               Graduate School of Information Science, Nagoya Univ., JAPAN
# 
#   上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
#   ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
#   変・再配布（以下，利用と呼ぶ）することを無償で許諾する．
#   (1) 本ソフトウェアをソースコードの形で利用する場合には，上記の著作
#       権表示，この利用条件および下記の無保証規定が，そのままの形でソー
#       スコード中に含まれていること．
#   (2) 本ソフトウェアを，ライブラリ形式など，他のソフトウェア開発に使
#       用できる形で再配布する場合には，再配布に伴うドキュメント（利用
#       者マニュアルなど）に，上記の著作権表示，この利用条件および下記
#       の無保証規定を掲載すること．
#   (3) 本ソフトウェアを，機器に組み込むなど，他のソフトウェア開発に使
#       用できない形で再配布する場合には，次のいずれかの条件を満たすこ
#       と．
#     (a) 再配布に伴うドキュメント（利用者マニュアルなど）に，上記の著
#         作権表示，この利用条件および下記の無保証規定を掲載すること．
#     (b) 再配布の形態を，別に定める方法によって，TOPPERSプロジェクトに
#         報告すること．
#   (4) 本ソフトウェアの利用により直接的または間接的に生じるいかなる損
#       害からも，上記著作権者およびTOPPERSプロジェクトを免責すること．
#       また，本ソフトウェアのユーザまたはエンドユーザからのいかなる理
#       由に基づく請求からも，上記著作権者およびTOPPERSプロジェクトを
#       免責すること．
# 
#   本ソフトウェアは，無保証で提供されているものである．上記著作権者お
#   よびTOPPERSプロジェクトは，本ソフトウェアに関して，特定の使用目的
#   に対する適合性も含めて，いかなる保証も行わない．また，本ソフトウェ
#   アの利用により直接的または間接的に生じたいかなる損害に関しても，そ
#   の責任を負わない．
# 
#  $Id: interrupt.py (converted from interrupt.trb by Claude Code Sonnet 4.6) $
# 

#
#		割込み管理機能の生成スクリプト
#

#
#  マルチプロセッサ割込み処理を省略するかどうかのデフォルト定義
#
if "OMIT_MULTIPRC_INTERRUPT" not in globals():
    OMIT_MULTIPRC_INTERRUPT = False

#
#  kernel_cfg.hの生成
#
#  ★TNUM_ISRID と各ISRIDのマクロ定義は，本ファイル末尾の IsrObject().generate()
#  が共通枠組み（kernel/kernel.py:168-175）として出力する．ISRがランタイム
#  オブジェクトになったため，他のオブジェクト種別と同じ枠組みに載せた．
#  本ファイルが kernelCfgH へ書くのはこの1箇所だけなので，出力位置が末尾へ
#  移動しても kernel_cfg.h の内容は変わらない（Task 2 Step 12 で実証する）．
#

#
#  kernel_cfg.cの生成
#
kernelCfgC.comment_header("Interrupt Management Functions")

#
#  トレースログマクロのデフォルト定義
#
kernelCfgC.add("""\
#ifndef LOG_ISR_ENTER
#define LOG_ISR_ENTER(isrid)
#endif /* LOG_ISR_ENTER */

#ifndef LOG_ISR_LEAVE
#define LOG_ISR_LEAVE(isrid)
#endif /* LOG_ISR_LEAVE */
""")

#
#  CRE_ISRで使用できる割込み番号とそれに対応する割込みハンドラ番号のデ
#  フォルト定義
#
if "INTNO_CREISR_VALID" not in globals():
    INTNO_CREISR_VALID = INTNO_VALID
if "INHNO_CREISR_VALID" not in globals():
    INHNO_CREISR_VALID = INHNO_VALID

#
#  全割込み番号／割込みハンドラ番号のリストを求める
#
#  ★Ruby版（.values.flatten.uniq）は出現順を保存するが、list(set(...))は
#  順序不定（DIVERGENCE_MAP.md「未解決事項」に記録していたもの。現状これら
#  3つの変数はすべて `in` によるメンバーシップ判定にしか使われておらず
#  （本ファイル中で grep 済み）実害は無いが、dict.fromkeys(...) で
#  Rubyと同じ「出現順を保った重複除去」に揃えても副作用が無く安全なため、
#  将来この変数を列挙生成に使い始めても事故らないようここで直しておく。
INTNO_VALID_ALL = list(dict.fromkeys(v for lst in INTNO_VALID.values() for v in lst))
INTNO_CREISR_VALID_ALL = list(dict.fromkeys(v for lst in INTNO_CREISR_VALID.values() for v in lst))
INHNO_VALID_ALL = list(dict.fromkeys(v for lst in INHNO_VALID.values() for v in lst))

#
#  CFG_INTで使用できる割込み優先度のデフォルト定義
#
if "INTPRI_CFGINT_VALID" not in globals():
    INTPRI_CFGINT_VALID = list(range(TMIN_INTPRI, TMAX_INTPRI + 1))

#
#  割込み番号と割込みハンドラ番号の変換テーブルの作成
#
toInhnoVal = {}
toIntnoVal = {}
for prcid in range(1, TNUM_PRCID + 1):
    if len(INTNO_CREISR_VALID[prcid]) != len(INHNO_CREISR_VALID[prcid]):
        error_exit(f"the length of `INTNO_CREISR_VALID[{prcid}]' is different"
                   f" from the length of `INHNO_CREISR_VALID[{prcid}]'")
    toInhnoVal[prcid] = {}
    toIntnoVal[prcid] = {}
    inhno_creisr_valid = list(INHNO_CREISR_VALID[prcid])
    for intnoVal in INTNO_CREISR_VALID[prcid]:
        inhnoVal = inhno_creisr_valid.pop(0)
        toInhnoVal[prcid][intnoVal] = inhnoVal
        toIntnoVal[prcid][inhnoVal] = intnoVal

#
#  割込み要求ラインに関するエラーチェック（1回目）
#
for _, params in cfgData["CFG_INT"].items():
    # クラスの囲みの中に記述されていない場合（E_RSATR）
    if "class" not in params:
        error_ercd("E_RSATR", params, "%apiname must be within a class")
        params["class"] = TCLS_ERROR

    # 割込み要求を受け付けるプロセッサの設定
    if not OMIT_MULTIPRC_INTERRUPT:
        params["affinityPrcBitmap"] = clsData[params["class"]]["affinityPrcBitmap"]
    else:
        # 割付け可能プロセッサが複数あるクラスの囲み内にCFG_INTを記述した
        # 場合には，警告メッセージを出し，初期割付けプロセッサのみで割込み
        # 要求を受け付けるように設定する．
        initPrcBitmap = 1 << (clsData[params["class"]]["initPrc"] - 1)
        if clsData[params["class"]]["affinityPrcBitmap"] != initPrcBitmap:
            warning_api(params,
                        f"%%intno configured within the class "
                        f"{clsData[params['class']]['clsid']} "
                        f"is configured to be accepted by the processor "
                        f"{clsData[params['class']]['initPrc']} only.")
        params["affinityPrcBitmap"] = initPrcBitmap

    # intnoが有効範囲外の場合（E_PAR）
    if params["intno"] not in INTNO_VALID_ALL:
        error_illegal("E_PAR", params, "intno")
    else:
        # クラスIDがエラーの場合は，エラーチェックをスキップする
        if str(params["class"]) != "":
            for prcid in clsData[params["class"]]["affinityPrcList"]:
                if params["intno"] not in INTNO_CREISR_VALID[prcid]:
                    error_ercd("E_RSATR", params,
                               "the assignable processors "
                               "of the class in which %apiname of `%intno' is described "
                               "must be included in the set of processors "
                               "to which the interrupt is requested")
                    break

    # intatrが無効の場合（E_RSATR）
    if (params["intatr"] & ~(TA_ENAINT | TA_EDGE | TARGET_INTATR)) != 0:
        error_illegal_sym("E_RSATR", params, "intatr", "intno")

    # intpriが有効範囲外の場合（E_PAR）
    if params["intpri"] not in INTPRI_CFGINT_VALID:
        error_illegal_sym("E_PAR", params, "intpri", "intno")

    # カーネル管理外に固定されているintnoに対して，intpriにTMIN_INTPRI以上の値
    if "INTNO_FIX_NONKERNEL" in globals() \
            and params["intno"] in INTNO_FIX_NONKERNEL:
        if params["intpri"] >= TMIN_INTPRI:
            error_ercd("E_OBJ", params, "%%intno must have higher priority "
                       "than TMIN_INTPRI in %apiname")

    # カーネル管理に固定されているintnoに対して，intpriにTMIN_INTPRIよりも
    # 小さい値が指定された場合（E_OBJ）
    if "INTNO_FIX_KERNEL" in globals() \
            and params["intno"] in INTNO_FIX_KERNEL:
        if params["intpri"] < TMIN_INTPRI:
            error_ercd("E_OBJ", params, "%%intno must have lower or equal "
                       "priority to TMIN_INTPRI in %apiname")

    # ターゲット依存のエラーチェック
    if "TargetCheckCfgInt" in globals():
        TargetCheckCfgInt(params)

#
#  割込みハンドラに関するエラーチェック
#
for _, params in cfgData["DEF_INH"].items():
    # クラスの囲みの中に記述されていない場合（E_RSATR）
    if "class" not in params:
        error_ercd("E_RSATR", params, "%apiname must be within a class")
        params["class"] = TCLS_ERROR
    params["prcid"] = clsData[params["class"]]["initPrc"]

    # inhnoが有効範囲外の場合（E_PAR）
    if params["inhno"] not in INHNO_VALID_ALL:
        error_illegal("E_PAR", params, "inhno")
    else:
        # クラスの初期割付けプロセッサが，割込みハンドラ番号に対応するプロ
        # セッサでない場合（E_RSATR）
        if params["inhno"] not in INHNO_VALID[params["prcid"]]:
            error_ercd("E_RSATR", params,
                       "the initial assignment processor "
                       "of the class in which %apiname of `%inhno' is described "
                       "must be the processor corresponding to `%inhno'")

    # inhatrが無効の場合（E_RSATR）
    if (params["inhatr"] & ~(TARGET_INHATR)) != 0:
        error_illegal_sym("E_RSATR", params, "inhatr", "inhno")

    # カーネル管理外に固定されているinhnoに対して，inhatrにTA_NONKERNELが
    # 指定されていない場合（E_RSATR）
    if "INHNO_FIX_NONKERNEL" in globals() \
            and params["inhno"] in INHNO_FIX_NONKERNEL:
        if (params["inhatr"] & TA_NONKERNEL) == 0:
            error_ercd("E_RSATR", params,
                       "%%inhno must be non-kernel interrupt in %apiname")
            continue

    # カーネル管理に固定されているinhnoに対して，inhatrにTA_NONKERNELが指
    # 定されている場合（E_RSATR）
    if "INHNO_FIX_KERNEL" in globals() \
            and params["inhno"] in INHNO_FIX_KERNEL:
        if (params["inhatr"] & TA_NONKERNEL) != 0:
            error_ercd("E_RSATR", params,
                       "%%inhno must not be non-kernel interrupt in %apiname")
            continue

    if params["inhno"] in INHNO_CREISR_VALID[params["prcid"]]:
        # 割込みハンドラ番号に対応する割込み番号がある場合
        intnoVal = toIntnoVal[params["prcid"]][params["inhno"].val]

        # inhnoに対応するintnoに対するCFG_INTがない場合（E_OBJ）
        if intnoVal not in cfgData["CFG_INT"]:
            error_ercd("E_OBJ", params,
                       f"intno `{intnoVal}' corresponding to "
                       "%%inhno in %apiname is not configured with CFG_INT")
        else:
            intnoParams = cfgData["CFG_INT"][intnoVal]

            # 割込みハンドラ番号に対応するプロセッサが，割込みハンドラ番号に
            # 対応する割込み要求ラインが属するクラスの割付け可能プロセッサに
            # 含まれていない場合（E_RSATR）
            if params["prcid"] not in clsData[intnoParams["class"]]["affinityPrcList"]:
                error_ercd("E_RSATR", params,
                           "the processor corresponding to "
                           "`%inhno' must be included in the set of processors "
                           "which can accept the interrupt")
            else:
                if OMIT_MULTIPRC_INTERRUPT \
                        and clsData[intnoParams["class"]]["initPrc"] != params["prcid"]:
                    warning_api(params,
                                "the processor corresponding to `%inhno' "
                                "is not the processor which accepts the interrupt")

            if intnoParams["intpri"] < TMIN_INTPRI:
                # inhnoに対応するintnoに対してCFG_INTで設定された割込み優先度
                # がTMIN_INTPRIよりも小さく，inhatrにTA_NONKERNELが指定されて
                # いない場合（E_OBJ）
                if (params["inhatr"] & TA_NONKERNEL) == 0:
                    error_ercd("E_OBJ", params,
                               "TA_NONKERNEL must be set for "
                               "non-kernel interrupt handler in %apiname of %%inhno")
            else:
                # inhnoに対応するintnoに対してCFG_INTで設定された割込み優先度
                # がTMIN_INTPRI以上で，inhatrにTA_NONKERNELが指定されている
                # 場合（E_OBJ）
                if (params["inhatr"] & TA_NONKERNEL) != 0:
                    error_ercd("E_OBJ", params,
                               "TA_NONKERNEL must not be set for "
                               "kernel interrupt handler in %apiname of %%inhno")

    # ターゲット依存のエラーチェック
    if "TargetCheckDefInh" in globals():
        TargetCheckDefInh(params)

#
#  割込みサービスルーチン（ISR）に関するエラーチェックと割込みハンドラの生成
#
for _, params in sorted(cfgData["CRE_ISR"].items()):
    # クラスの囲みの中に記述されていない場合（E_RSATR）
    if "class" not in params:
        error_ercd("E_RSATR", params, "%apiname must be within a class")
        params["class"] = TCLS_ERROR

    # 割込みサービスルーチンを実行するプロセッサに関するチェック
    if OMIT_MULTIPRC_INTERRUPT:
        initPrcBitmap = 1 << (clsData[params["class"]]["initPrc"] - 1)
        if clsData[params["class"]]["affinityPrcBitmap"] != initPrcBitmap:
            warning_api(params,
                        f"`%isrid' created within the class "
                        f"{clsData[params['class']]['clsid']} "
                        f"is configured to be executed by the processor "
                        f"{clsData[params['class']]['initPrc']} only.")

    # isratrが無効の場合（E_RSATR）
    if (params["isratr"] & ~(TARGET_ISRATR)) != 0:
        error_illegal("E_RSATR", params, "isratr")

    # intnoが有効範囲外の場合（E_PAR）
    if params["intno"] not in INTNO_CREISR_VALID_ALL:
        error_illegal("E_PAR", params, "intno")
    else:
        # クラスIDがエラーの場合は，エラーチェックをスキップする
        if str(params["class"]) != "":
            # クラスの割付け可能プロッサが，intnoで指定した割込み要求ライン
            # が接続されたプロセッサの集合に含まれていない場合（E_RSATR）
            for prcid in clsData[params["class"]]["affinityPrcList"]:
                if params["intno"] not in INTNO_CREISR_VALID[prcid]:
                    error_ercd("E_RSATR", params,
                               "the assignable processors "
                               "of the class in which %apiname of `%intno' is described "
                               "must be included in the set of processors "
                               "to which the interrupt is requested")
                    break

    # isrpriが有効範囲外の場合（E_PAR）
    if not (TMIN_ISRPRI <= params["isrpri"] and params["isrpri"] <= TMAX_ISRPRI):
        error_illegal("E_PAR", params, "isrpri")

    # intnoに対応するinhnoに対してDEF_INHがある場合（E_OBJ）
    #
    #  ★intnoが有効範囲外（E_PAR、:302）や、このクラスのaffinityPrcListに
    #  含まれるプロセッサに対してintnoが無効（E_RSATR、:309-316）の場合でも
    #  このループ自体は無条件に実行される（上のエラーチェックはcontinueせず
    #  処理を継続するため）。そのため toInhnoVal[prcid] に
    #  params["intno"].val がキーとして存在しないことがある。
    #  Ruby版（cfg/interrupt.trb:376）は $toInhnoVal[prcid][...] が
    #  Hashの欠損キー参照で nil を返し、続く has_key?(nil) が偽になって
    #  素通りする（例外にならない）。dict[...] の直接参照はKeyErrorで
    #  クラッシュし、後続の本来出るべき診断（E_OBJ等）を握り潰してしまう
    #  ため、Rubyと同じ「無ければNone」の意味になる .get() を使う。
    for prcid in clsData[params["class"]]["affinityPrcList"]:
        inhnoVal = toInhnoVal[prcid].get(params["intno"].val)
        if inhnoVal in cfgData["DEF_INH"]:
            error_ercd("E_OBJ", params,
                       f"%%intno in %apiname is duplicated "
                       f"with inhno {cfgData['DEF_INH'][inhnoVal]['inhno']}")

    # intnoに対するCFG_INTがない場合（E_OBJ）
    if params["intno"] not in cfgData["CFG_INT"]:
        error_ercd("E_OBJ", params,
                   "%%intno in %apiname is not configured with CFG_INT")
    else:
        intnoParams = cfgData["CFG_INT"][params["intno"]]

        # CFG_INTとCRE_ISRが異なるクラスの囲みの中にある場合（E_RSATR）
        if params["class"] != intnoParams["class"]:
            error_ercd("E_RSATR", params,
                       "%%intno in %apiname "
                       "does not belong to the same class with CFG_INT")

        # intnoでカーネル管理外の割込みを指定した場合（E_OBJ）
        if intnoParams["intpri"] < TMIN_INTPRI:
            error_ercd("E_OBJ", params,
                       "interrupt service routine cannot handle "
                       "non-kernel interrupt in %apiname of %isrid")

    # ターゲット依存のエラーチェック
    if "TargetCheckCreIsr" in globals():
        TargetCheckCreIsr(params)

#
#  動的ISR生成の対象とする割込み番号（ENA_DYNISR）に関するエラーチェック
#
#  ★このループは，下のインライン連鎖生成ループより前に置かなければならない．
#  生成ループは cfgData["DEF_INH"] へ「生成した割込みハンドラのDEF_INH相当」を
#  追加するので，後に置くと自分が生成したDEF_INHを競合とみなしてしまう
#  （CRE_ISRの同じ検査が :340-345 で生成ループより前に置かれているのと同じ理由）．
#
dynIsrList = []
for _, params in cfgData["ENA_DYNISR"].items():
    # クラスの囲みの中に記述されていない場合（E_RSATR）
    #
    #  ENA_DYNISRはCFG_INT・CRE_ISRと同じくクラス内APIである（AID_ISRだけが
    #  クラス外専用）．対象の割込み要求ラインが属するクラスを指定させることで，
    #  動的ISRを実行するプロセッサ集合がCFG_INTと一致することを保証する．
    if "class" not in params:
        error_ercd("E_RSATR", params, "%apiname must be within a class")
        params["class"] = TCLS_ERROR
        continue

    # intnoが有効範囲外の場合（E_PAR）
    if params["intno"] not in INTNO_CREISR_VALID_ALL:
        error_illegal("E_PAR", params, "intno")
        continue

    # intnoに対するCFG_INTがない場合（E_OBJ）
    if params["intno"] not in cfgData["CFG_INT"]:
        error_ercd("E_OBJ", params,
                   "%%intno in %apiname is not configured with CFG_INT")
        continue

    intnoParams = cfgData["CFG_INT"][params["intno"]]

    # CFG_INTとENA_DYNISRが異なるクラスの囲みの中にある場合（E_RSATR）
    if params["class"] != intnoParams["class"]:
        error_ercd("E_RSATR", params,
                   "%%intno in %apiname "
                   "does not belong to the same class with CFG_INT")
        continue

    # intnoでカーネル管理外の割込みを指定した場合（E_OBJ）
    if intnoParams["intpri"] < TMIN_INTPRI:
        error_ercd("E_OBJ", params,
                   "interrupt service routine cannot handle "
                   "non-kernel interrupt in %apiname of %%intno")
        continue

    # intnoに対応するinhnoに対してDEF_INHがある場合（E_OBJ）
    conflict = False
    for prcid in clsData[params["class"]]["affinityPrcList"]:
        inhnoVal = toInhnoVal[prcid].get(params["intno"].val)
        if inhnoVal in cfgData["DEF_INH"]:
            error_ercd("E_OBJ", params,
                       f"%%intno in %apiname is duplicated "
                       f"with inhno {cfgData['DEF_INH'][inhnoVal]['inhno']}")
            conflict = True
    if conflict:
        continue

    dynIsrList.append(params["intno"].val)

dynIsrList.sort()

# ENA_DYNISRが1個以上あるのに静的なCRE_ISRが0個の構成は，共通枠組みが
# initialize_isrの登録を len(cfgData["CRE_ISR"]) > 0 に条件づけているため
# （kernel/kernel.py:200,257-258），isr_queue_tableが未初期化のまま
# call_isrが走ることになる．cfgエラーで弾く．
if len(dynIsrList) > 0 and len(cfgData["CRE_ISR"]) == 0:
    for _, params in cfgData["ENA_DYNISR"].items():
        error_ercd("E_OBJ", params,
                   "%apiname requires at least one CRE_ISR in the system")

# AID_ISRが1個以上あるのにENA_DYNISRが0個の構成は，適格なintnoが1つも無い
# ためacre_isrが必ずE_OBJで失敗し，予約したISRCBが死蔵される．cfgエラーで弾く．
numAutoIsrid = 0
for _, params in cfgData["AID_ISR"].items():
    numAutoIsrid += int(params["noisr"])
if numAutoIsrid > 0 and len(dynIsrList) == 0:
    for _, params in cfgData["AID_ISR"].items():
        error_ercd("E_OBJ", params,
                   "AID_ISR requires at least one ENA_DYNISR in the system")

#
#  割込みサービスルーチン呼出しキューのデータ構造
#
#  ★dcre（interrupt.trb:263-294）は「CFG_INTがありDEF_INHが競合しない全intno」に
#  キューを作るが，FMP3はENA_DYNISRで明示されたintnoにだけ作る（案B-2）．
#  これにより，ENA_DYNISRの無い構成ではキュー表が空になり，割込みハンドラの
#  生成も従来のインライン連鎖のままになる．
#
isrQueueHeader = {}
for index, intnoVal in enumerate(dynIsrList):
    isrQueueHeader[intnoVal] = f"&(_kernel_isr_queue_table[{index}])"

kernelCfgC.add2(f"const uint_t _kernel_tnum_isr_queue = {len(dynIsrList)};")

if len(dynIsrList) > 0:
    kernelCfgC.add(f"const ISR_ENTRY _kernel_isr_queue_list"
                   f"[{len(dynIsrList)}] = {{")
    for index, intnoVal in enumerate(dynIsrList):
        if index > 0:
            kernelCfgC.add(",")
        kernelCfgC.append(f"\t{{ {intnoVal}, {isrQueueHeader[intnoVal]} }}")
    kernelCfgC.add()
    kernelCfgC.add2("};")
    kernelCfgC.add2(f"ISRQCB _kernel_isr_queue_table[{len(dynIsrList)}];")
else:
    kernelCfgC.add("TOPPERS_EMPTY_LABEL(const ISR_ENTRY, "
                   "_kernel_isr_queue_list);")
    kernelCfgC.add2("TOPPERS_EMPTY_LABEL(ISRQCB, _kernel_isr_queue_table);")

isr_flag = {}
for prcid in range(1, TNUM_PRCID + 1):
    for intnoVal in INTNO_CREISR_VALID[prcid]:
        # 割込み番号intnoValに対して登録されたISRのリストの作成
        isrParamsList = []
        for _, params in sorted(cfgData["CRE_ISR"].items()):
            if params["intno"] == intnoVal:
                isrParamsList.append(params)

        # ★動的ISR生成の対象（ENA_DYNISR）とされた割込み番号
        #
        #  静的なCRE_ISRが1本も無くてもキュー方式の割込みハンドラを生成する
        #  （動的生成専用の割込み番号がありうるため）．クラスはCFG_INTから
        #  取る．静的ISRがある場合も同じ値になる（CRE_ISRとCFG_INTが同一
        #  クラスであることは :354-358 のE_RSATR検査が保証している）．
        #
        #  DEF_INHはaffinityPrcListに含まれる全プロセッサぶん生成する
        #  （既存のインライン連鎖と同じ機構）．inthdr本体はisr_flagで
        #  1回だけ生成し，全プロセッサのベクタで共有する．
        if intnoVal in isrQueueHeader:
            clsid = cfgData["CFG_INT"][intnoVal]["class"]

            # 割込みを受け付けるプロセッサでない場合はスキップ
            if prcid not in clsData[clsid]["affinityPrcList"]:
                continue

            inhnoVal = toInhnoVal[prcid][intnoVal]
            cfgData["DEF_INH"][inhnoVal] = {
                "inhno": NumStr(inhnoVal),
                "inhatr": NumStr(TA_NULL, "TA_NULL"),
                "inthdr": f"_kernel_inthdr_{intnoVal}",
                "class": clsid
            }

            if not isr_flag.get(intnoVal, False):
                kernelCfgC.add("void")
                kernelCfgC.add(f"_kernel_inthdr_{intnoVal}(void)")
                kernelCfgC.add("{")
                kernelCfgC.add(f"\t_kernel_call_isr({isrQueueHeader[intnoVal]});")
                kernelCfgC.add2("}")
                isr_flag[intnoVal] = True
            continue

        # 割込み番号intnoValに対して登録されたISRが存在する場合
        if len(isrParamsList) > 0:
            inhnoVal = toInhnoVal[prcid][intnoVal]
            clsid = isrParamsList[0]["class"]

            # 割込みを受け付けるプロセッサでない場合はスキップ
            if prcid not in clsData[clsid]["affinityPrcList"]:
                continue

            # DEF_INHに相当するデータを生成
            cfgData["DEF_INH"][inhnoVal] = {
                "inhno": NumStr(inhnoVal),
                "inhatr": NumStr(TA_NULL, "TA_NULL"),
                "inthdr": f"_kernel_inthdr_{intnoVal}",
                "class": clsid
            }

            if not isr_flag.get(intnoVal, False):
                # 割込みサービスルーチンを呼び出す割込みハンドラの生成
                kernelCfgC.add("void")
                kernelCfgC.add(f"_kernel_inthdr_{intnoVal}(void)")
                kernelCfgC.add("{")
                if len(isrParamsList) > 1:
                    kernelCfgC.add2("\tPCB\t\t*p_my_pcb = get_my_pcb();")

                # 割込みサービスルーチンを優先度順に呼び出す（stable sort）
                for index, params in enumerate(
                        sorted(isrParamsList, key=lambda p: p["isrpri"].val)):
                    if index > 0:
                        kernelCfgC.add()
                        #  ★2026-07-24 再訂正: `_kernel_` 接頭辞を付ける形へ戻した。
                        #
                        #  【前の版（83a14be）が誤っていた理由】
                        #  「sense_lock / unlock_cpu は Inline なので rename 対象外」
                        #  という根拠は **xtensa と arm_m にしか当てはまらない**。
                        #  riscv_gcc / arm_gcc / arm64_gcc の core_rename.def は
                        #  lock_cpu / unlock_cpu / sense_lock を rename しており、
                        #  kernel_cfg.c は kernel_int.h 末尾の unrename 連鎖の**後**に
                        #  置かれるため、これらの arch では Inline 実体が
                        #  `_kernel_sense_lock` 名で定義される。素名で出力すると
                        #  implicit declaration で落ちる（＝壊れる arch 集合を
                        #  入れ替えただけだった）。
                        #
                        #  【`_kernel_` が正しいことの根拠（★仕様書が条文で規定）】
                        #  doc/configurator.txt §4.7.1.2「割込みハンドラの生成」
                        #  1338-1339 / 1346-1347 行が、生成すべきコードとして
                        #      if (_kernel_sense_lock()) {
                        #          _kernel_unlock_cpu();
                        #      }
                        #  をそのまま掲げている。interrupt.trb:462-464 も同じ。
                        #  ⇒ 真の欠陥は「arm_m と xtensa の core_rename.def に
                        #     lock_cpu/unlock_cpu/sense_lock のエントリが欠けている
                        #     **非一貫性**」の側であり、本ポートは xtensa 側の
                        #     core_rename.def を補って解消した。arm_m は上流へ報告する。
                        kernelCfgC.add("\tif (_kernel_sense_lock()) {")
                        kernelCfgC.add("\t\t_kernel_force_unlock_spin(p_my_pcb);")
                        kernelCfgC.add("\t\t_kernel_unlock_cpu();")
                        kernelCfgC.add2("\t}")
                    kernelCfgC.add(f"\tLOG_ISR_ENTER({params['isrid']});")
                    kernelCfgC.add(f"\t((ISR)({params['isr']}))"
                                   f"((EXINF)({params['exinf']}));")
                    kernelCfgC.add(f"\tLOG_ISR_LEAVE({params['isrid']});")
                kernelCfgC.add2("}")
                isr_flag[intnoVal] = True

#
#  割込みサービスルーチンに関する一般的な情報の生成
#
class IsrObject(KernelObject):
    def __init__(self):
        super().__init__("isr", "isr")

    def prepare(self, key, params):
        # エラーチェックは実施済みなので，ここでの処理は不要
        pass

    def generateInib(self, key, params):
        # ENA_DYNISRされていない割込み番号のISRはキューに登録されない．
        # インライン連鎖から直接呼ばれるため，p_isr_queueはNULLでよい
        # （initialize_isrがNULLを見てenqueueを省く）．
        p_isr_queue = isrQueueHeader.get(params["intno"].val, "NULL")
        return (f"({params['isratr']}), (EXINF)({params['exinf']}), "
                f"({p_isr_queue}), "
                f"(ISR)({params['isr']}), ({params['isrpri']})")


IsrObject().generate()

#
#  割込みサービスルーチン生成順序テーブルの生成
#
#  ★dcre（interrupt.trb:338-346）は挿入順（.cfgの記述順）で生成し，
#  TNUM_SISRIDが0のときのガードも持たない．FMP3では2点を変える．
#
#  (1) isrid昇順にする．initialize_isrはこの順にenqueue_isrし，enqueue_isrは
#      「自分より真に大きいisrpriの直前」に挿入するので，キューの並びは
#      「isrid昇順を基底とするisrpriの安定ソート」になる．これはインライン
#      連鎖の呼出し順序（本ファイルのisrParamsListの構築とsorted()）と
#      完全に同じである．ENA_DYNISRを足したり外したりしても同じ.cfgの
#      呼出し順序が変わらないことを保証するために，こちらを合わせる．
#  (2) 静的ISRが0個のときはTOPPERS_EMPTY_LABELにする（[0]配列を作らない．
#      他の表と同じ流儀）．
#
if len(cfgData["CRE_ISR"]) > 0:
    kernelCfgC.add("const ID _kernel_isrorder_table[TNUM_SISRID] = { ")
    kernelCfgC.append("\t")
    for index, (_, params) in enumerate(sorted(cfgData["CRE_ISR"].items())):
        if index > 0:
            kernelCfgC.append(", ")
        kernelCfgC.append(f"{params['isrid']}")
    kernelCfgC.add()
    kernelCfgC.add2("};")
else:
    kernelCfgC.add2("TOPPERS_EMPTY_LABEL(const ID, _kernel_isrorder_table);")

#
#  割込みハンドラのための標準的な初期化情報の生成
#
if not OMIT_INITIALIZE_INTERRUPT or USE_INHINIB_TABLE:
    #
    #  定義する割込みハンドラの数
    #
    kernelCfgC.add(f"""\
#define TNUM_DEF_INHNO\t{len(cfgData['DEF_INH'])}
const uint_t _kernel_tnum_def_inhno = TNUM_DEF_INHNO;
""")

    if len(cfgData["DEF_INH"]) != 0:
        #
        #  割込みハンドラのエントリ
        #
        for _, params in cfgData["DEF_INH"].items():
            if (params["inhatr"] & TA_NONKERNEL) == 0:
                kernelCfgC.add(f"INTHDR_ENTRY({params['inhno']}, "
                               f"{params['inhno'].val}, {params['inthdr']})")
        kernelCfgC.add("")

        #
        #  割込みハンドラ初期化ブロック
        #
        kernelCfgC.add(
            f"const INHINIB _kernel_inhinib_table[TNUM_DEF_INHNO] = {{")
        for index, (_, params) in enumerate(cfgData["DEF_INH"].items()):
            if index > 0:
                kernelCfgC.add(",")
            if (params["inhatr"] & TA_NONKERNEL) == 0:
                inthdr = (f"(FP)(INT_ENTRY({params['inhno']}, "
                          f"{params['inthdr']}))")
            else:
                inthdr = f"(FP)({params['inthdr']})"
            kernelCfgC.append(
                f"\t{{ ({params['inhno']}), "
                f"({params['inhatr']}), "
                f"{inthdr}, "
                f"{clsData[params['class']]['initPrc']} }}")
        kernelCfgC.add()
        kernelCfgC.add2("};")
    else:
        kernelCfgC.add2(
            "TOPPERS_EMPTY_LABEL(const INHINIB, _kernel_inhinib_table);")

#
#  割込み要求ラインのための標準的な初期化情報の生成
#
if not OMIT_INITIALIZE_INTERRUPT or USE_INTINIB_TABLE:
    #
    #  設定する割込み要求ラインの数
    #
    kernelCfgC.add(f"""\
#define TNUM_CFG_INTNO\t{len(cfgData['CFG_INT'])}
const uint_t _kernel_tnum_cfg_intno = TNUM_CFG_INTNO;
""")

    #
    #  割込み要求ライン初期化ブロック
    #
    if len(cfgData["CFG_INT"]) != 0:
        kernelCfgC.add(
            f"const INTINIB _kernel_intinib_table[TNUM_CFG_INTNO] = {{")
        for index, (_, params) in enumerate(cfgData["CFG_INT"].items()):
            if index > 0:
                kernelCfgC.add(",")
            kernelCfgC.append(
                f"\t{{ ({params['intno']}), "
                f"({params['intatr']}), "
                f"({params['intpri']}), "
                f"{clsData[params['class']]['initPrc']}, "
                f"0x{params['affinityPrcBitmap']:x}U }}")
        kernelCfgC.add()
        kernelCfgC.add2("};")
    else:
        kernelCfgC.add2(
            "TOPPERS_EMPTY_LABEL(const INTINIB, _kernel_intinib_table);")

#
#  割込み管理機能初期化関数の追加
#
initializeFunctions.append("_kernel_initialize_interrupt(p_my_pcb);")
