# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 把 src/ 注册为额外组件目录（外设/控制代码）
set(EXTRA_COMPONENT_DIRS ${CMAKE_SOURCE_DIR}/src)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SX70_ModelZ)

# 编译后复制固件，文件名带时间戳，方便 OTA 时识别版本
string(TIMESTAMP BUILD_TIME "build%Y%m%d%H%M%S")
add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin"
        "${CMAKE_BINARY_DIR}/${PROJECT_NAME}_${BUILD_TIME}.bin"
    COMMENT "Timestamped firmware: ${PROJECT_NAME}_${BUILD_TIME}.bin"
)
