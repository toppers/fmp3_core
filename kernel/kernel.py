# -*- coding: utf-8 -*-
#
#   TOPPERS/FMP Kernel
#       Toyohashi Open Platform for Embedded Real-Time Systems/
#       Flexible MultiProcessor Kernel
# 
#   Copyright (C) 2015 by FUJI SOFT INCORPORATED, JAPAN
#   Copyright (C) 2015-2023 by Embedded and Real-Time Systems Laboratory
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
#  $Id: kernel.py (converted from kernel.trb by Claude Code Sonnet 4.6) $
# 

#
#		コンフィギュレータのパス2の生成スクリプト
#

#
#  内部で使用するID
#
TCLS_NONE = 0			# クラス共通

#
#  タイムスタンプファイルの指定
#
timeStampFileName = "kernel_cfg.timestamp"

#
#  kernel_cfg.hの先頭部分の生成
#
kernelCfgH = GenFile("kernel_cfg.h")
kernelCfgH.add("""\
/* kernel_cfg.h */
#ifndef TOPPERS_KERNEL_CFG_H
#define TOPPERS_KERNEL_CFG_H
""")

#
#  kernel_cfg.cの先頭部分の生成
#
kernelCfgC = GenFile("kernel_cfg.c")
kernelCfgC.add("""\
/* kernel_cfg.c */
#include "kernel/kernel_int.h"
#include "kernel_cfg.h"

#if !(TKERNEL_PRID == 0x0008U && (TKERNEL_PRVER & 0xf000U) == 0x3000U)
#error The kernel does not match this configuration file.
#endif
""")

#
#  インクルードディレクティブ（#include）の生成
#
kernelCfgC.comment_header("Include Directives")
GenerateIncludes(kernelCfgC)
kernelCfgC.add()

#
#  スタック領域の確保
#
if "AllocStack" not in globals():
    def AllocStack(stack, size, secname):
        DefineVariableSection(kernelCfgC,
            f"static STK_T {stack}[COUNT_STK_T({size})]", secname)
        return f"ROUND_STK_T({size})"

#
#  固定長メモリプール領域の確保
#
if "AllocUserMempfix" not in globals():
    def AllocUserMempfix(mpf, blkcnt, blksz, secname):
        DefineVariableSection(kernelCfgC,
            f"static MPF_T {mpf}[({blkcnt}) * COUNT_MPF_T({blksz})]", secname)


