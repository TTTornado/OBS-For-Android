cmake_minimum_required(VERSION 3.28...3.30)

option(ENABLE_LIBFDK "Enable FDK AAC support" OFF)
if(NOT ENABLE_LIBFDK)
  return()
endif()

find_package(Libfdk REQUIRED)

add_library(obs-libfdk MODULE)
add_library(OBS::libfdk ALIAS obs-libfdk)

target_sources(obs-libfdk PRIVATE obs-libfdk.c)

target_link_libraries(obs-libfdk PRIVATE OBS::libobs Libfdk::Libfdk)

set_target_properties_obs(obs-libfdk PROPERTIES FOLDER plugins PREFIX "")

# Android：fdk-aac 是 C++ 实现、这里链的是静态库（libfdk-aac.a），而本目标只有 C 源
# 文件 —— CMake 按源语言会选 C 链接器，结果缺 C++ 运行时符号（operator new / __cxa_*）。
# 强制用 C++ 链接器，让它带上 libc++。
if(OS_ANDROID)
  set_target_properties(obs-libfdk PROPERTIES LINKER_LANGUAGE CXX)
endif()
