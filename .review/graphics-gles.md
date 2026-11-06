# libobs-opengl / OpenGL ES 图形后端 代码审查报告

审查员：`graphics-gles`　｜　只读审查，未修改任何源码文件
上游基线：`65c320b`（33.0.1）　｜　对照命令：`git diff upstream/master...HEAD -- <path>`、`git show 65c320b:<path>`
工作区：`D:\Agent工作区\OBS-For-Android`

---

# 范围

**主范围**（15 个文件，`+4239/-16`）：

| 路径 | 增删 |
|---|---|
| `libobs-opengl/CMakeLists.txt` | +11/-1 |
| `libobs-opengl/gl-android.c`（新增） | +559/0 |
| `libobs-opengl/gl-helpers.c/.h` | +57/-2、+43/-1 |
| `libobs-opengl/gl-shaderparser.c` | +34/0 |
| `libobs-opengl/gl-stagesurf.c` | +8/-3 |
| `libobs-opengl/gl-subsystem.c/.h` | +72/-4、+162/0 |
| `libobs-opengl/gl-texture2d.c` / `gl-texture3d.c` / `gl-texturecube.c` | +28/-3、+4/0、+3/-2 |
| `deps/glad-gles/CMakeLists.txt` / `include/glad/glad.h` / `include/glad/gles2.h` / `src/gles2.c`（全新增） | +39/0、+24/0、+2122/0、+1073/0 |

> `git diff upstream/master...HEAD --numstat -- libobs-opengl/ deps/glad-gles/` 输出即上表；合计 `4239 insertions(+), 16 deletions(-)`。

**Lead 追加的旁证范围**（跨范围，见「跨范围观察」）：`libobs/data/deinterlace_base.effect`、`libobs/data/format_conversion.effect`、`libobs/graphics/graphics-imports.c`、`libobs/util/source-profiler.c`、`cmake/common/compiler_common.cmake`、`frontend/widgets/OBSQTDisplay.cpp`。

**参考文档**：`PORTING.md`（780 行）、`CODESTYLE.md`（425 行）、`.clang-format`（213 行）均已通读。

---

# 做了什么

GLES 后端的接入是一条「**CMake 选源码 + CMake 选 glad 实现 + 通用代码里条件编译修能力差异**」的链路，共 6 层：

1. **CMake 选 glad 实现**：`libobs-opengl/CMakeLists.txt:5-14` 把上游无条件的 `add_subdirectory(deps/glad)` 改成 `if(OS_ANDROID) → deps/glad-gles / else → deps/glad`。`OS_ANDROID` 由 `cmake/common/osconfig.cmake:12` 定义。两个目录刻意导出同名 `obsglad` / `OBS::glad` 别名，`target_link_libraries(... OBS::glad)` 无需分支（`deps/glad-gles/CMakeLists.txt:5-9`）。
2. **CMake 选平台后端文件**：`libobs-opengl/CMakeLists.txt:33` 在 `target_sources(PRIVATE ...)` 里加 `$<$<PLATFORM_ID:Android>:gl-android.c>`，位置与上游 `$<PLATFORM_ID:Darwin>:gl-cocoa.m`、`$<PLATFORM_ID:Windows>:gl-windows.c` 同一列表。`:66-67` 追加 `EGL`、`android` 链接库。
3. **GLAD GLES 生成物**：`deps/glad-gles/include/glad/gles2.h:22` 记录 glad2 2.0.8 命令行 `--api='gles2=3.1'` + 7 个扩展（`GL_EXT_copy_image, GL_EXT_sRGB_write_control, GL_EXT_texture_filter_anisotropic, GL_EXT_texture_format_BGRA8888, GL_EXT_texture_sRGB_decode, GL_KHR_debug, GL_OES_EGL_image`）。`deps/glad-gles/include/glad/glad.h:9-24` 是手写 shim：`#include <glad/gles2.h>` 加 5 个 `GLAD_GL_*` 映射宏。`CMakeLists.txt:32` 定义 `GLAD_GLES2_USE_SYSTEM_EGL=1`（`gles2.c:959` 依赖）、`:37` 链 `EGL GLESv3`。
4. **EGL / 上下文 / 窗口**：`gl-android.c:16-41` 直接用 NDK `<EGL/egl.h>` + `eglGetProcAddress`/`dlsym` 喂 `gladLoadGLES2()`（`:201`）；`:95-107` 选 RGBA8888+depth24+stencil8 config；`:119-134` 先试 `EGL_CONTEXT_CLIENT_VERSION 3` + `MINOR_VERSION 1`，失败退 3.0；`:175-179` 建 1×1 pbuffer 作无窗口时的兜底 current surface；`:236-247` 从 `gs_init_data.window.surface` 取 `ANativeWindow*` 并 `acquire`；`:264-310` 建窗口面、探测 `EGL_KHR_gl_colorspace` 请求 sRGB、`eglSwapInterval(0)`；`:312-321` 拆 swapchain；`:354-411` enter/leave/clear context、present。`:447-559` 为 `__linux__` 分支强行导入的 11 个 dmabuf/sync/pixmap 符号提供「不支持」实现。
5. **能力差异裁剪**：`gl-subsystem.h:40-118` 集中把桌面 GL 常量映射到 GLES 等价物/降级值；`:176-223` 新增 `convert_gs_upload_format()` 与双版本 `gs_format_needs_rb_swap()`；`:232-245`、`:276-281` 改 `convert_gs_internal_format()`；`:804-812` 给 `struct gs_device` 加 Android 专属字段。`gl-subsystem.c` 相应裁剪 timer、显存查询、debug、copy_image、P010、border clamp、sRGB 开关。
6. **纹理/着色器差异**：BGRA 上传改 `GL_RGBA` + R/B 手动换序（`gl-helpers.c:22-33,60-64`、`gl-texture2d.c:227-253`）；`glMapBuffer` → `gl_map_buffer()`（GLES 用 `glMapBufferRange`，`gl-helpers.h:127-152`）；回读统一走 FBO+`glReadPixels`（`gl-stagesurf.c:110-113`）；shader 头改 `#version 310 es` + precision + 边界补偿版 `obs_load_2d`（`gl-shaderparser.c:740-781`）；`samplerRect` → `sampler2D`（`gl-shaderparser.c:85-94`）；去掉 `gl_PerVertex` 块（`gl-shaderparser.c:237-244`）。

---

# 侵入性评估（量化表格）

条件编译插入点按 `git diff` 中新增行的 `#if/#ifdef/#ifndef` 开口数统计（不含 `#else/#elif/#endif` 与 `#define`）。