#
#  カーネルオブジェクトに関する情報の生成（仮想クラス）
#
class KernelObject:
    def __init__(self, obj, object_name, obj_s=None):
        if obj_s is None:
            obj_s = obj
        self.obj = obj
        self.OBJ = obj.upper()
        self.object_name = object_name
        self.obj_s = obj_s
        self.OBJ_S = obj_s.upper()
        self.objid = obj + "id"
        self.api = "CRE_" + obj.upper()
        self.omit_cb = False
        self.noobj = "no" + obj
        self.aidapi = "AID_" + obj.upper()
        self.inibList = {f"{self.OBJ_S}INIB": f"a{self.obj_s}inib_table"}

    def checkAutoObjid(self, numAutoObjid):
        """AID_xxx が 1 個以上ある構成に対するオブジェクト種別ごとの追加検査．

        既定では何もしない．cyc/alm だけが「マスタプロセッサが時間イベント
        処理プロセッサであること」を要求するためオーバライドする
        （段階2 最終レビュー Minor 1 の hardening）．
        """
        pass

    def generate(self):
        # AID_xxx の処理（クラス外専用。ENA_SPRと同じ規約: kernel/task.py:141-142）
        #
        # 段階1では kernel_api.def に AID_TSK のみが登録されている（AID_SEM等
        # は未登録）。cfgDataはkernel_api.def登録済みAPIのみをキーに持つため、
        # self.aidapiが登録されていないオブジェクト（tsk以外）ではこのブロック
        # 全体を無効化し、既存の出力（TNUM_S*ID等の新設なし）を厳密に保つ
        # （段階3で他オブジェクトにもAID_xxxを登録すれば自動的に有効化される）。
        has_aid = self.aidapi in cfgData
        numAutoObjid = 0
        if has_aid:
            for _, params in cfgData[self.aidapi].items():
                if "class" in params:
                    error_ercd("E_RSATR", params,
                               f"{self.aidapi} must not be within a class")
                numAutoObjid += int(params[self.noobj])
        numObjid = len(cfgData[self.api]) + numAutoObjid

        # AID_xxx が 1 個以上あるのに静的オブジェクトが 0 個の構成は、
        # データ構造生成ガード（下記 len(cfgData[self.api]) > 0）により
        # CB 実体もアクセステーブルも初期化関数登録も生成されないまま
        # TNUM_xxxID だけが増える（free-list 未初期化のまま acre される）。
        # 段階2 ではこれを cfg エラーとして弾く（dcre も同じ穴を持つ）。
        if numAutoObjid > 0 and len(cfgData[self.api]) == 0:
            for _, params in cfgData[self.aidapi].items():
                error_ercd("E_OBJ", params,
                           f"{self.aidapi} requires at least one "
                           f"{self.api} in the system")

        # オブジェクト種別ごとの追加検査（既定は no-op）
        if numAutoObjid > 0:
            self.checkAutoObjid(numAutoObjid)

        # オブジェクトの数のマクロ定義の生成（kernel_cfg.h）
        kernelCfgH.add(f"#define TNUM_{self.OBJ}ID\t"
                       f"{numObjid}")

        # オブジェクトのID番号のマクロ定義の生成（kernel_cfg.h）
        for _, params in sorted(cfgData[self.api].items()):
            kernelCfgH.add(f"#define {params[self.objid]}\t"
                           f"{params[self.objid].val}")
        kernelCfgH.add()

        # オブジェクトのID番号を保持する変数
        if USE_EXTERNAL_ID:
            for _, params in sorted(cfgData[self.api].items()):
                kernelCfgC.add(f"const ID {params[self.objid]}_id"
                               f" = {params[self.objid].val};")
            kernelCfgC.add()

        # オブジェクトID番号の最大値
        if has_aid:
            kernelCfgC.add(f"const ID _kernel_tmax_{self.obj}id"
                           f" = (TMIN_{self.OBJ}ID + TNUM_{self.OBJ}ID - 1);")
            kernelCfgC.add(f"#define TNUM_S{self.OBJ}ID\t"
                           f"{len(cfgData[self.api])}")
            kernelCfgC.add2(f"const ID _kernel_tmax_s{self.obj}id"
                            f" = (TMIN_{self.OBJ}ID + TNUM_S{self.OBJ}ID - 1);")
        else:
            kernelCfgC.add2(f"const ID _kernel_tmax_{self.obj}id"
                            f" = (TMIN_{self.OBJ}ID + TNUM_{self.OBJ}ID - 1);")

        # inib_table のサイズトークン（AID登録済みなら静的数のみのサイズへ）
        inibSizeToken = f"TNUM_S{self.OBJ}ID" if has_aid else f"TNUM_{self.OBJ}ID"

        # データ構造
        if len(cfgData[self.api]) > 0:
            # 事前準備（エラーチェック，メモリ領域の生成）
            for key, params in sorted(cfgData[self.api].items()):
                # クラスのチェック
                if "class" not in params:
                    error_ercd("E_RSATR", params,
                               f"{self.api} of %{self.objid} "
                               "must be within a class")
                    params["class"] = TCLS_ERROR

                # カーネルオブジェクト毎の事前準備
                self.prepare(key, params)

            # オブジェクト全体に対して必要なメモリ領域の生成（オプション）
            if hasattr(self, "generateData"):
                self.generateData()

            # オブジェクト初期化ブロックの生成
            kernelCfgC.add(f"const {self.OBJ_S}INIB _kernel_{self.obj_s}inib_table"
                           f"[{inibSizeToken}] = {{")
            for index, (key, params) in enumerate(
                    sorted(cfgData[self.api].items())):
                if index > 0:
                    kernelCfgC.add(",")
                kernelCfgC.append(f"\t{{ {self.generateInib(key, params)} }}")
            kernelCfgC.add()
            kernelCfgC.add2("};")

            if not self.omit_cb:
                # オブジェクト管理ブロックの生成
                for _, params in sorted(cfgData[self.api].items()):
                    DefineVariableSection(kernelCfgC,
                        f"static {self.OBJ_S}CB "
                        f"_kernel_{self.obj_s}cb_{params[self.objid]}",
                        SecnameKernelData(params["class"]))
                for i in range(1, numAutoObjid + 1):
                    kernelCfgC.add(f"static {self.OBJ_S}CB "
                                   f"_kernel_a{self.obj_s}cb_{i};")
                kernelCfgC.add()

                # オブジェクト管理ブロックへのアクセステーブル
                kernelCfgC.add(f"{self.OBJ_S}CB\t*const "
                               f"_kernel_p_{self.obj_s}cb_table"
                               f"[TNUM_{self.OBJ}ID] = {{")
                for index, (_, params) in enumerate(
                        sorted(cfgData[self.api].items())):
                    if index > 0:
                        kernelCfgC.add(",")
                    kernelCfgC.append(
                        f"\t&_kernel_{self.obj_s}cb_{params[self.objid]}")
                for i in range(1, numAutoObjid + 1):
                    kernelCfgC.add(",")
                    kernelCfgC.append(f"\t&_kernel_a{self.obj_s}cb_{i}")
                kernelCfgC.add()
                kernelCfgC.add2("};")

            # オブジェクト初期化関数の追加
            initializeFunctions.append(
                f"_kernel_initialize_{self.object_name}(p_my_pcb);")
        else:
            # オブジェクトが1つもない場合
            kernelCfgC.add(f"TOPPERS_EMPTY_LABEL(const {self.OBJ_S}INIB, "
                           f"_kernel_{self.obj_s}inib_table);")
            if not self.omit_cb:
                kernelCfgC.add2(
                    f"TOPPERS_EMPTY_LABEL({self.OBJ_S}CB *const, "
                    f"_kernel_p_{self.obj_s}cb_table);")

        # 動的生成オブジェクト用の初期化ブロック（RAM・非const）。
        # self.aidapiがkernel_api.defに登録されているオブジェクトのみ出力する
        # （段階1ではAID_TSKのみ登録済み。他オブジェクトは既存出力のまま）。
        if has_aid:
            for typ, array in self.inibList.items():
                if numAutoObjid > 0:
                    kernelCfgC.add2(f"{typ} _kernel_{array}[{numAutoObjid}];")
                else:
                    kernelCfgC.add2(
                        f"TOPPERS_EMPTY_LABEL({typ}, _kernel_{array});")


