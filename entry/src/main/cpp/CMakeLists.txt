# 甜甜圈烤机 —— 原生 GPU 渲染层（EGL + OpenGL ES 3.0）
cmake_minimum_required(VERSION 3.5.0)
project(Bench3D)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(bench3d SHARED
    gl_core.cpp
    napi_bridge.cpp
)

target_include_directories(bench3d PRIVATE ${NATIVERENDER_ROOT_PATH})

# 这些 .so 都在 OHOS native sysroot 里，直接按 -l 名字链接即可
target_link_libraries(bench3d PUBLIC
    EGL
    GLESv3
    ace_ndk.z
    ace_napi.z
    hilog_ndk.z
    native_window
)