| 文件 | +行 | -行 | `#if` 块 | 关键信息 | 桌面编译产物 | 桌面运行时行为 |
|---|---:|---:|---:|---|---|---|
| `CMakeLists.txt` | 11 | 1 | 0 | 仅 CMake 分支 + 2 个 genexpr | 不变（Android 分支不生效） | 不变 |
| `gl-android.c`（新增） | 559 | 0 | 0 | 新平台后端文件，桌面不参与编译 | **完全不变** | **完全不变** |
| `gl-helpers.c` | 57 | 2 | 4 | `gl_init_face` 加 `swap_rb` 形参；`gl_copy_texture` 桌面两分支在 Android 编掉；新增 `gl_swap_rb_bgra` | **变**：`gl_init_face` 多一个形参、多一条 `swap_rb` 分支、多一次 `bfree(NULL)`；`gl_copy_texture` 多一对 `#if/#endif` | **不变**：桌面 `swap_rb` 恒 false，分支永不进入，`swap_buf` 恒 NULL；`gl_copy_texture` 三分支与上游逐字相同 |
| `gl-helpers.h` | 43 | 1 | 4（2 `#ifdef` + 2 `#if`） | `gl_init_face` 声明签名全局变更；`gl_map_buffer` 桌面 `#define`、Android `static inline`；`GL_STACK_*` 用常量存在性裁剪 | **变**：签名 + 1 个宏 + 2 个 inline | **不变**（`gl_map_buffer` 桌面仍展开为 `glMapBuffer`） |
| `gl-shaderparser.c` | 34 | 0 | 3 | `texture_rect`、顶点接口块、`buildstring` 三处整体平台分叉 | 变（3 对 `#if/#else`） | 不变（桌面走 `#else` 原逻辑） |
| `gl-stagesurf.c` | 8 | 3 | 1 | `#ifdef __APPLE__` → `#if defined(__APPLE__) \|\| defined(__ANDROID__)`；`glMapBuffer` → `gl_map_buffer` | 变（1 个宏条件 + 1 个符号替换） | 不变（桌面仍是 `__APPLE__` 判定 + `glMapBuffer`） |
| `gl-subsystem.c` | 72 | 4 | 11 | **含 1 处无守卫删除**（见 P1-1）与 1 处无守卫新增（`GLAD_GL_KHR_debug`） | **变** | **变（仅 Debug 构建的 GL 调试回调）** |
| `gl-subsystem.h` | 162 | 0 | 6 | **33 个 `#define`**（其中 25 个 `GL_*` 常量重定义）；`struct gs_device` 平台分叉 | 变（`#if` 块在桌面为空，但内联函数/结构体定义被解析） | 不变 |
| `gl-texture2d.c` | 28 | 3 | 1 | `convert_gs_upload_format`、`gl_map_buffer`、`gs_texture_unmap` 的 Android 换序块 | 变（3 处调用点 + 1 对 `#if/#endif`） | 不变 |
| `gl-texture3d.c` | 4 | 0 | **0** | **仅新增 4 行注释**（`:121-124`），无任何代码改动 | **完全不变** | **完全不变** |
| `gl-texturecube.c` | 3 | 2 | **0** | `convert_gs_format` → `convert_gs_upload_format`；`gl_init_face` 补 `swap_rb` 实参 | 变（2 个符号替换） | 不变（桌面两函数分别等价于原值/`false`） |
| `deps/glad-gles/*` | 3258 | 0 | 0 | 全新增，桌面不参与 | **完全不变** | **完全不变** |

合计：7 个上游 `.c/.h` 文件被插入约 **28 个条件编译块**；`gl-subsystem.h` 一个文件就占 33 个 `#define`。

### 结构体 / 函数签名是否因平台分叉

* **结构体：是。** `gl-subsystem.h:804-812` 在 `#if defined(__ANDROID__)` 内给 `struct gs_device` 加了 `bool framebuffer_srgb_enabled`，`sizeof(struct gs_device)` 在 Android 与桌面不同。缓解因素：该头是 `libobs-opengl` 私有头（`git show 65c320b:libobs-opengl/gl-subsystem.h:26` 只被本目录 include），同一个构建里不会出现两种布局混用，因此**无跨模块 ABI 风险**，但确实是源码级分叉，且让 `device_enable_framebuffer_srgb()` / `device_framebuffer_srgb_enabled()` 在 Android 上依赖一个「记账」字段（`gl-subsystem.c:973-1000`），语义与桌面不同。
* **函数签名：是，但不是平台分叉而是「全局加参数 + 双版本内联」。**
  * `gl-helpers.h:251-253`：`gl_init_face()` **全局无条件**加 `bool swap_rb` 形参；`gl-helpers.c:36-38` 定义同步变更；`gl-texture2d.c:34-36`、`gl-texturecube.c:39-41` 两个调用点靠 `gs_format_needs_rb_swap()` 传值，**无需 `#ifdef`**。
  * `gl-subsystem.h:203-223`：`gs_format_needs_rb_swap()` 桌面版 `{ UNUSED_PARAMETER(format); return false; }`（`:218-222`），Android 版真判断（`:204-215`）。
  * `gl-helpers.h:127-152`：`gl_map_buffer` 桌面是 `#define gl_map_buffer(t,a) glMapBuffer(t,a)`（`:152`），Android 是 `static inline` 包装 `glMapBufferRange`（`:133-149`）——**同名、同形、不同实现**。
  * `gl-subsystem.h:108-109`：`glDebugMessageCallback` → `glDebugMessageCallbackKHR`、`glClearDepth` → `glClearDepthf` 通过宏改名。

### 「桌面编译产物/行为真的不变吗？编译器能否消掉分支？」（回应 lead）

**结论：运行时语义不变；桌面二进制不字节相同；分支在 CI 配置下消不掉。**

* 语义：`gs_format_needs_rb_swap()` 是 `static inline` 且桌面恒 `return false`（`gl-subsystem.h:218-222`），调用点 `gl-texture2d.c:34-36` / `gl-texturecube.c:39-41` 经内联后传常量 `false`；`convert_gs_upload_format()` 桌面版函数体只剩 `return convert_gs_format(format);`（`gl-subsystem.h:188-200`，`#if` 块为空），内联后与原来的直接调用逐字等价。所以 `gl_texturecube.c` 的 `gl_format`、`gl-texture2d.c` 的 `gl_format` 值与上游完全一致。
* 但 `gl_init_face()` 定义在 `gl-helpers.c`，调用点在 `gl-texture2d.c` / `gl-texturecube.c`，**是不同翻译单元**。`swap_rb` 对 `gl_init_face` 而言是运行期形参，`gl-helpers.c:45-46` 的 `if (swap_rb && data && !compressed) swap_buf = bmalloc(size);` 和 `:98` 的 `bfree(swap_buf)` 在无 LTO 时无法被常量折叠。
* 是否开了 LTO：`cmake/common/compiler_common.cmake:116` 明确 `set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO OFF)`，而 `PORTING.md:581` 说明 CI/打包沿用默认 `RelWithDebInfo`。**即 CI 构建无 LTO**，该分支保留；只有 `Release`（`compiler_common.cmake:117` 为 ON）才可能被专门化消除。
* 因此桌面每次纹理上传多付：1 个参数传递、1 次恒不成立的分支、1 次 `bfree(NULL)`。开销可忽略，但「编译产物完全不变」的说法不成立。

### `#define` 是否污染全局命名空间

**是，且是本方向最集中的侵入点，但被 `__ANDROID__` 整体包住，不影响桌面。**

`gl-subsystem.h:40-118` 定义了 25 个 `GL_*` 前缀宏（`grep '^#define GL_' gl-subsystem.h` 命中 25 行，见 `:45,:50,:56-58,:62-64,:71-72,:77,:91-107`），包括：

* **等价改名**：`GL_BGRA→GL_BGRA_EXT`、`GL_FRAMEBUFFER_SRGB→GL_FRAMEBUFFER_SRGB_EXT`、15 个 `GL_DEBUG_*→GL_DEBUG_*_KHR`。
* **置 0（把合法桌面常量改成非法枚举）**：`GL_RGBA16/GL_RG16/GL_R16 = 0`（`:56-58`）、3 个 DXT 常量 = 0（`:62-64`）、`GL_TEXTURE_RECTANGLE = 0`（`:77`）。
* **语义降级**：`GL_CLAMP_TO_BORDER → GL_CLAMP_TO_EDGE`、`GL_MIRROR_CLAMP_EXT → GL_MIRRORED_REPEAT`（`:71-72`），为此又引入 `OBS_GL_HAS_BORDER_CLAMP`（`:83` / `:116`）来把 `gl-subsystem.c:545-552` 的 border-color 分支整段编掉。
* 另有 `deps/glad-gles/include/glad/glad.h:16-22` 的 5 个 `GLAD_GL_*` 映射，其中 `:18` `#define GLAD_GL_VERSION_4_3 0` 把一个版本旗子写死成 0。