#
#  通知ハンドラの生成関数
#
def generateNotifyHandler(key, params, objid):
    # パラメータを変数に格納
    nfymode = params["nfymode"]
    nfymode1 = nfymode & 0x0f
    nfymode2 = nfymode & ~0x0f
    par2 = params.get("par2")

    # 通知処理のパラメータ数による補正処理
    if nfymode == TNFY_HANDLER or nfymode1 == TNFY_SETVAR \
            or nfymode1 == TNFY_SETFLG \
            or nfymode1 == TNFY_SNDDTQ:
        # 通知処理のパラメータが2つの場合
        numpar = 2
        epar1 = params.get("par3")
        epar2 = params.get("par4")
    else:
        # 通知処理のパラメータが1つの場合
        numpar = 1
        epar1 = params.get("par2")
        epar2 = params.get("par3")

    # パラメータ数のチェック
    if (numpar == 2 and par2 is None) \
            or (nfymode2 != 0 and epar1 is None) \
            or (nfymode2 == TENFY_SETFLG and epar2 is None):
        # パラメータが足りない場合
        error_sapi(None, params,
                   f"too few parameters for nfymode `{nfymode}'", objid)
    elif (nfymode2 == 0 and epar1 is not None) \
            or (nfymode2 != TENFY_SETFLG and epar2 is not None):
        # パラメータが多すぎる場合
        error_sapi(None, params,
                   f"too many parameters for nfymode `{nfymode}'", objid)
    elif nfymode1 == TNFY_HANDLER and nfymode2 == 0:
        # タイムイベントハンドラの呼出し（通知ハンドラ名を上書き）
        params["nfyhdr"] = f"(NFYHDR)({par2})"
    else:
        # エラー通知のための変数のアドレスとオブジェクトIDを格納する
        # 変数の生成（エラーチェックのために必要）
        if nfymode2 == TENFY_SETVAR or nfymode2 == TENFY_INCVAR:
            kernelCfgC.add2(f"intptr_t *const {params['nfyhdr']}_p_evar ="
                            f" (intptr_t *)({epar1});")
        elif nfymode2 == TENFY_ACTTSK or nfymode2 == TENFY_WUPTSK:
            kernelCfgC.add2(f"const ID {params['nfyhdr']}_etskid = {epar1};")
        elif nfymode2 == TENFY_SIGSEM:
            kernelCfgC.add2(f"const ID {params['nfyhdr']}_esemid = {epar1};")
        elif nfymode2 == TENFY_SETFLG:
            kernelCfgC.add2(f"const ID {params['nfyhdr']}_eflgid = {epar1};")
        elif nfymode2 == TENFY_SNDDTQ:
            kernelCfgC.add2(f"const ID {params['nfyhdr']}_edtqid = {epar1};")

        # 関数の先頭部分の生成
        kernelCfgC.add("static void")
        kernelCfgC.add(f"{params['nfyhdr']}(EXINF exinf)")
        kernelCfgC.add("{")

        if nfymode2 == 0:
            # エラー通知がない場合
            errorCode = "(void) "
        else:
            # エラー通知がある場合
            kernelCfgC.add2("\tER\tercd;")
            errorCode = "ercd = "

        # イベント通知処理の処理
        if nfymode1 == TNFY_SETVAR and nfymode2 == 0:
            # 変数の設定
            kernelCfgC.add(f"\t*((intptr_t *) exinf) = ({par2});")
        elif nfymode1 == TNFY_INCVAR and nfymode2 == 0:
            # 変数のインクリメント
            kernelCfgC.add("\t(void) loc_cpu();")
            kernelCfgC.add("\t*((intptr_t *) exinf) += 1;")
            kernelCfgC.add("\t(void) unl_cpu();")
        elif nfymode1 == TNFY_ACTTSK:
            # タスクの起動
            kernelCfgC.add(f"\t{errorCode}act_tsk((ID) exinf);")
        elif nfymode1 == TNFY_WUPTSK:
            # タスクの起床
            kernelCfgC.add(f"\t{errorCode}wup_tsk((ID) exinf);")
        elif nfymode1 == TNFY_SIGSEM:
            # セマフォの返却
            kernelCfgC.add(f"\t{errorCode}sig_sem((ID) exinf);")
        elif nfymode1 == TNFY_SETFLG:
            # イベントフラグのセット
            kernelCfgC.add(f"\t{errorCode}set_flg(((ID) exinf), {par2});")
        elif nfymode1 == TNFY_SNDDTQ:
            # データキューへの送信
            kernelCfgC.add(f"\t{errorCode}psnd_dtq(((ID) exinf), {par2});")
        else:
            # nfymodeの値が正しくない場合（E_PAR）
            error_sapi("E_PAR", params, "illegal %%nfymode", objid)

        if nfymode2 != 0:
            # エラー通知処理の処理
            kernelCfgC.add("\tif (ercd != E_OK) {")

            if nfymode2 == TENFY_SETVAR:
                # 変数の設定
                kernelCfgC.add(
                    f"\t\t*{params['nfyhdr']}_p_evar = (intptr_t) ercd;")
            elif nfymode2 == TENFY_INCVAR:
                # 変数のインクリメント
                kernelCfgC.add("\t\t(void) loc_cpu();")
                kernelCfgC.add(f"\t\t*{params['nfyhdr']}_p_evar += 1;")
                kernelCfgC.add("\t\t(void) unl_cpu();")
            elif nfymode2 == TENFY_ACTTSK:
                # タスクの起動
                kernelCfgC.add(
                    f"\t\t(void) act_tsk({params['nfyhdr']}_etskid);")
            elif nfymode2 == TENFY_WUPTSK:
                # タスクの起床
                kernelCfgC.add(
                    f"\t\t(void) wup_tsk({params['nfyhdr']}_etskid);")
            elif nfymode2 == TENFY_SIGSEM:
                # セマフォの返却
                kernelCfgC.add(
                    f"\t\t(void) sig_sem({params['nfyhdr']}_esemid);")
            elif nfymode2 == TENFY_SETFLG:
                # イベントフラグのセット
                kernelCfgC.add(
                    f"\t\t(void) set_flg({params['nfyhdr']}_eflgid, "
                    f"{epar2});")
            elif nfymode2 == TENFY_SNDDTQ:
                # データキューへの送信
                kernelCfgC.add(
                    f"\t\t(void) psnd_dtq({params['nfyhdr']}_edtqid,"
                    f" (intptr_t) ercd);")
            else:
                # nfymodeの値が正しくない場合（E_PAR）
                error_sapi("E_PAR", params, "illegal %%nfymode", objid)
            kernelCfgC.add("\t}")

        # 関数の末尾部分の生成
        kernelCfgC.add2("}")


