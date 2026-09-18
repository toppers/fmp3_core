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
#  $Id: kernel_check.py (converted from kernel_check.trb by Claude Code Sonnet 4.6) $
#  

#
#		コンフィギュレータのパス3の生成スクリプト
#

#
#  非タスクコンテキスト用のスタック領域の生成を省略するかどうかの
#  デフォルト定義
#
if "OMIT_ISTK" not in globals():
    OMIT_ISTK = False

#
#  タイムスタンプファイルの指定
#
timeStampFileName = "check.timestamp"

#
#  データセクションのLMAからVMAへのコピー
#
if "lmaList" in globals():
    for lma in lmaList:
        startData = SYMBOL(lma["START_DATA"])
        endData = SYMBOL(lma["END_DATA"])
        startIdata = SYMBOL(lma["START_IDATA"])
        BCOPY(startIdata, startData, endData - startData)

#
#  通知情報のチェック関数
#
tmax_tskid = TMIN_TSKID + len(cfgData["CRE_TSK"]) - 1
tmax_semid = TMIN_SEMID + len(cfgData["CRE_SEM"]) - 1
tmax_flgid = TMIN_FLGID + len(cfgData["CRE_FLG"]) - 1
tmax_dtqid = TMIN_DTQID + len(cfgData["CRE_DTQ"]) - 1


def checkNotifyHandler(key, params, objid, exinf, nfyhdr):
    # パラメータを変数に格納
    nfymode = params["nfymode"]
    nfymode1 = nfymode & 0x0f
    nfymode2 = nfymode & ~0x0f
    par1 = params["par1"]

    # 通知処理のパラメータ数による補正処理
    if nfymode1 == TNFY_SETVAR or nfymode1 == TNFY_SETFLG \
            or nfymode1 == TNFY_SNDDTQ:
        # 通知処理のパラメータが2つの場合
        epar1 = params.get("par3")
    else:
        # 通知処理のパラメータが1つの場合
        epar1 = params.get("par2")

    # タイムイベントハンドラの先頭番地が，プログラムの先頭番地として正しく
    # ない場合（E_PAR）
    if nfymode == TNFY_HANDLER:
        tmehdr = nfyhdr
        params1 = params.copy()
        params1["tmehdr"] = params.get("par2")
        if (tmehdr & (CHECK_FUNC_ALIGN - 1)) != 0:
            error_sapi("E_PAR", params1, "%%tmehdr is not aligned", objid)
        if CHECK_FUNC_NONNULL and tmehdr == 0:
            error_sapi("E_PAR", params1, "%%tmehdr is null", objid)

    # イベント通知処理の変数の番地とオブジェクトIDのチェック
    if nfymode1 == TNFY_SETVAR or nfymode1 == TNFY_INCVAR:
        # 変数の設定／インクリメントによるタイムイベントの通知
        p_var = exinf
        params1 = params.copy()
        params1["p_var"] = par1

        # 通知方法中の変数の番地が，intptr_t型の変数の番地として正しくない
        # 場合（E_PAR）
        if (p_var & (CHECK_INTPTR_ALIGN - 1)) != 0:
            error_sapi("E_PAR", params1, "%%p_var is not aligned", objid)
        if CHECK_INTPTR_NONNULL and p_var == 0:
            error_sapi("E_PAR", params1, "%%p_var is null", objid)
    elif nfymode1 == TNFY_ACTTSK or nfymode1 == TNFY_WUPTSK:
        # タスクの起動／起床によるタイムイベントの通知
        tskid = exinf
        params1 = params.copy()
        params1["tskid"] = par1

        # 通知方法中のタスクIDが有効範囲外の場合（E_ID）
        if not (TMIN_TSKID <= tskid and tskid <= tmax_tskid):
            error_sapi("E_ID", params1, "illegal %%tskid", objid)
    elif nfymode1 == TNFY_SIGSEM:
        # セマフォの資源の返却によるタイムイベントの通知
        semid = exinf
        params1 = params.copy()
        params1["semid"] = par1

        # 通知方法中のセマフォIDが有効範囲外の場合（E_ID）
        if not (TMIN_SEMID <= semid and semid <= tmax_semid):
            error_sapi("E_ID", params1, "illegal %%semid", objid)
    elif nfymode1 == TNFY_SETFLG:
        # イベントフラグのセットによるタイムイベントの通知
        flgid = exinf
        params1 = params.copy()
        params1["flgid"] = par1

        # 通知方法中のイベントフラグIDが有効範囲外の場合（E_ID）
        if not (TMIN_FLGID <= flgid and flgid <= tmax_flgid):
            error_sapi("E_ID", params1, "illegal %%flgid", objid)
    elif nfymode1 == TNFY_SNDDTQ:
        # データキューへの送信によるタイムイベントの通知
        dtqid = exinf
        params1 = params.copy()
        params1["dtqid"] = par1

        # 通知方法中のデータキューIDが有効範囲外の場合（E_ID）
        if not (TMIN_DTQID <= dtqid and dtqid <= tmax_dtqid):
            error_sapi("E_ID", params1, "illegal %%dtqid", objid)

    # エラー通知処理の変数の番地とオブジェクトIDのチェック
    if nfymode2 == TENFY_SETVAR or nfymode2 == TENFY_INCVAR:
        # 変数の設定／インクリメントによるエラーの通知
        p_var = PEEK(SYMBOL(params["nfyhdr"] + "_p_evar"), sizeof_intptr_t)
        params1 = params.copy()
        params1["p_var"] = epar1

        # 通知方法中の変数の番地が，intptr_t型の変数の番地として正しくない
        # 場合（E_PAR）
        if (p_var & (CHECK_INTPTR_ALIGN - 1)) != 0:
            error_sapi("E_PAR", params1, "%%p_var is not aligned", objid)
        if CHECK_INTPTR_NONNULL and p_var == 0:
            error_sapi("E_PAR", params1, "%%p_var is null", objid)
    elif nfymode2 == TENFY_ACTTSK or nfymode2 == TENFY_WUPTSK:
        # タスクの起動／起床によるエラーの通知
        tskid = PEEK(SYMBOL(params["nfyhdr"] + "_etskid"), sizeof_ID)
        params1 = params.copy()
        params1["tskid"] = epar1

        # 通知方法中のタスクIDが有効範囲外の場合（E_ID）
        if not (TMIN_TSKID <= tskid and tskid <= tmax_tskid):
            error_sapi("E_ID", params1, "illegal %%tskid", objid)
    elif nfymode2 == TENFY_SIGSEM:
        # セマフォの資源の返却によるエラーの通知
        semid = PEEK(SYMBOL(params["nfyhdr"] + "_esemid"), sizeof_ID)
        params1 = params.copy()
        params1["semid"] = epar1

        # 通知方法中のセマフォIDが有効範囲外の場合（E_ID）
        if not (TMIN_SEMID <= semid and semid <= tmax_semid):
            error_sapi("E_ID", params1, "illegal %%semid", objid)
    elif nfymode2 == TENFY_SETFLG:
        # イベントフラグのセットによるエラーの通知
        flgid = PEEK(SYMBOL(params["nfyhdr"] + "_eflgid"), sizeof_ID)
        params1 = params.copy()
        params1["flgid"] = epar1

        # 通知方法中のイベントフラグIDが有効範囲外の場合（E_ID）
        if not (TMIN_FLGID <= flgid and flgid <= tmax_flgid):
            error_sapi("E_ID", params1, "illegal %%flgid", objid)
    elif nfymode2 == TENFY_SNDDTQ:
        # データキューへの送信によるエラーの通知
        dtqid = PEEK(SYMBOL(params["nfyhdr"] + "_edtqid"), sizeof_ID)
        params1 = params.copy()
        params1["dtqid"] = epar1

        # 通知方法中のデータキューIDが有効範囲外の場合（E_ID）
        if not (TMIN_DTQID <= dtqid and dtqid <= tmax_dtqid):
            error_sapi("E_ID", params1, "illegal %%dtqid", objid)