缓解事实（已核验）：`grep '#define GL_BGRA\b|GL_CLAMP_TO_BORDER\b|GL_FRAMEBUFFER_SRGB\b|GL_TEXTURE_RECTANGLE\b' deps/glad-gles/include/glad/gles2.h` **无匹配**，即 GLES 头里本就没有这些名字，因此不会产生「宏重定义」告警。但风险仍在：这些名字带 Khronos 保留前缀、定义在 `gl-subsystem.h` 这个被全部 GL 源文件包含的公共头里（`:26`），任何后续新增的 GLES 扩展头一旦定义了同名常量就会静默冲突；且 `GL_RGBA16=0` 一类「把不支持改成非法值」的做法，会让调用点在编译期看不出问题、运行期才炸 `GL_INVALID_VALUE`（作者注释自称「显式失败」，`gl-subsystem.h:52-55`），属于**刻意静默降级**，不是无风险。

---

# 是否符合上游多平台风格（逐项 verdict）

| 检查项 | verdict | 证据 |
|---|---|---|
| 上游用 CMake 按 OS 选平台后端源文件 | ✅ 是 | `git show 65c320b:libobs-opengl/CMakeLists.txt` 的 `target_sources` 列表用 `$<PLATFORM_ID:Darwin>:gl-cocoa.m` / `:Windows>:gl-windows.c` / `:Linux,FreeBSD,OpenBSD>:gl-nix.c` 等 genexpr；新增的 `$<$<PLATFORM_ID:Android>:gl-android.c>`（`libobs-opengl/CMakeLists.txt:33`）在**同一列表、同一写法** |
| `gl-android.c` 是否沿用「独立平台后端文件」惯例 | ✅ **是，且是本方向最符合上游的一点** | 559 行全部集中在单一新文件，`grep` 该文件无任何 `#if defined(__ANDROID__)`；`gl-subsystem.c`/`gl-texture2d.c` 等通用文件不 include 任何 Android 头。与 `gl-cocoa.m`、`gl-windows.c`、`gl-x11-egl.c` 的定位一致 |
| `os-*.cmake` 是否沿用上游组织 | ✅ 是 | 上游 `libobs/cmake/os-linux.cmake` 用 `target_sources(libobs PRIVATE ...)` + `target_compile_definitions` + `target_link_libraries` 三段式；新增 `libobs/cmake/os-android.cmake:14-24,26,34` 结构完全一致 |
| CODESTYLE「不要造平台专有 include 块 / 把源码拆成平台专用文件」 | ❌ **否** | `CODESTYLE.md:286-287` 原文：*"Do not create platform specific 'include blocks'... Refactor the source file to be platform-specific and use CMake to add the appropriate file to the target for a specific platform."* 实际在 7 个上游通用文件里插入约 28 个 `#if defined(__ANDROID__)` 块，最典型是 `gl-subsystem.h:40-118`（78 行平台兼容块塞进公共头）、`gl-shaderparser.c:740-781`（40 行 shader 头整体二分）、`gl-subsystem.c:145-149`（`#ifndef __ANDROID__` 包住一行 `gl_enable`）。`PORTING.md:706-707` 自己也承认仓库里约 24 处 `#ifdef __ANDROID__` 包 `#include` 违反该条（虽主要在前端，不在本范围） |
| GLAD GLES 以 vendoring 放进 `deps/` | ✅ **是** | `deps/` 现有 `blake2 / glad / json11 / libcaption / libdshowcapture / w32-pthreads` 均为 vendored 第三方目录、各自带 `CMakeLists.txt`；`deps/glad-gles/CMakeLists.txt:1,16-17,39` 连 `obsglad` OBJECT 名、`OBS::glad` 别名、`FOLDER deps POSITION_INDEPENDENT_CODE TRUE` 都与 `deps/glad/CMakeLists.txt` 一致。（注：任务提到的 `deps/libff` 在本基线不存在；`Get-ChildItem deps -Directory` 输出无此项） |
| 生成代码许可证头 | ⚠️ **部分** | 生成物合规：`gles2.c:1-3`、`gles2.h:1-27` 有 `SPDX-License-Identifier: (WTFPL OR CC0-1.0) AND Apache-2.0` 且记录 glad 版本/命令行/在线链接。**手写文件缺许可证**：`deps/glad-gles/include/glad/glad.h:1-24`、`deps/glad-gles/CMakeLists.txt:1-39`、`libobs-opengl/gl-android.c:1-14` 均只有描述性注释，**无 SPDX、无 GPLv2 头**。对照 `git show 65c320b:libobs-opengl/gl-x11-egl.c:1-16` / `gl-subsystem.c:1-16`，上游每个平台后端/子系统文件都带完整 GPLv2 + Copyright 头 |
| 生成流程可复现 | ❌ **否** | `deps/glad-gles/CMakeLists.txt:3`、`include/glad/glad.h:4`、`libobs-opengl/CMakeLists.txt:5` 三处引用 `.qoder/gen-glad-gles.sh`，但 `git ls-files \| Select-String 'qoder\|gen-glad'` **无匹配**、`Get-ChildItem .qoder` 报「不存在」。3258 行生成代码没有随仓库提交的再生成配方 |
| 新增文件是否带 `.clang-format` | ⚠️ 否 | `deps/glad/.clang-format` 存在；`deps/glad-gles/` 下无同级文件（`Get-ChildItem deps/glad-gles -Recurse` 仅 4 个文件）。生成代码通常自带格式，影响很小 |
| 复用同一 `OBS::glad` 别名避免分支 | ⚠️ 取巧但脆弱 | `deps/glad-gles/CMakeLists.txt:5-9` 自述「只有 libobs-opengl 用得到这个别名，其余使用者在 Android 配置都不启用」；`libobs-opengl/CMakeLists.txt:4` 的 `if(NOT TARGET OBS::glad)` 意味着**谁先 add_subdirectory 谁赢**，一旦将来 Android 配置也引入 `deps/glad`（或另一个 target 先加），会产生重复 target 错误或悄悄选错实现 |

---

# 问题清单

## P0

本范围内未发现「正常使用必然崩溃/数据损坏」的 P0。最严重的是下面两条 P1：一条是**跨后端无守卫的共享资源改动**（影响面超出 Android），一条是**Android 上 graphics 初始化可被扩展门槛整体拒绝**。

## P1

### P1-1　`libobs/data/*.effect` 无条件修改，改变 D3D11 / 桌面 GL / Metal 的输出语义（跨后端）

**证据**：`git diff upstream/master...HEAD -- libobs/data/format_conversion.effect libobs/data/deinterlace_base.effect`；仓库同时存在 `libobs-d3d11`、`libobs-metal`、`libobs-opengl` 三个后端（`Get-ChildItem -Directory | Where Name -like 'libobs*'`），这些 `.effect` 是运行时文本、由各后端共同解析，**没有按后端生成，也没有任何平台宏**。

* `format_conversion.effect:571,582,619,630,676,736,821,1014,1021,1034,1046` 等 10+ 处把 `float3 ... : TARGET` 改成 `float4(..., 1.)`。`gl-subsystem.h:236-244` 的注释自述动机：Android 把 `GS_BGRX` 内部格式从 3 通道 `GL_SRGB8` 升到 4 通道 `GL_SRGB8_ALPHA8` 后 alpha 变真实存在，原 `float3` 输出落地 alpha=0，源被当全透明，故改 `float4(rgb,1.)`。
  * **对其他后端的影响**：桌面 GL 的 `gl_write_outputs()`（`gl-shaderparser.c:207-217`）按 `main->return_type` 生成 `out vecN outputval;`，返回类型从 `vec3` 变 `vec4`，写入 RGBA 目标时 alpha 由 0/未定义变成 1.0；D3D11 的 `float3 → SV_Target` 同样由未定义 alpha 变成 1.0；Metal 同理。只要下游有 alpha 混合/预乘 alpha 路径，画面就会变。`PORTING.md:43` 明确 CI「不覆盖任何运行时行为」，`PORTING.md:751` 亦承认录制/推流未做真机验证 —— **这三个后端的语义变化没有任何测试覆盖**。
  * 这同时是一个「修 Android 根因却改共享资产」的设计问题：真正的原因是 GLES 侧内部格式升级（4 通道），却把修复放进了所有后端共用的 shader 源码。