#
#  プロセッサ毎のデータ構造を置くセクションを決定するクラスの決定
#
prcClass = {}
for prcid in range(1, TNUM_PRCID + 1):
    # クラスのリストをサーチして，初期割付けプロセッサがprcidに一致する
    # 最初のクラスを用いる．
    cls = next(((k, v) for k, v in clsData.items()
                if prcid == v["initPrc"]), None)
    if cls is None:
        # 初期割付けプロセッサがprcidに一致するクラスがない時は，クラス共
        # 通の標準メモリリージョンを用いる．
        prcClass[prcid] = TCLS_NONE
    else:
        prcClass[prcid] = cls[0]

#
#  カーネル動作状態フラグとプロセッサ管理ブロック（PCB）
#
kernelCfgC.comment_header("Processor Management Functions")

kernelCfgC.add2("bool_t\t_kernel_kerflg_table[TNUM_PRCID];")

for prcid in range(1, TNUM_PRCID + 1):
    DefineVariableSection(kernelCfgC,
        f"static PCB _kernel_pcb_prc{prcid}",
        SecnameKernelData(prcClass[prcid]))
kernelCfgC.add()

kernelCfgC.add("PCB\t*const _kernel_p_pcb_table[TNUM_PRCID] = {")
for prcid in range(1, TNUM_PRCID + 1):
    if prcid > 1:
        kernelCfgC.add(",")
    kernelCfgC.append(f"\t&_kernel_pcb_prc{prcid}")