#
#  タスクに関するチェック
#
tinib = SYMBOL("_kernel_tinib_table")
for key, params in sorted(cfgData["CRE_TSK"].items()):
    # taskがプログラムの先頭番地として正しくない場合（E_PAR）
    task = PEEK(tinib + offsetof_TINIB_task, sizeof_TASK)
    if (task & (CHECK_FUNC_ALIGN - 1)) != 0:
        error_wrong_id("E_PAR", params, "task", "tskid", "not aligned")
    if CHECK_FUNC_NONNULL and task == 0:
        error_wrong_id("E_PAR", params, "task", "tskid", "null")

    # stkがターゲット定義の制約に合致しない場合（E_PAR）
    if USE_TSKINICTXB:
        stk = GetStackTskinictxb(key, params, tinib)
    else:
        stk = PEEK(tinib + offsetof_TINIB_stk, sizeof_void_ptr)
    if (stk & (CHECK_STACK_ALIGN - 1)) != 0:
        error_wrong_id("E_PAR", params, "stk", "tskid", "not aligned")
    if CHECK_STACK_NONNULL and stk == 0:
        error_wrong_id("E_PAR", params, "stk", "tskid", "null")

    tinib += sizeof_TINIB

#
#  固定長メモリプールに関するチェック
#
mpfinib = SYMBOL("_kernel_mpfinib_table")
for _, params in sorted(cfgData["CRE_MPF"].items()):
    mpf = PEEK(mpfinib + offsetof_MPFINIB_mpf, sizeof_void_ptr)

    # mpfがターゲット定義の制約に合致しない場合（E_PAR）
    if (mpf & (CHECK_MPF_ALIGN - 1)) != 0:
        error_wrong_id("E_PAR", params, "mpf", "mpfid", "not aligned")
    if CHECK_MPF_NONNULL and mpf == 0:
        error_wrong_id("E_PAR", params, "mpf", "mpfid", "null")

    mpfinib += sizeof_MPFINIB