* `format_conversion.effect:574,585,596,609,622`（`image.Sample(def_sampler, uv, 0)` 去掉第三参数）：`gl-write_texture_call(... "texture", true)`（`gl-shaderparser.c:435-436`）把 `Sample` 直接译成 GLSL `texture(s, uv, ...)`，第三参数在桌面 GLSL 是 float bias、在 HLSL 是 int2 offset。`0` 在 GLSL ES 3.0 下因取消 int→float 隐式转换被拒。**去掉它在桌面 GL 等价于 bias=0→无 bias、在 D3D11 等价于 offset=(0,0)→无 offset，语义基本一致**，但仍是全局改动，且更优雅的修法是让 `gl-shaderparser.c` 在 GLES 下把整数字面量补成 `0.0`（该文件 `:760` 已经在 `obs_load_2d` 里对 `lod` 做了 `float(lod)`，说明作者知道这个手法），而不是改共享 effect。
* `deinterlace_base.effect:18-22,72,98-114,164,168-172,192-195` 的 `/2`→`/2.0`、`-1`→`-1.0`：`PORTING.md` 外的注释（`:18-22`）自述「HLSL 与桌面 GLSL 下写成 2.0 语义完全一致」。**这一项风险最低**，但同样无条件。
* `format_conversion.effect:767,781` 的 `65535. / 4095` → `65535. / 4095.` 属同类浮点字面量修补，语义不变。

**改进建议**：
1. 把 `float4(rgb,1.)` 的 alpha 修复**收进 GLES 后端**：在 `gl-shaderparser.c` 的 `gl_write_outputs()` 里对 Android/GLES 强制生成 `vec4` 输出并补 `, 1.0`，或更根本地——用 `GL_EXT_texture_swizzle` / sampler 的 alpha 默认值处理，避免 4 通道附件放大 alpha 语义。这样 `libobs/data/*.effect` 可与上游保持字节一致。
2. `Sample` 第三参数与 `/2` 字面量问题，全部由 `gl-shaderparser.c` 在 GLES 分支做 token 级转换（已有 `float(lod)` 先例）。
3. 若坚持改共享 effect，至少：把 `float4(rgb,1.)` 作为「修 bug」单独提交并在 D3D11/Metal/桌面 GL 上各跑一次 `format_conversion` 回归（NV12/P010/YUY2 转 RGB 的 alpha=1 与 alpha=0 对比），在 `PORTING.md` 里记录验证矩阵；并显式确认所有 `: TARGET` 输出已无 `float3`（`grep 'float3.*TARGET' libobs/data/*.effect` 现无匹配，但插件自带 effect 未核查）。

### P1-2　Android 上 `GL_EXT_texture_sRGB_decode` 被当作硬门槛，缺该扩展的设备 graphics 初始化必然失败

**证据**：`gl-subsystem.c:139-143` 的 `if (!GLAD_GL_EXT_texture_sRGB_decode) { blog(LOG_ERROR, "... is required."); return false; }` 在 Android 构建里**没有被 `#if` 包住**（`:145` 的 `#ifndef __ANDROID__` 只包住了后面的 `GL_TEXTURE_CUBE_MAP_SEAMLESS`）；`gl-subsystem.c:620` 又无条件使用 `GL_TEXTURE_SRGB_DECODE_EXT`。该扩展由 glad 的 `--extensions` 列表生成（`deps/glad-gles/include/glad/gles2.h:22,33`），`GLAD_GL_EXT_texture_sRGB_decode` 只在驱动上报该扩展时置 1（`deps/glad-gles/src/gles2.c:33`）。`gl_init_extensions()` 被 `device_create()` 调用（`:325` 之前），返回 false ⇒ `obs_reset_video()` 失败 ⇒ 整个应用无视频。

`GL_EXT_texture_sRGB_decode` 不是 GLES 3.1 核心、`PORTING.md:43` 又声明 CI 完全不覆盖运行时。同文件 `:158-163` 已经对 `GL_EXT_sRGB_write_control` 采用了「缺了就告警 + 记账降级」的温和策略，两者处理不一致。

**改进建议**：把 `GL_EXT_texture_sRGB_decode` 也改成 Android 上的能力标志（缺扩展时 `device_load_texture()` 跳过 `gl_tex_param_i(...GL_TEXTURE_SRGB_DECODE_EXT...)`，`gl-subsystem.c:620` 用 `if (GLAD_GL_EXT_texture_sRGB_decode)` 包住），或至少在 `gl_init_extensions` 里给 Android 分支降级为 `LOG_WARNING`，并把它列为真机兼容性验收项（多 SoC：Adreno / Mali / PowerVR）。

### P1-3　`gl_enable_debug()` 无守卫删除了桌面 `GL_ARB_debug_output` 分支

**证据**：`git diff` 显示 `gl-subsystem.c:116-125` 由上游（``git show 65c320b:libobs-opengl/gl-subsystem.c`` 对应段）

```c
if (GLAD_GL_VERSION_4_3) { glDebugMessageCallback(...); gl_enable(GL_DEBUG_OUTPUT); }
else if (GLAD_GL_ADB_debug_output) { glDebugMessageCallbackARB(...); }   /* 被删 */
else { ... }
```

改为

```c
if (GLAD_GL_VERSION_4_3 || GLAD_GL_KHR_debug) { ... }
else { ... }
```

**删除没有 `#ifdef` 保护**，且 `#ifdef _DEBUG`（`gl-subsystem.c:126-128` 的 `#else/#endif` 所对应的守卫）在桌面 Debug 构建同样生效。`GLAD_GL_KHR_debug` 在桌面 glad 里确实存在（`deps/glad/src/glad.c:912`、`include/glad/glad.h:8167`），所以不会编译失败，且在 GL<4.3 + KHR_debug 的驱动上反而**扩展**了调试输出；但只暴露 `GL_ARB_debug_output`（较老驱动）的桌面 Debug 构建**永久失去 KHR/ARB 调试回调**。作者显然是为了让 Android 走 `_KHR` 符号（`gl-subsystem.h:108`）而顺手删分支。

**改进建议**：保留三条分支，桌面原样，Android 用 `#if defined(__ANDROID__)` 单独给 `GLAD_GL_KHR_debug` 分支；或把 `glDebugMessageCallbackARB` 也纳入 Android 兼容块判断。至少不要为了 GLES 而删桌面的功能分支。

### P1-4　glad 生成物的再生成脚本未入库，3258 行生成代码不可复现

**证据**：`deps/glad-gles/CMakeLists.txt:3`、`include/glad/glad.h:4`、`libobs-opengl/CMakeLists.txt:5` 三处写「产物由 `.qoder/gen-glad-gles.sh` 生成（glad2 2.0.8）」；`git ls-files | Select-String 'qoder|gen-glad'` 输出为空，`Get-ChildItem .qoder` 报「不存在」。`gles2.h:22` 虽记录了完整 glad 命令行，但没有可执行的脚本、没有锁定的扩展清单来源、没有 checksum。

