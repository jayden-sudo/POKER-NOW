cmake_minimum_required(VERSION 3.16)

# 引用全專案共用元件(後續裝置照抄本行,只改相對深度)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../common/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(poker_sticks3)