kernelCfgC.add()
kernelCfgC.add2("};")

#
#  各機能モジュールのコンフィギュレーション
#
initializeFunctions = []
IncludeTrb("kernel/task.py")
IncludeTrb("kernel/semaphore.py")
IncludeTrb("kernel/eventflag.py")
IncludeTrb("kernel/dataqueue.py")
IncludeTrb("kernel/pridataq.py")
IncludeTrb("kernel/mutex.py")
IncludeTrb("kernel/spin_lock.py")
IncludeTrb("kernel/mempfix.py")
IncludeTrb("kernel/cyclic.py")
IncludeTrb("kernel/alarm.py")
IncludeTrb("kernel/interrupt.py")
IncludeTrb("kernel/exception.py")

#
#  非タスクコンテキスト用のスタック領域
#
if not OMIT_ISTACK:
    kernelCfgC.comment_header("Stack Area for Non-task Context")

    icsData = {}
    for _, params in cfgData["DEF_ICS"].items():
        # パラメータが省略された時のデフォルト値の設定
        params.setdefault("istk", "NULL")

        # クラスの囲みの中に記述されていない場合（E_RSATR）
        if "class" not in params:
            error_ercd("E_RSATR", params, "%apiname must be within a class")
            params["class"] = TCLS_ERROR

        # istkszがターゲット定義の最小値よりも小さい場合（E_PAR）
        if params["istksz"] < TARGET_MIN_ISTKSZ:
            error_wrong("E_PAR", params, "istksz", "too small")

        # istkszがターゲット定義の制約に合致しない場合（E_PAR）
        if params["istk"] != "NULL" \
                and (params["istksz"] & (CHECK_STKSZ_ALIGN - 1)) != 0:
            error_wrong("E_PAR", params, "istksz", "not aligned")

        # プロセッサに対して非タスクコンテキスト用スタック領域が設定されて
        # いる場合（E_OBJ）
        prcid = clsData[params["class"]]["initPrc"]
        if prcid in icsData:
            error_ercd("E_OBJ", params,
                       f"%apiname is duplicated for processor {prcid}")

        icsData[prcid] = params.copy()

    for prcid in range(1, TNUM_PRCID + 1):
        if prcid not in icsData:
            # DEF_ICSがない場合
            params = {}
            params["istksz"] = AllocStack(
                f"_kernel_istack_prc{prcid}",
                "DEFAULT_ISTKSZ", SecnameStack(prcClass[prcid]))
            params["istk"] = f"_kernel_istack_prc{prcid}"
            icsData[prcid] = params
        else:
            params = icsData[prcid]
            if params["istk"] == "NULL":
                # スタック領域の自動割付け
                stkSecname = SecnameStack(params["class"])
                params["istksz"] = AllocStack(
                    f"_kernel_istack_prc{prcid}",
                    params["istksz"], stkSecname)
                params["istk"] = f"_kernel_istack_prc{prcid}"
            else:
                params["istksz"] = f"({params['istksz']})"
                params["istk"] = f"(void *)({params['istk']})"
    kernelCfgC.add()

    kernelCfgC.add("const size_t _kernel_istksz_table[TNUM_PRCID] = {")
    for index, prcid in enumerate(range(1, TNUM_PRCID + 1)):
        if index > 0:
            kernelCfgC.add(",")
        kernelCfgC.append(f"\t{icsData[prcid]['istksz']}")
    kernelCfgC.add()
    kernelCfgC.add2("};")

    if not OMIT_ISTK:
        kernelCfgC.add("STK_T *const _kernel_istk_table[TNUM_PRCID] = {")
        for index, prcid in enumerate(range(1, TNUM_PRCID + 1)):
            if index > 0:
                kernelCfgC.add(",")
            kernelCfgC.append(f"\t{icsData[prcid]['istk']}")
        kernelCfgC.add()
        kernelCfgC.add2("};")

    kernelCfgC.add("#ifdef TOPPERS_ISTKPT")
    kernelCfgC.add("STK_T *const _kernel_istkpt_table[TNUM_PRCID] = {")
    for index, prcid in enumerate(range(1, TNUM_PRCID + 1)):
        if index > 0:
            kernelCfgC.add(",")
        kernelCfgC.append(
            f"\tTOPPERS_ISTKPT({icsData[prcid]['istk']}, "
            f"{icsData[prcid]['istksz']})")
    kernelCfgC.add()
    kernelCfgC.add("};")
    kernelCfgC.add2("#endif /* TOPPERS_ISTKPT */")