**改进建议**：把生成脚本（含 glad 版本、API 版本、7 个扩展列表、`GLAD_GLES2_USE_SYSTEM_EGL` 的处理、生成后对 `glad.h` shim 的拼接）提交到 `deps/glad-gles/` 下，并在 CMake 里加一个可选的 `regenerate` target；否则将来升级 NDK/glad 或增删扩展时无法重现同一份产物。

## P2

| # | 位置 | 影响 | 建议 |
|---|---|---|---|
| P2-1 | `gl-android.c:119-134` 3.1→3.0 fallback 是**死路** | 退到 3.0 后 `GLAD_GL_ES_VERSION_3_1=0`，而 `deps/glad-gles/include/glad/glad.h:16` 把 `GLAD_GL_VERSION_3_3` 映射到 `GLAD_GL_ES_VERSION_3_1`，`gl-subsystem.c:132-135` 会硬失败并打「requires OpenGL version 3.3」；`gl-shaderparser.c:741` 又无条件发 `#version 310 es`，3.0 上下文下 shader 必然编译失败。fallback 只是把清晰失败换成两个误导性失败 | 要么去掉 fallback 并在 `gl_platform_create` 里对 `GLAD_GL_ES_VERSION_3_1==0` 直接 `LOG_ERROR` 返回（`gl-android.c:206-209` 现在只打 WARNING）；要么真正支持 3.0（但需改 shader 头与 3.1 特性） |
| P2-2 | `gl-android.c:354-379,400-411`（`eglMakeCurrent` / `eglSwapBuffers` 失败只 `blog`） | 无 `EGL_CONTEXT_LOST` 恢复路径；`eglMakeCurrent` 失败后仍继续 GL 调用，容易级联成难查的 GL 错误。`graphics-imports.c:242-243` 的 `device_register_loss_callbacks`/`unregister` 是 `GRAPHICS_IMPORT_OPTIONAL` 但只在 `_WIN32` 分支导入，Android 无法用 | 把 enter/leave/clear 的失败上抛或至少置 `device->context_lost` 标志并短路 present；实现 loss callbacks 并在 Android 分支也 `GRAPHICS_IMPORT_OPTIONAL`；在 `device_present` 里识别 `EGL_CONTEXT_LOST` 触发重建 |
| P2-3 | `gl-android.c:312-321` `gl_platform_cleanup_swapchain` | `eglMakeCurrent(...pbuffer...)` 的返回值未检查就 `eglDestroySurface`；若切回 pbuffer 失败，被销毁的窗口面可能仍是别处 current surface。`:249-262` 的 `gl_windowinfo_destroy` 对「surface 仍存活」只打 WARNING 不清理 | 检查返回值；把 EGLSurface 的销毁统一收口到一处（当前依赖 `gs_swapchain_destroy` 的调用顺序 `gl-subsystem.c:1542-1548`） |
| P2-4 | `gl-subsystem.h:71-83` `GL_CLAMP_TO_BORDER → GL_CLAMP_TO_EDGE` + `OBS_GL_HAS_BORDER_CLAMP 0` | `gl-subsystem.c:316-323` 创建的 `raw_load_sampler` 用 `GS_ADDRESS_BORDER` + `border_color=0`；在 Android 上越界采样从「透明黑」变成「边缘像素」。作者注释自称「阶段 1 可接受」，`gl-shaderparser.c:750-760` 用 `obs_load_2d` 的边界判定补偿了 `Load()` 路径，但**普通 `Sample()` 路径没有补偿**，属于静默画面差异 | 补运行时能力标志（见「更优实现」），并给 raw/unorm 直采加真机边缘像素对比用例 |
| P2-5 | `gl-helpers.c:67-79` | Android 生产路径**永久**保留 bring-up 诊断：每次 `glTexImage2D` 后无条件 `glGetError()`（`:70`），且只消费一个错误码、不 drain；`glGetError()` 在部分驱动上会强制同步。`gl_success()` 原本会 drain 全部错误 | 用 `OBS_ANDROID_GL_DIAG` 编译期/运行期开关包起来，默认关；错误分支改用 `gl_success` + 单独记录参数 |
| P2-6 | `gl-android.c:1-14`、`deps/glad-gles/CMakeLists.txt:1-39`、`deps/glad-gles/include/glad/glad.h:1-24` | 三个手写新文件**无 SPDX / GPLv2 许可证头**，与上游平台后端（`git show 65c320b:libobs-opengl/gl-x11-egl.c:1-16`）不一致；`glad.h` shim 是手写代码却与 `(WTFPL OR CC0-1.0) AND Apache-2.0` 的生成物混放，授权来源不清 | 补 GPLv2 头（libobs-opengl 部分）与明确的 SPDX（deps 部分），并在 shim 里注明「本文件为 OBS 手写、非 glad 生成」 |
| P2-7 | `deps/glad-gles/include/glad/glad.h:2` | 注释称「libobs-opengl 里统一 `#include <glad/gl.h>`」，实际 `git show 65c320b:libobs-opengl/gl-subsystem.h:26` 和现仓 `gl-subsystem.h:26` 都是 `#include <glad/glad.h>`；`deps/glad/include/glad/` 下也只有 `glad.h/glad_egl.h/glad_wgl.h`，无 `gl.h`。注释会误导后续维护者 | 修正为 `<glad/glad.h>` |
| P2-8 | `deps/glad-gles/CMakeLists.txt:5-9` + `libobs-opengl/CMakeLists.txt:4` | 两侧共用 `OBS::glad` 别名、靠 `if(NOT TARGET OBS::glad)`「先到先得」，一旦 Android 配置里别处也 `add_subdirectory(deps/glad)` 会重复 target 或静默选错实现 | 用 `OBS::glad_gles` 别名 + `target_link_libraries` 里的 `$<PLATFORM_ID:Android>` genexpr 显式选择，避免同名隐式竞争 |
| P2-9 | `gl-subsystem.c:452-475`（`device_timer_create` Android 恒 NULL）配合 `libobs/util/source-profiler.c:449-451` | `source_profiler_source_render_begin()` 里 `*timer = gs_timer_create(); gs_timer_begin(*timer);` **无 NULL 检查**。Android 上 `gs_timer_begin/end/get_data` 是 no-op（`gl-subsystem.c:1632-1650,1652-1670`）、`gs_timer_destroy(NULL)` 安全（`:1623`），所以不崩，但 `gpu_enabled`（`source-profiler.c:271`）被用户开启后会把 NULL 塞进 `render_timers`（`:473-475`），功能静默失效 | 在 `source_profiler.c:450` 后加 `if (*timer) gs_timer_begin(*timer);`，或在 `gs_timer_create()` 包装层统一处理；同时把 `gpu_enabled` 的置位与实际 timer 能力挂钩 |
| P2-10 | `gl_subsystem.h:56-58,62-64,77` 把 7 个 `GL_*` 常量置 0 | 如作者注释所述「显式失败」，但调用点（如 `convert_gs_internal_format` 的 `GS_R16/GS_RG16/GS_RGBA16` 分支）在编译期看不出异常，运行期才 `GL_INVALID_VALUE`；且 `GL_TEXTURE_RECTANGLE=0` 让 `gs_texture_is_rect()` 恒 false 依赖「没人创建矩形纹理」这一隐含前提 | 改成 `static inline` 的能力函数返回「不支持」并让上层显式报错，而不是伪造 GL 常量值 |
| P2-11 | `gl-shaderparser.c:740-781` Android 分支**漏发 `obs_load_3d`** | `gl_write_texture_code()`（`:443-445`）把 `texture3d` 变量的 `.Load()` 映射到 `obs_load_3d`；桌面 `#else` 分支在 `:773-780` 定义了它，Android `#if` 分支（`:741-761`）只定义了 `obs_load_2d`，`grep 'obs_load_3d' libobs-opengl/` 仅在桌面分支命中。目前 `grep 'texture3d' **/*.effect` 只找到 `plugins/obs-filters/data/color_grade_filter.effect:5`，且该 effect 用 `.Sample()`（`:80,92,105,126`）而非 `.Load()`，所以**尚未触发**；但任何新增/第三方插件的 3D `Load()` effect 在 Android 上会直接 shader 编译失败（`undeclared identifier obs_load_3d`），且报错位置离根因很远 | 在 Android 分支补一份 `obs_load_3d`（GLES 3.1 的 `textureSize(sampler3D,int)` / `textureLod(sampler3D,vec3,float)`，注意 `ivec3→vec3`、`int→float` 显式转换），或把 `obs_load_2d/3d` 抽成共享的按平台生成函数，避免两份 prelude 各自漂移 |