#
#  周期通知に関するチェック
#
cycinib = SYMBOL("_kernel_cycinib_table")
for key, params in sorted(cfgData["CRE_CYC"].items()):
    exinf = PEEK(cycinib + offsetof_CYCINIB_exinf, sizeof_EXINF)
    nfyhdr = PEEK(cycinib + offsetof_CYCINIB_nfyhdr, sizeof_NFYHDR)

    # 通知情報のチェック
    checkNotifyHandler(key, params, "cycid", exinf, nfyhdr)

    cycinib += sizeof_CYCINIB

#
#  アラーム通知に関するチェック
#
alminib = SYMBOL("_kernel_alminib_table")
for key, params in sorted(cfgData["CRE_ALM"].items()):
    exinf = PEEK(alminib + offsetof_ALMINIB_exinf, sizeof_EXINF)
    nfyhdr = PEEK(alminib + offsetof_ALMINIB_nfyhdr, sizeof_NFYHDR)

    # 通知情報のチェック
    checkNotifyHandler(key, params, "almid", exinf, nfyhdr)

    alminib += sizeof_ALMINIB

#
#  非タスクコンテキスト用のスタック領域に関するチェック
#
if not OMIT_ISTACK and not OMIT_ISTK:
    istkTable = SYMBOL("_kernel_istk_table", True)
    if istkTable is not None:
        for _, params in cfgData["DEF_ICS"].items():
            prcid = clsData[params["class"]]["initPrc"]
            istk = PEEK(istkTable + (prcid - 1) * sizeof_void_ptr,
                        sizeof_void_ptr)

            # istkがターゲット定義の制約に合致しない場合（E_PAR）
            if (istk & (CHECK_STACK_ALIGN - 1)) != 0:
                error_wrong("E_PAR", params, "istk", "not aligned")
            if CHECK_STACK_NONNULL and istk == 0:
                error_wrong("E_PAR", params, "istk", "null")

#
#  カーネルメモリプール領域に関するチェック
#
if len(cfgData["DEF_MPK"]) > 0:
    params0 = cfgData["DEF_MPK"][1]
    mpk = PEEK(SYMBOL("_kernel_mpk"), sizeof_void_ptr)

    # mpkがターゲット定義の制約に合致しない場合（E_PAR）
    if (mpk & (CHECK_MPK_ALIGN - 1)) != 0:
        error_wrong("E_PAR", params0, "mpk", "not aligned")
    if CHECK_MPK_NONNULL and mpk == 0:
        error_wrong("E_PAR", params0, "mpk", "null")

#
#  初期化ルーチンに関するチェック
#
for prcid in range(0, TNUM_PRCID + 1):
    inirtnb = SYMBOL(f"_kernel_inirtnb_table_{prcStr[prcid]}", True)
    if inirtnb is not None:
        for _, params in iniData[prcid].items():
            inirtn = PEEK(inirtnb + offsetof_INIRTNB_inirtn, sizeof_INIRTN)

            # inirtnがプログラムの先頭番地として正しくない場合（E_PAR）
            if (inirtn & (CHECK_FUNC_ALIGN - 1)) != 0:
                error_wrong("E_PAR", params, "inirtn", "not aligned")
            if CHECK_FUNC_NONNULL and inirtn == 0:
                error_wrong("E_PAR", params, "inirtn", "null")

            inirtnb += sizeof_INIRTNB

#
#  終了処理ルーチンに関するチェック
#
for prcid in range(0, TNUM_PRCID + 1):
    terrtnb = SYMBOL(f"_kernel_terrtnb_table_{prcStr[prcid]}", True)
    if terrtnb is not None:
        for _, params in reversed(list(terData[prcid].items())):
            terrtn = PEEK(terrtnb + offsetof_TERRTNB_terrtn, sizeof_TERRTN)

            # terrtnがプログラムの先頭番地として正しくない場合（E_PAR）
            if (terrtn & (CHECK_FUNC_ALIGN - 1)) != 0:
                error_wrong("E_PAR", params, "terrtn", "not aligned")
            if CHECK_FUNC_NONNULL and terrtn == 0:
                error_wrong("E_PAR", params, "terrtn", "null")

            terrtnb += sizeof_TERRTNB