#
#  アイドルスタック
#
if not OMIT_IDSTACK:
    kernelCfgC.comment_header("Stack Area for Idle")

    idstksz = ""
    for prcid in range(1, TNUM_PRCID + 1):
        idstksz = AllocStack(
            f"_kernel_idstack_prc{prcid}",
            "DEFAULT_IDSTKSZ", SecnameStack(prcClass[prcid]))
    kernelCfgC.add()

    kernelCfgC.add("#ifndef TOPPERS_ISTKPT")
    kernelCfgC.add("STK_T *const _kernel_idstk_table[TNUM_PRCID] = {")
    for index, prcid in enumerate(range(1, TNUM_PRCID + 1)):
        if index > 0:
            kernelCfgC.add(",")
        kernelCfgC.append(f"\t_kernel_idstack_prc{prcid}")
    kernelCfgC.add()
    kernelCfgC.add("};")
    kernelCfgC.add2("#endif /* TOPPERS_ISTKPT */")

    kernelCfgC.add("#ifdef TOPPERS_ISTKPT")
    kernelCfgC.add("STK_T *const _kernel_idstkpt_table[TNUM_PRCID] = {")
    for index, prcid in enumerate(range(1, TNUM_PRCID + 1)):
        if index > 0:
            kernelCfgC.add(",")
        kernelCfgC.append(
            f"\tTOPPERS_ISTKPT(_kernel_idstack_prc{prcid}, {idstksz})")
    kernelCfgC.add()
    kernelCfgC.add("};")
    kernelCfgC.add2("#endif /* TOPPERS_ISTKPT */")

#
#  タイムイベント管理
#
kernelCfgC.comment_header("Time Event Management")

# タイムイベントヒープ領域の生成
for prcid in range(1, TNUM_PRCID + 1):
    if TOPPERS_TEPP_PRC & (1 << (prcid - 1)) != 0:
        DefineVariableSection(kernelCfgC,
            f"static TMEVTN\t_kernel_tmevt_heap_prc{prcid}"
            f"[1 + TNUM_TSKID + TNUM_CYCID + TNUM_ALMID]",
            SecnameKernelData(prcClass[prcid]))
kernelCfgC.add()