---

# 更优实现方案

按「收益/代价」排序，给出三档：

### 方案 A（推荐，代价最小、最贴合上游）：把差异收进「平台后端文件 + 一张能力表 + 一个兼容头」

沿用 `gl-android.c` 这个已经做对的「独立平台后端文件」，但把散落在 7 个通用文件里的约 28 个 `#ifdef` 全部搬走：

1. **能力表**：在 `gl-subsystem.h` 里定义**无平台分叉**的
   ```c
   struct gl_caps {
       bool has_border_clamp;        /* 替代 OBS_GL_HAS_BORDER_CLAMP */
       bool has_srgb_write_control;  /* 替代 #if 里的 GLAD_GL_EXT_sRGB_write_control 判断 */
       bool has_timer_query;
       bool has_p010;
       bool has_copy_image;
       bool has_gl_colorspace;
       ...
   };
   ```
   `gl_platform_create()`（`gl-android.c:184-217`）与桌面 `gl-nix.c`/`gl-windows.c` 各自填表；通用代码改成 `if (device->caps.has_border_clamp)`。**这一举消除 `struct gs_device` 的平台分叉（`gl-subsystem.h:804-812`）、`device_enable_framebuffer_srgb()` 的双实现（`gl-subsystem.c:973-1000`）、`device_p010_available()` 的 `#if` 分支（`:1561-1575`）**。OBS 已有这套抽象的雏形：`device_nv12_available` / `device_p010_available` / `device_shared_texture_available` / `device_query_dmabuf_capabilities`（`libobs/graphics/device-exports.h:132-162`），扩展它是顺势而为。
2. **格式转换函数表 / 编译期兼容头**：把 `convert_gs_*()`、`gs_format_needs_rb_swap()`、`gl_map_buffer()`、`glDebugMessageCallback` 映射、25 个 `GL_*` 宏全部挪到 `libobs-opengl/gl-gles-compat.h`，由 CMake 通过 `target_compile_definitions(libobs-opengl PRIVATE $<$<PLATFORM_ID:Android>:OBS_GLES>)` 控制；桌面版是零宏、零额外参数的纯直通头。`gl-subsystem.h` 只保留一行 `#include "gl-gles-compat.h"`（用 CMake 决定 include 目录里放哪个同名文件更好，连 `#include` 守卫都不需要）。
3. **shader 差异按平台拆文件**：`#version/precision/obs_load_2d/gl_PerVertex/samplerRect` 的差异（现 `gl-shaderparser.c:85-94,237-244,740-781`）抽成 `gl-shaderparser-gles.c`，由 `libobs-opengl/CMakeLists.txt` 的 `$<PLATFORM_ID:Android>` 选择；或至少抽成 `gl_shader_buildstring_gles()` 一个函数放在 `gl-android.c` 里，通用文件只做一次函数指针调用。
4. **effect 修复收进 parser**：见 P1-1 建议，让 `libobs/data/*.effect` 与上游字节一致。

**代价**：一次性重构约 300~400 行、需要把 `device->caps` 作为参数穿过若干函数；之后每次 rebase 上游，`libobs-opengl/` 里除 `gl-android.c` 和新增的 `gl-gles-compat.h` / `gl-shaderparser-gles.c` 外都无需处理冲突。**收益**：桌面 GL 路径编译产物与上游完全等价（这是本次审查里最难自证的一点），也直接满足 `CODESTYLE.md:286-287`。

### 方案 B（最干净，代价最大）：独立 `libobs-gles/` 后端目录

把 `libobs-opengl/` 整目录复制成 `libobs-gles/`，在其中自由改 GLES 语法；`libobs-opengl/` **保持与上游 65c320b 逐字一致**。`libobs/CMakeLists.txt` 按 `OS_ANDROID` 选后端模块名。

**这恰恰是上游自己的组织方式**：仓库同时有 `libobs-d3d11/`、`libobs-metal/`、`libobs-opengl/` 三个并列后端目录（`Get-ChildItem -Directory -Filter 'libobs*'`），而不是在一个后端里用 `#ifdef` 兼容 D3D/Metal。

**代价**：约 4k 行源码永久分叉，上游每次修 `libobs-opengl` 的 bug 都要人工同步到 `libobs-gles`；但考虑到本 fork 已有 137 文件 / +21932 行的改动量和长期 rebase 成本，这个「复制一次、之后互不干扰」的方案在维护上可能反而更省。**收益**：桌面产物零风险、GLES 代码里可以彻底不用 `#ifdef`、能名正言顺用 GLES 3.1 原生 API 而不是伪造桌面常量。

### 方案 C（折中）：保留现状，但补齐三件事

如果短期不动结构，至少：(1) 把 25 个 `GL_*` 宏收进单独 `gl-gles-compat.h` 并把 `#define GL_RGBA16 0` 一类改成显式报错函数；(2) 补 `EGL_CONTEXT_LOST`/线程亲和/返回值检查（P2-2、P2-3）；(3) 提交 glad 生成脚本（P1-4）与许可证头（P2-6）。代价最小，但通用文件里的 `#ifdef` 会随上游 rebase 持续产生冲突。

### 关于 `gl_init_face()` 加参数的评价（回应 lead）

`gl-helpers.h:251-253` + `gl-subsystem.h:203-223` 这套「**全局加一个 `bool` 形参 + 用一个恒 false 的 inline 谓词喂它**」的写法，确实比在两个调用点各写一对 `#ifdef` 更符合上游风格：调用点干净（`gl-texture2d.c:34-36`、`gl-texturecube.c:39-41` 无任何条件编译），桌面语义不变，且把「是否需要换序」表达成了一个有名字的语义谓词而非平台宏——这一点值得肯定，是本次移植里少数称得上「抽象」而非「打补丁」的地方。

但它的边界也很清楚：谓词名字是 `gs_format_needs_rb_swap()`（格式语义），而不是 `is_gles()`（平台语义），这是好设计；可 `gl_init_face()` 的实现里仍必须靠 `#if defined(__ANDROID__)`（`gl-helpers.c:56-65`）来编掉 `memcpy`/换序，**且桌面产物因跨 TU 无法常量折叠（见上文 LTO 结论）而并非字节不变**。若采用方案 A 的第 2 点，这个模式可以升级成「`gl_init_face` 接受一个 `const struct gl_upload_ops *` 或能力函数指针」，桌面传直通 ops、GLES 传换序 ops，连 `swap_rb` 这个布尔都不需要，桌面分支也能被 LTO 之前的常量传播消掉。

---

# 实现亮点

