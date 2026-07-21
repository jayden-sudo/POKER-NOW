cmake_minimum_required(VERSION 3.16)

# 引用:① 全專案共用元件(poker_core) ② 本專案自帶 vb6824 元件(閉源 .a 音訊驅動;
#   2026-07-21 起 demo 已搬離本 repo,vb6824 複製進 poker/components 使專案自足)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../common/components"
    "${CMAKE_CURRENT_LIST_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# R13 已裁定修復:核心 pbus_session.c on_cmd() 的 body[8] → sizeof(pn_cmd_ack_t)+6,
# 原本此處的 -Wno-error=stringop-overflow 降級已移除(2026-07-21,主 agent)。

project(poker_zuowei_c3)