kernelCfgC.add(
    "TMEVTN\t*const _kernel_p_tmevt_heap_table[TNUM_PRCID] = {")
for index, prcid in enumerate(range(1, TNUM_PRCID + 1)):
    if index > 0:
        kernelCfgC.add(",")
    if TOPPERS_TEPP_PRC & (1 << (prcid - 1)) != 0:
        kernelCfgC.append(f"\t_kernel_tmevt_heap_prc{prcid}")
    else:
        kernelCfgC.append("\tNULL")
kernelCfgC.add()
kernelCfgC.add2("};")

for prcid in range(1, TNUM_PRCID + 1):
    if TOPPERS_TEPP_PRC & (1 << (prcid - 1)) != 0:
        kernelCfgC.add(f"TEVTCB _kernel_tevtcb_prc{prcid};")
kernelCfgC.add()

kernelCfgC.add(
    "TEVTCB\t*const _kernel_p_tevtcb_table[TNUM_PRCID] = {")
for index, prcid in enumerate(range(1, TNUM_PRCID + 1)):
    if index > 0:
        kernelCfgC.add(",")
    if TOPPERS_TEPP_PRC & (1 << (prcid - 1)) != 0:
        kernelCfgC.append(f"\t&_kernel_tevtcb_prc{prcid}")
    else:
        kernelCfgC.append("\tNULL")
kernelCfgC.add()
kernelCfgC.add2("};")

#
#  各モジュールの初期化関数
#
kernelCfgC.comment_header("Module Initialization Function")
kernelCfgC.append("""\
void
_kernel_initialize_object(PCB *p_my_pcb)
{
""")
for func in initializeFunctions:
    kernelCfgC.add(f"\t{func}")
kernelCfgC.add2("}")

#
#  初期化ルーチンと終了処理ルーチン機能の準備
#
globalVars.append("prcStr")
prcStr = {0: "global"}
for prcid in range(1, TNUM_PRCID + 1):
    prcStr[prcid] = f"prc{prcid}"

#
#  初期化ルーチン機能
#

# kernel_cfg.hの生成
kernelCfgH.add2(f"#define TNUM_INIRTN\t{len(cfgData['ATT_INI'])}")

# kernel_cfg.cの生成
kernelCfgC.comment_header("Initialization Routine")

# エラーチェック
for _, params in cfgData["ATT_INI"].items():
    # クラスに属していて，属するクラスの指定が有効範囲外の場合（E_RSATR）
    if "class" in params and params["class"] not in clsData:
        error_illegal("E_RSATR", params, "class")
        params["class"] = TCLS_ERROR

    # iniatrが無効の場合（E_RSATR）
    if params["iniatr"] != TA_NULL:
        error_illegal_sym("E_RSATR", params, "iniatr", "inirtn")

# 初期化ルーチンの情報の整理
globalVars.append("iniData")
iniData = {}
for prcid in range(0, TNUM_PRCID + 1):
    iniData[prcid] = {}
for key, params in cfgData["ATT_INI"].items():
    if "class" not in params:
        iniData[0][key] = params
    else:
        iniData[clsData[params["class"]]["initPrc"]][key] = params

# 初期化ルーチンテーブルの生成
for prcid in range(0, TNUM_PRCID + 1):
    if len(iniData[prcid]) > 0:
        kernelCfgC.add(
            f"const INIRTNB _kernel_inirtnb_table_{prcStr[prcid]}"
            f"[{len(iniData[prcid])}] = {{")
        for index, (_, params) in enumerate(iniData[prcid].items()):
            if index > 0:
                kernelCfgC.add(",")
            kernelCfgC.append(
                f"\t{{ (INIRTN)({params['inirtn']}), "
                f"(EXINF)({params['exinf']}) }}")
        kernelCfgC.add()
        kernelCfgC.add2("};")

if len(cfgData["ATT_INI"]) > 0:
    kernelCfgC.add(
        "const INIRTNBB _kernel_inirtnbb_table[TNUM_PRCID + 1] = {")
    for prcid in range(0, TNUM_PRCID + 1):
        if prcid > 0:
            kernelCfgC.add(",")
        if len(iniData[prcid]) > 0:
            kernelCfgC.append(
                f"\t{{ {len(iniData[prcid])}, "
                f"_kernel_inirtnb_table_{prcStr[prcid]} }}")
        else:
            kernelCfgC.append("\t{ 0, NULL }")
    kernelCfgC.add()
    kernelCfgC.add2("};")