1. **平台后端文件的隔离度很高**：`gl-android.c` 559 行、`0` 个 `#ifdef`，把 EGL/NDK/ANativeWindow 全部关在一个 CMake genexpr 选中的文件里（`libobs-opengl/CMakeLists.txt:33`），完全对上 `gl-cocoa.m`/`gl-windows.c` 的惯例。这是本次移植最符合上游的一点。
2. **`__linux__` 导出符号的兜底很到位**：`libobs/graphics/graphics-imports.c:244-255` 在 `__linux__` 分支导入 11 个 `GRAPHICS_IMPORT`（非 OPTIONAL，缺一个就 `success=false`、整个 graphics 模块加载失败）；NDK clang 也定义 `__linux__`，`gl-android.c:447-559` 逐一提供了这 11 个「不支持」实现（经 `grep` 核对，`device_texture_create_from_dmabuf / query_dmabuf_capabilities / query_dmabuf_modifiers_for_format / texture_create_from_pixmap / query_sync_capabilities / sync_create / sync_create_from_syncobj_timeline_point / sync_destroy / sync_export_syncobj_timeline_point / sync_signal_syncobj_timeline_point / sync_wait` 全部覆盖），且这些符号在 `libobs/graphics/device-exports.h:143-162` 带 `EXPORT`，可见性没问题。注释（`gl-android.c:434-445`）把「为什么必须写」讲清楚了，属于高质量工程债记录。
3. **退化策略「宁可显式失败/如实报不支持，也不静默错色」**：`gl-subsystem.h:52-55`（16-bit unorm 不给 16F 而是置 0 让 `glTexImage2D` 报错）、`gl-subsystem.c:1561-1575`（P010 如实返回 false，并写明补 `EXT_texture_norm16` 的完整路径）、`gl-subsystem.c:237-280`（显存查询返回 0）、`gl-subsystem.c:973-1000`（sRGB 开关「只记账」）、`gl-subsystem.c:1652-1670`（timer 取不到如实 false）。方向正确，只是「置 0」的实现手法可商榷（P2-10）。
4. **注释密度与因果链**：`gl-subsystem.h:176-190`（BGRA 上传/回读不对称的实测数据 0x502/0x500、为什么不用 `GL_UNPACK_SWAP_BYTES`）、`:236-244`（`GS_BGRX` 升 4 通道导致 alpha 问题的完整证据链）、`gl-android.c:109-113`（sRGB 必须建面时请求而非塞进 `eglChooseConfig`）、`gl-android.c:291-293`（连「上一版把 0x3089 写在同一行导致 grep 假绿」都记下来）。这类注释对后续 rebase 极有价值，明显优于普通移植。
5. **GLAD 的 EGL 头冲突处理**：`deps/glad-gles/CMakeLists.txt:11-14,29-32` 明确不打包 glad 自带的 `eglplatform.h`/`khrplatform.h`、用 `GLAD_GLES2_USE_SYSTEM_EGL=1`（对应 `gles2.c:959`），`gl-android.c:5-7,28-40` 用 `GLADapiproc` 精确匹配 glad 2.0.8 的 `GLADloadfunc` 签名以避开 `-Werror`。都是踩过坑才写得出来的细节。
6. **`GL_STACK_UNDERFLOW/OVERFLOW` 用常量存在性裁剪**（`gl-helpers.h:49-59`）而不是 `#ifdef __ANDROID__`：注释（`:44-48`）还指出「本文件被 `gl-subsystem.h` 的兼容块 *前面* 一行 include，宏此时还没生效」，说明作者理解了 include 顺序。这是全范围里唯一一处「按能力而非按平台」的条件编译，值得推广。
7. **`eglSwapInterval(0)`**（`gl-android.c:302-307`）与上游 `gl-x11-egl.c:528`、`gl-wayland-egl.c:335` 对齐，并解释了「`device_present` 持 obs 全局 graphics 锁、不能卡在 vsync」的后果。
8. **1×1 pbuffer 兜底**（`gl-android.c:175-179,196-199`）让 `obs_reset_video()` + 离屏渲染不依赖 Java/Qt 窗口，与 `PORTING.md:34` 的「原生层不依赖 Qt」声明一致（本范围内 `gl-android.c:16-26` 只 include NDK 头与 `gl-subsystem.h`/`glad`，确无 Qt 依赖）。

---

# PORTING.md 声明核验表

| 声明（行号） | 判定 | 证据 |
|---|---|---|
| `:33` CI 覆盖「libobs / **libobs-opengl(GLES)** / 全部 `android-*` 采集插件的编译与链接」 | **属实（限编译）** | `libobs-opengl/CMakeLists.txt:5-14,33,66-67` + `deps/glad-gles/CMakeLists.txt:16-38` 构成完整 GLES 构建链；`PORTING.md:43` 自己限定「不覆盖任何运行时行为」，两者一致 |
| `:34` `-DENABLE_FRONTEND=OFF` 验证「原生层不依赖 Qt」 | **属实（本范围内）** | `gl-android.c:16-26` 仅 `<dlfcn.h>/<stdint.h>/<string.h>/<EGL/egl.h>/<EGL/eglext.h>/<android/native_window.h>` + `gl-subsystem.h`/`<glad/glad.h>`，无 Qt 头；`deps/glad-gles/CMakeLists.txt:37` 只链 NDK 的 `EGL GLESv3` |
| `:43` 「不覆盖：任何运行时行为」 | **属实，且是本报告多条风险的根因** | 与 P1-2（扩展门槛）、P2-2（context loss）、P2-4（CLAMP_TO_BORDER 静默降级）、P1-1（跨后端语义变化无任何回归）直接呼应 |
| `:734-737` 浮动 dock 崩溃链「新原生窗口销毁 Surface → `makeCurrent(): no EGLSurface` → `QRhiGles2` 失败 → SIGSEGV」；倾向「Android 上直接去掉 `DockWidgetFloatable`」 | **属实（现象与归因层级），但「根因在 gl-android.c 这一层」存疑/倾向否** | 见下方专项结论 |
| `:751` 「录制 / 推流 / 屏幕采集 / 系统内录 / 相机方向」未做真机功能验证 | **属实** | 与 `:43`、以及 `gl-subsystem.h:236-244` 注释里「b102/b104 实测出图」这类仅针对预览的验证记录一致；本范围内 `gl-stagesurf.c` 的 FBO 回读、`gl-texture2d.c:227-253` 的 PBO 换序均属录制/回读链路，无 CI 覆盖 |
| `:760` 「容器格式限制 5 处各自编码、**预览面所有权 5 处各自实现** —— 收敛成单一入口」 | **属实（跨范围旁证）** | `frontend/widgets/OBSQTDisplay.cpp:150-159` 明说「Java 全应用只有一块共享的 SurfaceView」、`:155-159` 用 `obsAndroidDisplayIsLifecycleOwner()` 判定谁有权摘面；`:118-121` 承认「全应用有好几个 OBSQTDisplay，构造就抢会把主预览的槽顶掉」；`:286-327` 的 `createDisplayFromExistingSurface()` 是又一处所有权交接（`:320-322` 手工 balance `ANativeWindow` 引用） |
| `:674` 「上游的 `.clang-format` / `.editorconfig` / `CODESTYLE.md` 全部适用，新增代码要遵守」 | **部分不实** | `CODESTYLE.md:286-287` 被本范围约 28 个平台 `#ifdef` 违反；`gl-android.c:1-14` 无上游通行的 GPLv2 头（对照 `git show 65c320b:libobs-opengl/gl-x11-egl.c:1-16`）。`:774-776` 自己也把「约 24 处 `#ifdef __ANDROID__` 包 `#include`」列为待办 |
| `:701` 「`.c` 文件不受大括号约束 —— C 走 Linux 内核风格」 | **属实** | `CODESTYLE.md:147-157` 确认纯 C 走 Linux Kernel Coding Style、C17；本范围全是 `.c/.h`，不受 `:199` 的 C++ 大括号规则约束 |
| `:690` 「`.clang-format` 里那段 `IndentWidth: 4 / UseTab: Never` 属于 `Language: ObjC`」 | **属实（旁证）** | `.clang-format:107` 的 `---` 分隔符、`:112` `Language: ObjC`、`:169` `IndentWidth: 4`、`:212` `UseTab: Never`；文件开头 `:63,103,106` 是 `8` / `ForContinuationAndIndentation` |
| `:33` 之外**没有任何 GLES/EGL/GLAD 专节** | **文档缺口（存疑）** | `grep 'GLES\|OpenGL\|EGL\|GLAD\|glad\|图形后端' PORTING.md` 仅命中 `:33` 与 `:735` 两处。本次最侵入的子系统（15 文件 / +4239 行 / 28 个条件编译块 / 25 个 GL 常量重定义 / 两个共享 effect 文件）在 780 行移植说明里没有独立章节，只有代码内注释。建议补一节「GLES 后端：能力矩阵与已知降级」 |

