cmake_minimum_required(VERSION 3.16)

# 引用全專案共用元件(poker_core);本板無自有 components/
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../common/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(poker_cardputer_adv)