else:
    kernelCfgC.add2(
        "TOPPERS_EMPTY_LABEL(const INIRTNBB, _kernel_inirtnbb_table);")

#
#  終了処理ルーチン機能
#

# kernel_cfg.hの生成
kernelCfgH.add2(f"#define TNUM_TERRTN\t{len(cfgData['ATT_TER'])}")

# kernel_cfg.cの生成
kernelCfgC.comment_header("Termination Routine")

# エラーチェック
for _, params in cfgData["ATT_TER"].items():
    # teratrが無効の場合（E_RSATR）
    if params["teratr"] != TA_NULL:
        error_illegal_sym("E_RSATR", params, "teratr", "terrtn")

# 終了処理ルーチンの情報の整理
globalVars.append("terData")
terData = {}
for prcid in range(0, TNUM_PRCID + 1):
    terData[prcid] = {}
for key, params in cfgData["ATT_TER"].items():
    if "class" not in params:
        terData[0][key] = params
    else:
        terData[clsData[params["class"]]["initPrc"]][key] = params

# 終了処理ルーチンテーブルの生成（逆順）
for prcid in range(0, TNUM_PRCID + 1):
    if len(terData[prcid]) > 0:
        kernelCfgC.add(
            f"const TERRTNB _kernel_terrtnb_table_{prcStr[prcid]}"
            f"[{len(terData[prcid])}] = {{")
        items_reversed = list(reversed(list(terData[prcid].items())))
        for index, (_, params) in enumerate(items_reversed):
            if index > 0:
                kernelCfgC.add(",")
            kernelCfgC.append(
                f"\t{{ (TERRTN)({params['terrtn']}), "
                f"(EXINF)({params['exinf']}) }}")
        kernelCfgC.add()
        kernelCfgC.add2("};")

if len(cfgData["ATT_TER"]) > 0:
    kernelCfgC.add(
        "const TERRTNBB _kernel_terrtnbb_table[TNUM_PRCID + 1] = {")
    for prcid in range(0, TNUM_PRCID + 1):
        if prcid > 0:
            kernelCfgC.add(",")
        if len(terData[prcid]) > 0:
            kernelCfgC.append(
                f"\t{{ {len(terData[prcid])}, "
                f"_kernel_terrtnb_table_{prcStr[prcid]} }}")
        else:
            kernelCfgC.append("\t{ 0, NULL }")
    kernelCfgC.add()
    kernelCfgC.add2("};")
else:
    kernelCfgC.add2(
        "TOPPERS_EMPTY_LABEL(const TERRTNBB, _kernel_terrtnbb_table);")

#
#  kernel_cfg.hの末尾部分の生成
#
kernelCfgH.append("""\
#endif /* TOPPERS_KERNEL_CFG_H */
""")

#
#  カーネルメモリプール領域
#
kernelCfgC.comment_header("Kernel Memory Pool Area")

if len(cfgData["DEF_MPK"]) == 0:
    mpksz = "0"
    mpk = "NULL"
else:
    if len(cfgData["DEF_MPK"]) > 1:
        error("E_OBJ: too many DEF_MPK")
    params0 = cfgData["DEF_MPK"][1]
    if "class" in params0:
        error_ercd("E_RSATR", params0, "DEF_MPK must not be within a class")
    params0.setdefault("mpk", "NULL")
    if params0["mpksz"] == 0:
        error_wrong("E_PAR", params0, "mpksz", "zero")
    if params0["mpk"] == "NULL":
        kernelCfgC.add("static MB_T _kernel_memory_pool"
                       f"[COUNT_MB_T({params0['mpksz']})];")
        mpksz = f"ROUND_MB_T({params0['mpksz']})"
        mpk = "_kernel_memory_pool"
    else:
        if (params0["mpksz"] & (CHECK_MB_ALIGN - 1)) != 0:
            error_wrong("E_PAR", params0, "mpksz", "not aligned")
        mpksz = f"({params0['mpksz']})"
        mpk = f"(void *)({params0['mpk']})"

kernelCfgC.add(f"const size_t _kernel_mpksz = {mpksz};")
kernelCfgC.add2(f"MB_T *const _kernel_mpk = {mpk};")