### 浮动 dock 崩溃的根因是否落在 `gl-android.c` 这一层

**判定：主因不在 `gl-android.c` 的 EGL/上下文实现，而在 Qt 平台插件 + 前端窗口/Surface 所有权管理；`gl-android.c` 至多是加剧因素。**

证据链：

1. `makeCurrent(): no EGLSurface` 是 **Qt `QRhiGles2` / `QAndroidPlatformOpenGLContext`** 的错误串（Qt 为整个 scene graph 维护自己的 EGLContext/EGLSurface），而 `libobs-opengl` 用的是自己 `eglGetDisplay(EGL_DEFAULT_DISPLAY)` + `eglCreateContext` 出来的**另一套** EGLContext（`gl-android.c:48-54,145,172`），两者不共享 surface。`gl-android.c` 全文没有调用 Qt 的上下文、也从不 `eglMakeCurrent` 到 Qt 的 surface。
2. `PORTING.md:734-737` 自述的触发链是「浮动 dock → **新原生窗口销毁 Surface**」，即 Qt 新建 QWindow/ANativeWindow 时其 EGLSurface 尚未就绪或已被回收；而提出的修复是「Android 上直接去掉 `DockWidgetFloatable`」——这是**前端/Qt 层**的改动，作者本人也没把修复指向 `libobs-opengl`。
3. 前端确有一个与 Qt 窗口层级强耦合的独立 Java SurfaceView：`OBSQTDisplay.cpp:229-230`（"attach 让 Java 建/复用那块置顶 SurfaceView"）、`:310-322`（把 `ANativeWindow*` 经 `gs_init_data.window.surface` 交给 `gl-android.c:242`）、`:143-144`（"gs_swapchain_destroy（eglDestroySurface + ANativeWindow_release）"）。浮动 dock 造成的窗口层级/可见性抖动会先作用于这套共享 SurfaceView，再间接触发 Qt 的 surface 重建。
4. `gl-android.c:242-246` 对 `ANativeWindow` 做了 `acquire`、`:258-259` 在 `gl_windowinfo_destroy` 里 `release`，与 `OBSQTDisplay.cpp:320-322` 的「swapchain 侧已加过一次引用，我们这次 fromSurface 的引用要还掉」形成手工引用平衡——引用计数一旦错一拍，表现正是「Surface 被提前销毁」。这是**跨层契约**，风险在两侧接口而非 `gl-android.c` 内部。

**加剧因素（属本层，需修）**：`gl-android.c:354-379` 的 `gl_clear_context` / `device_leave_context` 无条件 `eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)`，失败仅打日志；`eglMakeCurrent` 是**按调用线程**释放当前上下文，若 `device_create`（`gl-subsystem.c:325` 调用 `gl_clear_context`）或 `device_leave_context` 恰好跑在 Qt 也持有 EGL context 的同一线程上，会连带解绑 Qt 的上下文。`:400-411` 的 `device_present` 对 `eglSwapBuffers` 失败只记日志、无 `EGL_CONTEXT_LOST` 恢复（P2-2）。这些不会**产生** `makeCurrent(): no EGLSurface`，但会让窗口面抖动时的失败更难收敛、更容易升级成 SIGSEGV。

---

# 跨范围观察

1. **共享 effect 的后端爆炸半径**（lead 追加，详见 P1-1）：`libobs/data/*.effect` 被 `libobs-d3d11`、`libobs-metal`、`libobs-opengl` 三后端共用；`format_conversion.effect` 的 `float3→float4(rgb,1.)` 与 `deinterlace_base.effect` 的 `/2→/2.0` 都是**无守卫**改动。`gl-shaderparser.c:207-217` 证实 GL 侧按返回类型生成 `out vecN`，所以 `float3→float4` 在桌面 GL 上确实改变了写出的 alpha。建议按后端生成 / 在 parser 内做 GLES 适配，共享资产与上游保持一致。
2. **`gl-texture3d.c` 与 `gl-texturecube.c` 核实结果**（lead 追加）：
   * `gl-texture3d.c:121-124` **只加了 4 行注释**，`convert_gs_format()` 调用原样保留 —— 桌面与 Android 编译产物、行为都完全不变。注释与 `gl-subsystem.h:176-190` 的「上传/回读分离」策略自洽（3D 纹理不走 `gl_init_face`，无换序参数，故保持 `GL_BGRA`）。这处「宁可留注释也不改代码」是恰当的。
   * `gl-texturecube.c:39-41` 给 `gl_init_face` 补 `gs_format_needs_rb_swap()` 实参、`:66` 把 `convert_gs_format` 换成 `convert_gs_upload_format` —— **两处都是无守卫改动**，但桌面语义不变（见「侵入性评估」的 LTO 分析）。风险不在行为而在「桌面产物不再与上游等价」以及「`convert_gs_upload_format` 在桌面是纯直通、在 Android 是 `GL_RGBA`+换序」这种同名函数语义分叉。
3. **生成器脚本未入库**（P1-4）与 **`.gitignore` 的 `/*` 规则**（`PORTING.md:78-89`）：`deps/glad-gles/` 已成功入库（`git ls-files` 命中 4 个文件），说明 `.gitignore` 放行链是通的；但 `.qoder/` 整体不在仓库里，可能是被 `/*` 吞掉的又一个实例。建议 `git check-ignore -v .qoder/gen-glad-gles.sh` 确认。
4. **`libobs/cmake/os-android.cmake`** 复用 `os-linux.cmake` 的三段式结构、但替换 `obs-nix.c → obs-android.c` 与 `os_generate_uuid → platform-android.c`（文件头部注释），符合上游多平台惯例；其中 `-Wl,--exclude-libs,ALL`（`:34` 附近）处理静态 FFmpeg 汇编符号抢占，属于构建层亮点，不在本范围细评。
5. **`cmake/common/compiler_common.cmake:113-124`** 决定 LTO 开关，直接决定「`gl_init_face` 的桌面分支能否被消掉」（见上文）；`PORTING.md:581` 确认打包用 `RelWithDebInfo`（LTO OFF）。
6. **`libobs/util/source-profiler.c:449-451`** 的 NULL timer 无检查（P2-9），是「Android 恒返回 NULL」与上游调用方假设之间的契约缺口。
7. **`PORTING.md` 缺少 GLES 专节**，而代码内注释反而极详尽（多处引用 `plan.md §8.9`、`0907摄像头问题.md §2`、`.qoder/gen-glad-gles.sh` 等仓库外/未入库文档）。建议把能力矩阵（GLES 3.1 必需、`EXT_texture_sRGB_decode` 必需、P010/timer/dmabuf 不支持、CLAMP_TO_BORDER 降级、BGRA 换序）写进 `PORTING.md`，避免知识只存在于代码注释和未入库的 plan 文档里。

---

*报告完。所有结论均附 `文件:行号` 或命令输出证据；审查过程未修改任何源码文件。*
