# libobs 核心层 + 平台抽象 + 上游侵入性对照 — 代码审查报告

> 审查范围：`libobs/` 下全部改动与新增文件（16 个）。基线 `65c320b`（上游 33.0.1），HEAD `dc6e156`。
> 所有结论基于 `git diff upstream/master...HEAD -- <path>`（三点 diff，merge-base = `65c320b46e1af5c7a555161aea6182cdf7a8c524`）、`git show 65c320b:<path>`、`git grep` 输出，以及 `PORTING.md` / `CODESTYLE.md` / `.clang-format` 原文。
> 本审查为**只读**，未修改任何源码文件。

---

# 范围

| # | 文件 | 性质 |
|---|---|---|
| 1 | `libobs/CMakeLists.txt` | 上游文件·修改 |
| 2 | `libobs/cmake/os-android.cmake` | 新增 |
| 3 | `libobs/obs-android.c` | 新增 |
| 4 | `libobs/obs-android.h` | 新增 |
| 5 | `libobs/obs-core-modules-loader-android.c` | 新增 |
| 6 | `libobs/util/platform-android.c` | 新增 |
| 7 | `libobs/data/deinterlace_base.effect` | 上游文件·修改 |
| 8 | `libobs/data/format_conversion.effect` | 上游文件·修改 |
| 9 | `libobs/graphics/graphics.h` | 上游公共头·修改 |
| 10 | `libobs/obs.h` | 上游公共头·修改 |
| 11 | `libobs/obs-display.c` | 上游文件·修改 |
| 12 | `libobs/obs-nix-platform.c` | 上游文件·修改 |
| 13 | `libobs/obs-service.c` | 上游文件·修改 |
| 14 | `libobs/util/platform.c` | 上游文件·修改 |
| 15 | `libobs/util/platform-nix.c` | 上游文件·修改 |
| 16 | `libobs/util/threading-posix.c` | 上游文件·修改 |

统计口径：`git diff upstream/master...HEAD --numstat -- libobs/` → **16 文件，+1163 / -45**（全仓库为 137 文件 +21932/-148，libobs 仅占约 5% 的新增行，但集中了几乎全部"上游公共 API 侵入"）。

---

# 做了什么

libobs 层的 Android 支持可概括为**「新增一个 Android 平台后端 + 复用 POSIX/nix 后端 + 少量共享文件打补丁」**三件事：

1. **CMake 平台分派**：`libobs/CMakeLists.txt:268-269` 新增 `elseif(OS_ANDROID) include(cmake/os-android.cmake)`；`cmake/common/osconfig.cmake:9-12`（范围外，作为证据）用与 `OS_WINDOWS`/`OS_MACOS`/`OS_LINUX` 同一套模式 `set(OS_ANDROID TRUE)`。

2. **独立平台后端 `libobs/cmake/os-android.cmake:16-28`**：用 `target_sources(libobs PRIVATE ...)` 加入
   - 新增的 `obs-android.c`（对应上游 `obs-nix.c` / `obs-windows.c` / `obs-cocoa.m` 的平台角色：模块路径、`find_libobs_data_file`、`log_system_info`、热键空实现）；
   - 新增的 `obs-core-modules-loader-android.c`（对应上游 `obs-core-modules-loader-{nix,win,mac}.c`）；
   - 新增的 `util/platform-android.c`（只实现 `os_generate_uuid()`，因 bionic 无 libuuid）；
   - **原样复用** `platform-nix.c` / `threading-posix.c` / `pipe-posix.c` / `obs-nix-platform.c` / `audio-monitoring/null/`。
   并 `target_link_libraries(libobs PRIVATE ... m log android mediandk)`、`target_link_options(libobs PRIVATE -Wl,--exclude-libs,ALL)`（`os-android.cmake:31-37`）。

3. **公共 API 与数据文件**：`graphics.h` 给 `struct gs_window` 加 Android 分支（`ANativeWindow*` → `void *surface`）；`obs.h` 条件包含 `obs-android.h`（4 张 Java↔插件回调总线：USB / 音频 / 相机 / 屏幕采集，见 `obs-android.h:10-296`）；两个 `.effect` 文件做 GLSL ES 3.0 兼容化。

4. **共享文件补丁**：`platform.c`（`timespec_get` 兼容 shim）、`platform-nix.c`（屏蔽 uuid）、`threading-posix.c`（`pthread_setname_np` 分支）、`obs-nix-platform.c`（默认平台值改 INVALID）、`obs-service.c`（补两处空指针校验）、`obs-display.c`（纯重构）。

---

# 侵入性评估（含量化表格）

## 逐文件量化

数据来自 `git diff upstream/master...HEAD --numstat -- libobs/` 与对 diff 中新增行的逐行统计（`#dirs` = 新增的 `#if/#ifdef/#ifndef/#elif/#else/#endif` 条数）：

| 文件 | +行 | -行 | 新增条件编译指令 | 含 `__ANDROID__` 的新增行 | 守卫 |
|---|---:|---:|---:|---:|---|
| `libobs/CMakeLists.txt` | 2 | 0 | 0 | 0 | **无守卫** |
| `libobs/cmake/os-android.cmake` | 37 | 0 | 0 | 1 | 新增文件 |
| `libobs/data/deinterlace_base.effect` | 17 | 13 | 0 | 0 | **无守卫** |
| `libobs/data/format_conversion.effect` | 29 | 29 | 0 | 0 | **无守卫** |
| `libobs/graphics/graphics.h` | 5 | 0 | 1 | 1 | 有守卫 |
| `libobs/obs-android.c` | 586 | 0 | 6 | 0 | 新增文件 |
| `libobs/obs-android.h` | 300 | 0 | 4 | 0 | 新增文件 |
| `libobs/obs-core-modules-loader-android.c` | 109 | 0 | 0 | 0 | 新增文件 |
| `libobs/obs-display.c` | 3 | 1 | 0 | 0 | **无守卫** |
| `libobs/obs-nix-platform.c` | 10 | 0 | 3 | 2 | 有守卫 |
| `libobs/obs-service.c` | 4 | 0 | 0 | 0 | **无守卫** |
| `libobs/obs.h` | 4 | 0 | 2 | 1 | 有守卫 |
| `libobs/util/platform-android.c` | 39 | 0 | 0 | 1 | 新增文件 |
| `libobs/util/platform-nix.c` | 4 | 0 | 4 | 2 | 有守卫 |
| `libobs/util/platform.c` | 13 | 1 | 3 | 1 | 有守卫 |
| `libobs/util/threading-posix.c` | 1 | 1 | 1 | 1 | 有守卫（改既有链） |
| **合计** | **1163** | **45** | **24** | **9** | |

**上游文件被触碰 11 个，仅 +75/-45 行、24 条条件编译指令**——绝对量很小；但其中 **5 个文件是完全无守卫改动**（`CMakeLists.txt`、2 个 `.effect`、`obs-display.c`、`obs-service.c`），这是本次「侵入性」评估的重点（lead 指定核实项）。新增 5 个文件共 +1071 行，全部集中在 Android 侧，符合"独立后端"的方向。

## 对上游公共 API 的侵入

| 头文件 | 改动 | ABI/API 影响 | 桌面编译产物影响 |
|---|---|---|---|
| `libobs/graphics/graphics.h:485-489` | `struct gs_window` 新增 `#elif defined(__ANDROID__)` 分支，`void *surface` | **Android  ABI 改变**：由 `{uint32_t id; void *display;}`（16B）变 `{void *surface;}`（8B）。桌面因 `__ANDROID__` 未定义，**布局字节不变**，无 ABI 破坏。 | 无（预处理分支不进入）；但公共头新增了 `ANativeWindow*` 知识（注释里），属知识性侵入。 |
| `libobs/obs.h:37-40` | `#ifdef __ANDROID__` 包 `#include "obs-android.h"` | **Android 侧公共 API 面扩大**：向所有 `obs.h` 消费者（含桌面编译单元在 Android 工具链下）暴露 ~40 个 `obs_android_*` 符号与 4 张回调表。桌面侧零影响。 | 桌面零影响；但违反 `CODESTYLE.md:286-287`（详见下）。 |

**结论**：对**桌面**编译产物（Windows/macOS/Linux）无 ABI/API 影响——所有公共头改动都在 `__ANDROID__` 或未定义分支内。真正的跨桌面影响来自下面两类无守卫改动。

## 无守卫改动对桌面 / Linux 路径的影响（lead 重点核实项）

| 文件:行号 | 改动 | 桌面/Linux 影响判定 | 证据 |
|---|---|---|---|
| `libobs/CMakeLists.txt:268-269` | 新增 `elseif(OS_ANDROID)` | **零影响**。桌面 `OS_ANDROID` 未定义，分支不进入；`OS_LINUX` 等仍按原顺序命中。 | `cmake/common/osconfig.cmake`（HEAD）中 `OS_ANDROID` 只在 `CMAKE_SYSTEM_NAME STREQUAL "Android"` 时置位 |
| `libobs/obs-display.c:258-260` | `if (render_display_begin(...))` → `const bool began = ...; if (began) {...}` | **行为逐字节等价**。`began` 仅用于该 `if`，函数体、副作用顺序、`gs_present()` 位置完全不变（`git show HEAD:libobs/obs-display.c` 与 `65c320b` 逐行比对确认）。零回归风险，但属**无意义 churn**，rebase 上游时会产生冲突。 | 两版 `render_display()` 对照 |
| `libobs/obs-service.c:418,427` | 两个 getter 补 `if (!obs_service_valid(service, ...)) return NULL;` | **会改变桌面行为，但是良性修复**：原实现直接解引用 `service->info.*`，传入 NULL / 已释放 service 会崩溃或 UB；新实现返回 NULL。同文件其余约 20 个 getter 全都有此校验（如 `obs-service.c:194,210,369,391,409,436`），这两处是上游 33.0.1 的遗漏。**合法调用零影响**；性质是「顺手修上游 bug」的 scope creep，宜单独提 upstream PR。 | `git show 65c320b:libobs/obs-service.c` 附近 getter 对照 |
| `libobs/data/deinterlace_base.effect:21-24,72,98-112,164-197` | 浮点除数/减数 `2`→`2.0`、`1`→`1.0`（约 13 处）+ 4 行块注释 | **语义等价，但桌面 shader 源码被改写**。HLSL 与桌面 GLSL 均允许 int→float 隐式提升；GLSL ES 3.0 不允许，这是改动动机（`deinterlace_base.effect:20-23` 注释自述）。`.effect` 是**全平台共用的数据文件**，由 `gs_effect_create_from_file()` 运行时编译 → Windows(D3D11)/macOS/GL(Linux) 都会用改后的源码重新编译。回归风险低但非零。 | `git diff upstream/master...HEAD -- libobs/data/deinterlace_base.effect` |
| `libobs/data/format_conversion.effect:571-598,606,616,619-626,676,736,741,767,779,814-818,1014-1055` | 三类无条件改动（详见下） | **桌面 shader 产物被实质改写**（返回类型 `float3`→`float4`、`Sample` 第三参数删除）。语义等价性需逐类论证，且**桌面无回归验收记录**。这是全范围最需标注的跨桌面影响。 | `git diff ... -- libobs/data/format_conversion.effect` |

`format_conversion.effect` 三类改动逐一核实：

1. **`.Sample(def_sampler, uv, 0)` → `.Sample(def_sampler, uv)`（7 处）**：上游 `libobs-opengl/gl-shaderparser.c:409-445` 把 `Sample` 的剩余参数原样写进 GLSL `texture(...)`，第三参数生成 `texture(s, uv, 0)`（桌面 GLSL 3.30 允许 int→float bias 隐式转换；GLSL ES 3.0 拒绝）。删除后桌面生成 `texture(s, uv)`（bias 0 为默认）→ **功能等价**。D3D11 侧对应 `Texture2D.Sample` 的 offset 重载，标量 `0` 与省略 offset 亦等价。证据：`git show 65c320b:libobs-opengl/gl-shaderparser.c:376-445`。
2. **`float3` 返回类型 → `float4` 且 `return rgb` → `return float4(rgb, 1.)`（11 个 `*_Reverse` / `*Limited` / `*Full` 函数）**：`gl-shaderparser.c:199-207` 用 `main->return_type` 生成 `out vec3/vec4 outputval;`，`:651-665` 再赋值；`libobs-d3d11/d3d11-shaderprocessor.cpp` 亦原样写返回类型。桌面由 `out vec3` 变 `out vec4`，**语义等价**（显式补 alpha=1.0 反而更符合 GLSL 规范——`out vec3` 的第 4 分量在规范中未定义），但确属对桌面 shader 输出签名的实质改写。**回归风险：低，需桌面真机回归**。
3. **`65535. / 4095` → `65535. / 4095.`（2 处，`:767,:779`）**：纯字面量，零影响。

> **最重要的文档缺口**：`PORTING.md` 共 780 行，`git grep -n -i "effect|shader|GLSL|着色|Adreno|2.0|float3|Sample" HEAD -- PORTING.md` **无任何相关命中**。也就是说这两处「影响桌面 shader 编译产物」的无守卫改动完全没有记载、没有验收判据、没有列入待办。相比之下文档对 Qt/APK/ccache 的记录极其详尽，这个反差本身就是风险。

## platform-nix.c / threading-posix.c / obs-service.c / obs-display.c 的 Linux/BSD 回归风险

| 文件 | Linux/BSD 行为 | 回归风险 |
|---|---|---|
| `libobs/util/platform-nix.c:29-31,1102,1112` | `#if !defined(__ANDROID__)` / `#ifndef __ANDROID__` 在 Linux/BSD 恒为真 → `<uuid/uuid.h>` 照常包含、`os_generate_uuid()` 照常编译。**字节级等价**。 | 无 |
| `libobs/util/threading-posix.c:264` | 由 `#elif defined(__GLIBC__) && !defined(__MINGW32__)` 改为 `#elif (defined(__GLIBC__) \|\| defined(__ANDROID__)) && !defined(__MINGW32__)`。Linux/glibc 下两个条件真值相同（`__ANDROID__` 未定义）→ **字节级等价**。 | 无 |
| `libobs/obs-service.c:418,427` | 见上，仅 NULL/失效入参路径变化（崩溃 → 返回 NULL）。 | 极低（良性） |
| `libobs/obs-display.c:258-260` | 纯重构，等价。 | 无 |
| `libobs/util/platform.c:800-810` | `#if defined(__ANDROID__) && __ANDROID_API__ < 29` 在 Linux/BSD 恒为假 → `#define obs_timespec_get timespec_get`，`init_timespec_offset()` 调用由 `timespec_get` 变为宏展开的 `timespec_get`，**字节级等价**。 | 无 |
| `libobs/obs-nix-platform.c:22-31` | Linux/BSD 走 `#else` 分支，初值仍 `OBS_NIX_PLATFORM_X11_EGL`，**字节级等价**。 | 无 |

**结论**：这 6 个共享 `.c` 的守卫改动对 Linux/BSD 编译产物与运行时行为均无回归；`obs-service.c` 与 `obs-display.c` 的无守卫改动中，前者是良性 bug 修复、后者是零行为重构。**唯一真正影响桌面产物的是两个 `.effect` 数据文件**。

---

# 是否符合上游多平台风格（逐项对照 verdict）

上游惯例的证据基线：

- `libobs/CMakeLists.txt`（65c320b）: `if(OS_WINDOWS) .../elseif(OS_MACOS) .../elseif(OS_LINUX) .../elseif(OS_FREEBSD OR OS_OPENBSD) ...`
- `libobs/cmake/os-linux.cmake:9-18` / `os-windows.cmake` / `os-macos.cmake`：各用 `target_sources(libobs PRIVATE ...)` 加入 `obs-nix.c`/`obs-windows.c`/`obs-cocoa.m`、`platform-nix.c`/`platform-windows.c`/`platform-cocoa.m`、`threading-posix.c`/`threading-windows.c`。
- `libobs/util/platform.c`（65c320b）：**只有 `_WIN32` / `_MSC_VER` 守卫**，其余平台差异全部下沉到 `platform-*.c`。
- `libobs/obs-nix-platform.c`（65c320b）：**零平台守卫**，一个 `static enum` + 一个 `void*`，提供运行期 `obs_set_nix_platform()` API。
- 公共头：`obs.h`（65c320b）**从不用宏包裹 `#include`**；`graphics/graphics.h:482` 只在 `struct gs_window` 内部按 `_WIN32/__APPLE__/__linux__||__FreeBSD__` 选成员。
- `CODESTYLE.md:286-287`：*"Do not create platform specific "include blocks"… **Instead of using a macro to conditionally compile code ... well, don't do that at all**… Refactor the source file to be platform-specific and use CMake to add the appropriate file to the target for a specific platform."*；`:285` 要求 include 块内字母序。`.clang-format:84` `SortIncludes: false`。

| 对照项 | Verdict | 证据与理由 |
|---|---|---|
| **C1. CMake 按 OS 选择 `os-*.cmake`** | ✅ **符合** | `libobs/CMakeLists.txt:265-270` 沿用上游 `if/elseif` 链，只在 `OS_MACOS` 与 `OS_LINUX` 之间插入 `elseif(OS_ANDROID)`；`cmake/common/osconfig.cmake:9-12` 用同一套 `set(OS_ANDROID TRUE)` 并同样 `list(APPEND CMAKE_MODULE_PATH .../cmake/android)`。命名 `os-android.cmake` 与 `os-linux.cmake`/`os-windows.cmake`/`os-macos.cmake` 一致。 |
| **C2. `os-*.cmake` 里用 `target_sources` 按平台选源文件** | ✅ **符合（做得好）** | `libobs/cmake/os-android.cmake:16-28` 与 `os-linux.cmake:9-18` 同构；新增 `obs-android.c`（对应 `obs-nix.c`）、`util/platform-android.c`（对应 `platform-*.c`），并**原样复用** `platform-nix.c`/`threading-posix.c`/`pipe-posix.c`/`obs-nix-platform.c`——正是上游「bionic 是 POSIX，能复用就别重写」的分层精神。`os-android.cmake:2-8` 的注释把复用/替换理由写得很清楚。 |
| **C3. 平台后端文件命名 `obs-<platform>.c` / `platform-<platform>.c`** | ✅ **符合** | `obs-android.c`、`util/platform-android.c`、`obs-core-modules-loader-android.c` 与 `obs-nix.c`、`platform-nix.c`、`obs-core-modules-loader-nix.c` 一一对应。 |
| **C4. 上游 core module loader 的组织** | ⚠️ **部分符合** | 上游把 3 个 loader 放在 `libobs/CMakeLists.txt` 的 `$<PLATFORM_ID:Darwin/Windows/Linux,FreeBSD,OpenBSD>` 生成器表达式里（`git show 65c320b:libobs/CMakeLists.txt` 的 `target_sources` 段）。Android 的 `PLATFORM_ID` 为 `Android`，不匹配任何一支 → 必须新增 loader，`obs-core-modules-loader-android.c:32-35` 的自述**属实**。但移植把它塞进 `os-android.cmake` 的 `target_sources`，与上游「loader 选择集中在 CMakeLists.txt」的组织不一致。属轻度偏差，功能正确。 |
| **C5. 公共头 `graphics.h` 的平台守卫惯例** | ✅ **符合** | `graphics/graphics.h:482-495` 上游本就是 `#if _WIN32 / #elif __APPLE__ / #elif __linux__||__FreeBSD__` 的成员选择链；Android 新增的 `#elif defined(__ANDROID__)` 是同一模式的自然延伸，且**必须排在 `__linux__` 之前**（NDK clang 同时定义 `__linux__`），`graphics.h:486-488` 注释正确。 |
| **C6. 公共头 `obs.h` 不包宏 include** | ❌ **不符合** | `libobs/obs.h:37-40` 新增 `#ifdef __ANDROID__` + `#include "obs-android.h"` + `#endif`——正是 `CODESTYLE.md:286-287` 明文禁止的「platform specific include block」。上游 `obs.h` 从无此写法。更关键：**这层包含是冗余的**——4 个 Android 插件都已直接 `#include <obs-android.h>`（`plugins/android-audio/aaudio-input.c:24`、`android-camera/camera2-input.c:63`、`android-capture/uvc-input.c:55`、`android-screen/screen-capture.c:63`）。详见 P1-1。 |
| **C7. 平台判断是否散落到不该出现的地方** | ⚠️ **部分不符合** | 6 个共享上游文件被插入 `__ANDROID__`（详见下）：其中 `threading-posix.c` 属既有守卫链的最小扩展（✅），`graphics.h` 属既有 struct 守卫链（✅）；**`obs.h`、`platform-nix.c`、`obs-nix-platform.c`、`platform.c` 4 处是新增类别的散落**（❌）。上游 `platform.c` 只有 `_WIN32`/`_MSC_VER`，其余全在 `platform-*.c`；移植打破了这条分层。 |
| **C8. `__ANDROID__` 与上游惯用宏的一致性** | ✅ **一致，但有一处粒度混杂** | 上游 C 侧惯用 `_WIN32`/`__APPLE__`/`__linux__`/`__FreeBSD__`；Android NDK 同时定义 `__linux__` 与 `__ANDROID__`。移植统一用 `__ANDROID__`（lead 确认全仓库 `Q_OS_ANDROID` 为 0，Qt 前端也一律 `__ANDROID__`），并在 `graphics.h:485` 正确处理了与 `__linux__` 的优先级。**例外**：`util/platform.c:800` 引入 `__ANDROID_API__ < 29` 这一「API 等级」判断，与上游按 OS 宏分层的粒度不同，且按 `PORTING.md:20` minSdk=29 该分支恒为假（死代码）。 |
| **C9. 不要动共享 effect 数据文件来适配单平台着色器** | ❌ **不符合** | `libobs/data/*.effect` 是全平台共用资源。上游从不为单个后端改这两份文件（`git diff` 显示 33.0.1 原版）；移植做了 46 行无条件改写（`deinterlace_base.effect` +17/-13、`format_conversion.effect` +29/-29），桌面 shader 源码随之改变。更优雅的做法是 Android 专属副本（见方案 F）。 |

**总体 verdict**：**CMake 分层与新增后端文件是「符合上游惯例」的，且是整个移植里最干净的部分**；**不符合集中在 4 处共享文件的 `#ifdef` 散落 + 2 处无守卫的 effect 数据文件改写**。侵入性绝对量小（上游文件仅 +75/-45），但性质上偏「在通用文件里塞补丁」而非「新增独立后端」，与 `CODESTYLE.md:286-287` 的指导思想有落差。`PORTING.md:706` 自己也承认约 24 处平台 include 块违规，本范围内确认 2 处（`obs.h:37`、`platform-nix.c:29`），却未列入 `PORTING.md:774` 的待办清单。

---

# 问题清单

## P0

**无确证的 P0**。两个 `.effect` 的无守卫改动虽然影响桌面 shader 产物，但经源码级论证语义等价、CI 编译通过，不构成发版阻塞；`obs-android.c` 的并发 UB 只在「重注册 + 并发读」这一被文档明确排除的用法下触发。最严重问题定级 P1。

## P1

### P1-1 `libobs/obs.h:37-40` — 公共头里的平台 include 块，违反 CODESTYLE 且冗余

```c
#ifdef __ANDROID__
/* 无 root 的 USB 通路：App 注册回调、采集插件通过 libobs 取 fd（见 obs-android.h） */
#include "obs-android.h"
#endif
```

- **影响**：① `CODESTYLE.md:286-287` 明文禁止「platform specific include blocks」，并要求「把源文件重构成平台专属、由 CMake 选」；`PORTING.md:703-711` 也把这条列为「已知未遵守」。② 注释只说了「USB 通路」，但 `obs-android.h` 实际含 4 张总线（USB/音频/相机/屏幕）共 ~40 个 `EXPORT` 声明，注释与内容不符。③ 冗余：4 个插件都已直接包含该头，仅 `frontend/widgets/OBSAndroidUsb.cpp:5`、`OBSAndroidPermissions.cpp`、`OBSBasic.cpp` 依赖这层传递包含。④ 把 Android 专有符号挂进 `obs.h` 这一最核心公共头，未来上游若新增同名符号会冲突。
- **改进建议**：删除该 `#ifdef` 块；给上述 3 个前端 `.cpp` 各补一行 `#include <obs-android.h>`。迁移代价 3 行，桌面零影响，Android 插件侧零改动。详见方案 A。

### P1-2 `libobs/data/deinterlace_base.effect` / `libobs/data/format_conversion.effect` — 无守卫改动影响桌面 shader，且文档零记载

- **影响**：两个文件是全平台共用数据文件（运行时由 `gs_effect_create_from_file()` 编译），46 行无条件改写使 **Windows(D3D11)/macOS/Linux(GL) 的着色器源码与 33.0.1 基线不再字节一致**：
  - `format_conversion.effect:571,578,619,676,736,741,814,1014,1021,1034,1043` 等把 11 个 `TARGET` 函数返回类型 `float3` 改成 `float4`（`gl-shaderparser.c:199-207,651-665` 会用 `main->return_type` 生成 `out vec3/vec4 outputval`）；
  - 同文件 7 处 `.Sample(def_sampler, uv, 0)` → `.Sample(def_sampler, uv)`（`gl-shaderparser.c:409-445` 把第三参数原样写入 GLSL `texture()`）；
  - `deinterlace_base.effect` 约 13 处 `/ 2` → `/ 2.0`、`- 1` → `- 1.0`。
  经论证语义等价（bias 0 为默认、`float4(rgb,1.)` 补的是未定义的 alpha、int→float 隐式提升），**回归风险低但非零**，且**没有任何桌面回归验收记录**——`PORTING.md` 780 行 `git grep -i "effect|shader|GLSL|Adreno"` 零命中。此外每次 rebase 上游 effect 都会冲突。
- **改进建议**：① 至少在 `PORTING.md` 补一节，逐条记录三类改动、等价性论证与桌面回归验收判据；② 若要求「桌面产物与上游字节一致」，把 GLSL ES 兼容副本放到 Android 专属 `data-android/` 并由 `find_libobs_data_file()` 优先查找（方案 F）；③ 提交 upstream 时把 `2.0`/`float4(rgb,1.)` 这类「对桌面也正确」的写法单独提 PR，让上游吸收，从根上消除 diff。

### P1-3 `libobs/obs-android.c:61-82` — `add_default_module_paths()` 是死代码，且 `PORTING.md:281` 声明有误

- **影响**：该函数无原型（全仓库 `git grep -n add_default_module_paths HEAD` 只命中 `PORTING.md:281`、`PluginModuleLoader_Android.cpp:48`、`AndroidRuntimeBootstrap.hpp:21` 三处**注释** + `obs-android.c:61` 定义），**无任何调用者**；上游 33.0.1 也不存在该函数（`git grep add_default_module_paths 65c320b` 无结果）。实际路径注册发生在 `frontend/plugin-manager/PluginModuleLoader_Android.cpp:79-86`：构造 `ModuleLoadInfo` 后直接 `obs_load_plugins()`，`obs_add_module_path()` 在 Android 前端从未被调用。于是 `obs-android.c:73-81` 里的 `dstr` 拼接、`ROOT_PATH_ENV` 判空错误日志全部是不可达代码；`PORTING.md:281-282`「路径由 libobs/obs-android.c 的 add_default_module_paths() 注册」**属有误陈述**，会误导后续维护者去改一个不生效的函数。
- **改进建议**：删除 `add_default_module_paths()`（或改为导出 `obs_android_get_root_path()` 供 loader 复用，见 P2-4）；同时修正 `PORTING.md:281-282`、`PluginModuleLoader_Android.cpp:48`、`AndroidRuntimeBootstrap.hpp:21` 三处注释，指向真实的 `PluginModuleLoader_Android.cpp:79-86`。

### P1-4 `libobs/obs-android.c:272-285,349-361,406-418,461-473` — 四张 host 表的并发写构成 C11 数据竞争（UB）

- **影响**：以 USB 为例，`static struct obs_android_usb_host usb_host_store;` 是**普通静态对象**，`static _Atomic(const struct obs_android_usb_host *) usb_host;` 是原子指针。setter 先 `usb_host_store = *host;`（普通写）再 `atomic_store_explicit(&usb_host, &usb_host_store, memory_order_release)`；读侧（`obs-android.c:294`、`318`、`328`、`336` 等）`const ... *h = usb_host;` 后解引用 `h->get_devices`。若发生**重注册**（或注销后重注册）与并发读，对 `usb_host_store` 的普通写与读之间没有 happens-before，构成 **C11 未定义行为**，而不只是 `obs-android.c:270-271` 所说的「不保证看到一致的快照」。指针本身确已原子化（`obs-android.c:265-268` 的修正是对的），但**被指向的结构体内容没有**。
  另：`obs-android.c:254-257,347-348,404-405,459-460` 的注释仍写「等价于 relaxed store/load」「借用指针」，与 `obs-android.c:258-271` 的新版说明（按值拷贝 + 原子指针）**自相矛盾**，且读侧实际是 seq_cst 隐式加载而非 relaxed。
- **改进建议**：① 文档层面把「重注册 + 并发读」明确列为**未定义行为**而非「不支持的用法」；② 实现层面用「不可变快照 + 引用计数/RCU」或「双缓冲 + 原子索引」，或最简单：在 `obs-android.h` 的线程约定里禁止运行期重注册，setter 仅在 `obs_startup()` 之前/之后单线程调用一次，并把 `usb_host_store` 改为 `const` 初始化后不再写；③ 统一四段注释，删除残留的「relaxed store/load」「借用指针」表述，读侧改用 `atomic_load_explicit(&usb_host, memory_order_acquire)` 与 release 配对。

## P2

### P2-1 `libobs/util/platform.c:800-810` — `timespec_get` shim 在 minSdk=29 下是死代码

```c
#if defined(__ANDROID__) && __ANDROID_API__ < 29
static int obs_timespec_get(...) { ... clock_gettime(...) ... }
#else
#define obs_timespec_get timespec_get
#endif
```

- **影响**：`PORTING.md:20` 明确 minSdk=29，`__ANDROID_API__ >= 29` → 该分支恒不生效，编译产物里只剩 `#define obs_timespec_get timespec_get` 这一宏别名，把一个新符号名泄漏进共享上游文件；`init_timespec_offset()` 的调用点也被无谓改写。**3 条条件编译指令 + 12 行全为无效侵入**。且与上游 `platform.c` 只用 `_WIN32`/`_MSC_VER` 的分层惯例不符（见 C7）。
- **改进建议**：直接删掉整段 shim，恢复上游单行 `timespec_get(&timespec_offset.ts, TIME_UTC)`；若将来要支持 API<29，把 `os_timespec_get()` 放进 `util/platform-android.c`（由 CMake 选择），不要在共享文件里塞 `#if`。代价 0，收益：少 3 条指令、少 1 个宏、少 1 处 rebase 冲突点。

### P2-2 `libobs/util/platform-nix.c:29-31,1102,1112` — 用宏包裹 `#include`，且依赖"互斥编译"隐式契约

```c
#if !defined(__ANDROID__)
#include <uuid/uuid.h>
#endif
...
#ifndef __ANDROID__
char *os_generate_uuid(void) { ... }
#endif
```

- **影响**：`CODESTYLE.md:286` 禁止「用宏条件编译代码」，`#include` 块尤其点名；`os_generate_uuid()` 的唯一性靠「`platform-nix.c` 屏蔽 + `platform-android.c` 实现」的隐式互斥契约保证，一旦 rebase 漏掉守卫或 CMake 同时加入两份就是 duplicate symbol，没有链接期断言。Linux/BSD 行为无变化（守卫恒真）。
- **改进建议**：见方案 C（拆分 `platform-nix-uuid.c`）。**但需如实权衡**：拆分要改动上游 `platform-nix.c`（移动 12 行），反而增加上游 diff；若以「少改上游文件」为优先，保留现状更简单，只需在 `platform-android.c:22-23` 补一句契约说明。

### P2-3 `libobs/obs-nix-platform.c:22-31` — 上游零守卫文件被塞 `#if defined(__ANDROID__)`

- **影响**：改的是 `static enum obs_nix_platform_type obs_nix_platform` 的初值（Android 用 `OBS_NIX_PLATFORM_INVALID` 而非 `X11_EGL`）。逻辑上正确（`OBSBasic.cpp:1206` 的 `isWayland`、`importers/studio.cpp:115` 的 X11 判断都会读到非 X11），注释也论证充分。但：① 该文件上游从无平台守卫；② `obs_set_nix_platform()` 里的 `assert(platform != OBS_NIX_PLATFORM_INVALID)` 目前靠「Android 没人调它」这一**隐含前提**才不炸（`obs-nix-platform.c:36`），一旦未来有代码调用就崩；③ 让一个「Unix 平台 API」文件承担 Android 默认值，语义别扭。
- **改进建议**：见方案 B（新增 `obs-nix-platform-android.c` 并返回 INVALID，由 `os-android.cmake` 选它），上游文件可完全还原。

### P2-4 `libobs/obs-core-modules-loader-android.c:59` 与 `libobs/obs-android.c:42` — `OBS_ROOT_PATH` 字面量重复

- **影响**：两处各写一次 `getenv("OBS_ROOT_PATH")`，`obs-android.c:51-59` 的 `get_root_path()` 是 `static` 且有缓存（含判空），loader 用不了只能重复；两份路径拼接逻辑（`obs-android.c:76-77` 与 `loader:69-77`）也各写一遍。若 env 名或布局变更需改两处。
- **改进建议**：在 `obs-android.h` 导出 `const char *obs_android_get_root_path(void)`（内部仍用缓存的 `dstr`），loader 与 `find_libobs_data_file()` 共用；路径模板收敛成一个 `obs_android_module_paths()` 辅助函数。约 10 行改动。

### P2-5 `libobs/obs-core-modules-loader-android.c:98-99` — 「核心模块装载 X/Y」日志语义错误

- **影响**：`loaded` 只在 `find_core_module(&info, callback, data)` 返回 true 时自增。而上游 `find_core_module()`（`git show 65c320b:libobs/obs-module.c`）是 `if (os_file_exists(module_path.array)) { callback(...); found = true; }` —— **只要 `.so` 存在就返回 true，不代表加载成功**；真正失败在 `load_all_callback()` 里递增 `fail_info->fail_count`。所以 `obs-core-modules-loader-android.c:98` 打印的「核心模块装载：%d/%u」实为「找到文件数」，`loaded == 0`（`:100`）只覆盖「一个文件都没找到」，不覆盖「全找到但全部 `obs_open_module` 失败」。同时 `obs_load_core_modules()` 的返回值只看 `fail_count`，与 `loaded` 无关。
- **改进建议**：把计数改名为 `found`，日志改「核心模块 locate：%d/%u」；另用 `blog(LOG_ERROR)` 汇总 `fail_count` 或至少提示「加载结果以 obs_load_core_modules() 为准」。

### P2-6 `libobs/obs-core-modules-loader-android.c:88-95` — 缺失核心模块时降级为 WARNING 且继续，与上游相反

- **影响**：上游 `obs-core-modules-loader-nix.c` 遇到 `!found` 是 `blog(LOG_ERROR, "Failed to load core module %s", name); break;`。Android 版刻意「不 break、不计失败」，让 `loadPlugins()` 稍后以 `OBS_MODULE_TYPE_PLUGIN` 重试。后果：一个缺失/解包失败的必需 core 模块会**降级成普通插件出现在插件管理器里、且不再免疫用户 disabled 配置**——这与 `PORTING.md:298`「core 留给随包发布的必需功能，禁用会直接把应用弄残」的设计目标冲突，也意味着 `obs_load_core_modules()` 仍返回 true（因 `find_core_module()` 未调用 callback，`fail_count` 不增）。注释（`:88-91`）虽写明理由，但把「降级」当成可接受的常态掩盖了「插件没解包出来」这类真实故障。
- **改进建议**：至少升级为 `LOG_ERROR` 并在 `loaded == 0` 时给出可操作诊断（已做，`:100-105`）；对**单个**缺失也打 ERROR。若坚持降级语义，需在 `PORTING.md` 明确写出「core 缺位会降级为可禁用插件」这一行为及其后果。

### P2-7 `libobs/obs-core-modules-loader-android.c:49` / `frontend/plugin-manager/PluginModuleLoader_Android.cpp:38` — 「14 个」数量有误

- **影响**：两处注释称 `obs_core_modules[]` 是「14 个白名单插件 / 内置插件」。实际 `plugins/CMakeLists.txt` 的 `if(OS_ANDROID)` 分支只有 **13** 个 `add_subdirectory`（`:52,53,54,55,56,57,58,59,60,84,98,126,135`），`list(REMOVE_ITEM ... android-audio android-camera android-capture android-screen)` 剔除 4 个后 core 为 **9** 个——与 `PORTING.md:295`「8 个上游插件 + obs-libfdk」及 `PORTING.md:733`「实测 13 个插件、0 失败」一致。源码注释与文档互相矛盾，会让维护者误判清单规模。
- **改进建议**：两处「14」改为「13（其中 4 个为 PLUGIN、9 个为 CORE）」，或干脆不写死数字（改为引用 `obs_core_modules_count`）。

### P2-8 `libobs/obs-android.c:34-40` / `:76` / `obs-core-modules-loader-android.c:76` — 路径约定文档与实现不一致

- **影响**：`obs-android.c:37` 的块注释写 `$OBS_ROOT_PATH/lib/<abi>/`，但 `obs-android.c:76`（`"%s/lib"`）、`obs-core-modules-loader-android.c:76`（`"/lib/"`）、`PluginModuleLoader_Android.cpp:79`（`+ "/lib"`）实际都是 `$ROOT/lib`，**没有 `<abi>` 子目录**。`PORTING.md:270` 与 `:282` 也都写 `$OBS_ROOT_PATH/lib`。注释会误导打包脚本作者。
- **改进建议**：删掉 `<abi>/` 这一层，或说明 ABI 目录只用于系统 linker 的 `nativeLibraryDir`（与 `OBS_ROOT_PATH` 不是同一棵目录树）。

### P2-9 `libobs/obs-android.c:84-106` — `find_libobs_data_file()` 失败时静默回退到桌面路径

- **影响**：`get_root_path()` 为 NULL 时不打日志，直接 `check_path(file, OBS_DATA_PATH "/libobs/", &output)`；`OBS_DATA_PATH` 在 Android 是编译期绝对前缀（不存在），最终返回 NULL 且无任何诊断。上游 `obs-nix.c:find_libobs_data_file()` 有 3 级回退（`OBS_DATA_PATH`、相对 exe、`OBS_INSTALL_DATA_PATH`），Android 版少了后 2 级。启动期若 `OBS_ROOT_PATH` 未设置，故障表现为「effect 加载失败」而非根因。
- **改进建议**：与 `add_default_module_paths()` 一样在 `!root` 时打 `LOG_ERROR`；或把 `OBS_INSTALL_DATA_PATH` 回退也补上。

### P2-10 `libobs/util/platform-android.c:18-38` — 头文件自包含与注释笔误

- **影响**：① 用到 `snprintf` 但未直接 `#include <stdio.h>`，靠 `platform.h` 间接提供（`platform.h` 确实含 `<stdio.h>`），不符 `CODESTYLE.md:242`「include what you use / 空实现也要能编译」；`arc4random_buf` 由 `<stdlib.h>` 提供（已含，OK）。② `:22-23` 注释写「platform-nix.c hides its **uuid_generate()** version」，上游函数名实为 `os_generate_uuid()`（`platform-nix.c:1102`），属笔误。③ `os_generate_uuid()` 未校验 `bmalloc(37)` 返回值（上游同样不校验，可接受）。
- **改进建议**：补 `#include <stdio.h>`；`uuid_generate()` 改为 `os_generate_uuid()`。

### P2-11 `libobs/obs-android.c` / `libobs/obs-core-modules-loader-android.c` — 中文日志与上游 libobs 全英文惯例不一致

- **影响**：`obs-android.c:66-69,283-284,360,417,472`、`obs-core-modules-loader-android.c:61,92-94,98-104` 等大量中文 `blog(...)`/`CORE_LOG(...)`；上游 libobs 日志一律英文。`PORTING.md:758-759` 自己也把「探针日志 63 处、约 21 处是每事件残留」列为工程债，建议加 `OBS_ANDROID_PROBE` 开关并把长期项降到 `LOG_DEBUG`。本范围内 4 条总线注册日志（`obs-android.c:283,360,417,472`）用 `LOG_INFO` 偏吵。
- **改进建议**：与全仓库探针日志治理一并处理；至少把总线注册/注销日志降为 `LOG_DEBUG` 或统一英文。

### P2-12 `libobs/obs-display.c:258-260` / `libobs/obs-service.c:418,427` — 无守卫的 scope creep

- **影响**：`obs-display.c` 是零行为的纯重构（无功能收益），`obs-service.c` 是顺手修上游 bug。两者都与 Android 移植无因果关系，却让上游文件产生永久 diff，每次 rebase 都可能冲突；`obs-service.c` 还改变了桌面在非法入参下的行为（崩溃 → 返回 NULL），虽属良性但应可追溯。
- **改进建议**：`obs-display.c` 直接还原上游写法；`obs-service.c` 的两行校验单独整理成 upstream PR，若不能提交则在 `PORTING.md` 补记「顺带修复上游两处 getter 空指针」。

---

# 更优实现方案（含代价）

按「收益 / 代价」排序，均给出具体改动点。

## 方案 A（强烈推荐，代价最小）— 删除 `obs.h` 的平台 include 块

- **改动**：删 `libobs/obs.h:37-40`；给 `frontend/widgets/OBSAndroidUsb.cpp:5`、`frontend/widgets/OBSAndroidPermissions.cpp`、`frontend/widgets/OBSBasic.cpp` 各补 `#include <obs-android.h>`（3 行）。4 个插件已直接包含，无需动。
- **代价**：3 行；前端需重新编译验证（纯 include，无链接变化）。
- **收益**：公共头恢复上游形态；消除 `CODESTYLE.md:286` 违规；Android 专有声明不再从 `obs.h` 泄漏到每个消费者；桌面编译单元少一层条件包含。
- **风险**：低。需确认 `OBSBasic.cpp` 确实用到 `obs_android_*`（`git grep` 已确认注释提及，**具体符号需前端 owner 复核**）。

## 方案 B（推荐）— `obs-nix-platform.c` 还原，Android 用独立实现文件

- **改动**：新增 `libobs/obs-nix-platform-android.c`（实现 `obs_set_nix_platform` / `obs_get_nix_platform` / `obs_set_nix_platform_display` / `obs_get_nix_platform_display` 四个函数，`obs_get_nix_platform()` 恒返回 `OBS_NIX_PLATFORM_INVALID`）；`libobs/cmake/os-android.cmake:19` 把 `obs-nix-platform.c` 换成 `obs-nix-platform-android.c`；还原 `libobs/obs-nix-platform.c` 到 65c320b 原版。
- **代价**：新增约 40 行文件 + 1 行 CMake；需确认 `obs-nix-platform.h` 的 `EXPORT` 符号仍唯一。
- **收益**：上游文件 100% 还原（0 处 `#ifdef`）；Android 语义显式化；不再依赖「Android 没人调 `obs_set_nix_platform()`」这一隐含前提（现有 `obs-nix-platform.c:36` 的 `assert` 可保留但不影响 Android）。

## 方案 C（可选，需权衡）— 拆分 uuid 实现，消除 2 处违规

- **改动**：把 `platform-nix.c:1102-1112` 的 `os_generate_uuid()` 与 `:30` 的 `#include <uuid/uuid.h>` 移到新文件 `libobs/util/platform-nix-uuid.c`；`os-linux.cmake` / `os-freebsd.cmake` 各加 1 行把它加入 `target_sources`；`os-android.cmake` 不加；还原 `platform-nix.c` 的两处 `#if`。
- **代价**：改动上游 `platform-nix.c`（移动 12 行，**反而增加上游 diff**）+ 2 个 `os-*.cmake` 各 1 行 + 新增 1 个文件。
- **收益**：消除 `CODESTYLE.md:286` 的 2 处违规、消除「互斥编译」隐式契约。
- **权衡结论**：**若以「最小化上游 diff」为优先，保留现状的 `#ifndef __ANDROID__` 反而更划算**——并非所有 `#ifdef` 都值得拆。建议只做方案 A/B，方案 C 记为技术债。

## 方案 D（零代价）— 删掉 `timespec_get` 兼容 shim

- **改动**：删 `libobs/util/platform.c:800-810`，恢复 `timespec_get(&timespec_offset.ts, TIME_UTC)`。
- **代价**：0（minSdk=29 已保证 `timespec_get` 可用，`PORTING.md:20`）。
- **收益**：少 3 条条件编译指令、少 1 个宏、少 1 个死分支、`platform.c` 恢复「只有 `_WIN32` 守卫」的上游分层。若未来要支持 API<29，改用 `platform-android.c` 提供 `os_timespec_get()` 由 CMake 选。

## 方案 E（架构级，不建议当前阶段做）— 平台函数表 / 弱符号

- **设想**：定义 `struct obs_platform_backend { add_default_module_paths, find_libobs_data_file, log_system_info, hotkeys_init/free/is_pressed, key_to_str, key_from_virtual_key, key_to_virtual_key, get_module_extension, os_generate_uuid }`，由 `os-*.cmake` 选一个 backend `.c` 注册到全局指针，`obs.c` / `obs-internal.h` 调用点改走函数表。
- **代价**：需改上游调用点 + 新增约 200 行胶水 + 全平台回归；会**显著扩大与上游的 diff**。
- **收益**：共享文件彻底零 `#ifdef`，新增平台只需加一个 backend 文件。
- **结论**：上游本身并没有这套抽象（它用「平台专属 .c 文件 + CMake 选源文件」，已经足够），引入函数表属过度设计，与「尽量贴上游」的移植目标冲突。**不推荐**。

## 方案 F（针对 P1-2，代价中等）— effect 的 Android 专属副本

- **改动**：把 GLSL ES 兼容版另存为 `libobs/data-android/{deinterlace_base,format_conversion}.effect`（或只在构建期生成）；`libobs/obs-android.c:84-106` 的 `find_libobs_data_file()` 在 Android 上优先查 `$ROOT/share/libobs-android/`；桌面 `libobs/data/*.effect` **完全还原上游字节**。
- **代价**：新增 2 个 effect 副本（约 1000 行重复，需维护双份）+ 构建/查找逻辑；或改成「构建期用 sed 从上游生成」，则引入构建期文本处理。
- **收益**：桌面 shader 产物 100% 与 33.0.1 一致；上游 effect 文件零 diff，rebase 无冲突；GLSL ES 差异被隔离在 Android 侧。
- **权衡**：现有改动语义等价且 CI 编译通过，若接受「桌面回归低风险 + 补文档 + 补桌面回归验收」，维持现状更省。**建议至少先做「补文档 + 提 upstream PR」这两步**，把方案 F 作为「要求桌面产物字节一致时」的备选。

## 迁移代价汇总

| 方案 | 改动文件数 | 新增行 | 还原上游文件 | 建议 |
|---|---:|---:|---:|---|
| A | 4（1 删 + 3 加 include） | ~3 | `obs.h` | ✅ 立即做 |
| B | 3（新增 1 + CMake 1 + 还原 1） | ~40 | `obs-nix-platform.c` | ✅ 做 |
| D | 1（删 12 行） | -12 | `platform.c` | ✅ 立即做 |
| C | 4 | ~15 | `platform-nix.c`（但要移动代码） | ⚠️ 记为债 |
| F | 3~4 | ~1000 | 2 个 effect | ⚠️ 视桌面回归要求 |
| E | 多 | ~200 | 无 | ❌ 不做 |

---

# 实现亮点

1. **CMake 分层几乎完全贴合上游**：`libobs/CMakeLists.txt:268-269` + `cmake/common/osconfig.cmake:9-12` + `libobs/cmake/os-android.cmake:16-28` 三处组合，与上游 `os-linux.cmake` / `os-windows.cmake` / `os-macos.cmake` 的组织同构；`os-android.cmake:2-8` 的注释明确论证了「bionic 是 POSIX，能复用 `platform-nix.c`/`threading-posix.c`/`pipe-posix.c` 就不重写」，并解释了为何 `obs-nix-platform.c` 必须保留（前端 `obs_get_nix_platform()` 的链接依赖）。这是整个移植最干净的部分。

2. **`graphics.h` 的宏优先级处理正确**：`graphics/graphics.h:485-489` 把 `#elif defined(__ANDROID__)` 放在 `#elif defined(__linux__) || defined(__FreeBSD__)` **之前**，注释点明「NDK clang 同样定义了 `__linux__`，否则会静默掉进 X11 分支拿 `uint32_t id`」。这是 Android 移植最容易踩的坑，处理得对。

3. **`threading-posix.c:264` 是「最小侵入」的范本**：只在既有 `#elif defined(__GLIBC__) && !defined(__MINGW32__)` 链上追加 `|| defined(__ANDROID__)`，1 行改动，没有引入新 `#ifdef` 块；不加这一行 `os_set_thread_name()` 在 Android 上会变成空函数体（bionic 既非 `__APPLE__`/`__FreeBSD__`/`__GLIBC__`）。

4. **`obs-core-modules-loader-android.c` 机制理解到位**：`load_core_modules(obs_find_module_callback2_t, void*)` 签名、`extern bool find_core_module(...)`、`OBS_MODULE_TYPE_CORE`、`.path_info = {.binary, .data}` 结构与 `obs-core-modules-loader-nix.c` 一致；`%module%` 替换由 `obs-module.c:make_data_dir()` 保证（`git show 65c320b:libobs/obs-module.c` 确认 `dstr_replace(parsed_data_dir, "%module%", name)`）；`find_core_module()` 内部会补 `get_module_extension()`（`.so`），loader 传的 binary 不带扩展名是对的。core 免疫 disabled（`load_all_callback` 中 `info->type != OBS_MODULE_TYPE_CORE && is_disabled_module(...)`）、重复项被 "already loaded" 跳过——与 `PORTING.md:286-289` 的自述一致。

5. **`obs-android.c` 的防御性细节质量高于一般移植代码**：
   - 枚举结果强制 NUL 兜底（`obs-android.c:309-312,384-387`），防 Java 侧截断出非 NUL 结尾字符串；
   - 出参先清零再问（`obs-android.c:519-524,560-563`），让「忘记判返回值」退化成「尺寸/采样率不成立」这条更早更响的失败；
   - fail-open / fail-closed 语义逐格论证后果不对称（`obs-android.c:443-453` 相机 `capture_allowed` fail-open vs `:480-489` 屏幕 `has_consent` fail-closed）；
   - ABI 用编译期宏而非运行期 `ro.product.cpu.abi`（`obs-android.c:139-155`），注释说明模拟器会改写运行期值；
   - 把借用指针改成按值拷贝 + 原子指针（`obs-android.c:258-271`），修正了原本的悬垂指针与非原子并发读写（虽仍留 P1-4 的残余问题，但方向正确且注释诚实）。

6. **`util/platform-android.c` 简洁正确**：`arc4random_buf` + 手工置 version 4 / variant `10xx` 位 + `bmalloc(37)` + `snprintf`，输出格式与 `platform-nix.c` 的 `uuid_unparse_lower` 一致；与 `platform-nix.c` 的 `#ifndef __ANDROID__` 形成互斥，链接期只有一个 `os_generate_uuid()`。

7. **`obs-service.c:418,427` 补的校验本身是好的**：同文件其余约 20 个 getter 都有 `obs_service_valid()`，这两处是上游遗漏，补上后与文件内惯例一致（只是属于 scope creep，见 P2-12）。

---

# PORTING.md 声明核验表

| # | 声明（`PORTING.md` 行号） | 判定 | 证据 |
|---|---|---|---|
| V1 | `:280` 「装载器不再用编译期白名单：就是 `obs_load_plugins()` + `os_glob($ROOT/lib/*.so)`」 | **属实（有限定）** | `PluginModuleLoader_Android.cpp:79-86` 直接构造 `ModuleLoadInfo` 调 `obs_load_plugins()`（经 `loadPluginsByInfo`，`PluginModuleLoader.hpp:53`）；`obs_load_plugins()` → `find_modules_in_path()` → `os_glob()`。**但 core 仍走 `obs_core_modules[]` 编译期白名单**（`obs-core-modules-loader-android.c:72`），「不再用白名单」只对 PLUGIN 成立 |
| V2 | `:281-282` 「路径由 `libobs/obs-android.c` 的 `add_default_module_paths()` 注册，模板与桌面一致」 | ❌ **有误** | `obs-android.c:61` 定义的函数无原型、全仓库无调用者（`git grep -n add_default_module_paths HEAD` 仅 3 处注释 + 1 处定义）；上游 65c320b 无此函数（`git grep ... 65c320b` 空）。实际注册在 `PluginModuleLoader_Android.cpp:79-86`。**模板本身**（`bin=$ROOT/lib`、`data=$ROOT/share/obs/obs-plugins/%module%`）与 `obs-android.c:76-77` 一致 ✔ |
| V3 | `:272` 「与 Linux/BSD 完全相同的『扫目录』装载」 | ✅ **属实** | `obs_load_plugins()` 的 `find_modules_in_path()`/`os_glob()` 路径在 libobs 侧未被修改（`git diff ... -- libobs/obs-module.c` 无输出） |
| V4 | `:286-289` `obs_is_core_module()` / `obs-module.c:502` core 免疫 disabled | ✅ **属实** | `obs-module.c` `load_all_callback`：`if (info->type != OBS_MODULE_TYPE_CORE && is_disabled_module(info->name))` 跳过；`obs_is_core_module()` 存在；前端 PluginManager 用它过滤（跨范围，未逐行核） |
| V5 | `:295` core = 8 个上游插件 + `obs-libfdk`（9 个），plugin = 4 个 `android-*` | ✅ **属实** | `plugins/CMakeLists.txt:52-60,84,98,126,135` 共 **13** 个 `add_subdirectory`；`:137` `list(REMOVE_ITEM ... android-audio android-camera android-capture android-screen)` → 9 core + 4 plugin |
| V6 | `:303-304` 「`add_subdirectory()` 之后、`set_obs_core_modules()` 之前 `list(REMOVE_ITEM)`」 | ✅ **属实** | `plugins/CMakeLists.txt` 中 `list(REMOVE_ITEM)` 确实在 13 个 `add_subdirectory` 之后、`set_obs_core_modules()` 之前 |
| V7 | `:105-115` 「Android 分支提前 `return()` 会跳过 `set_obs_core_modules()`，需在 return 前补」 | ✅ **属实** | 顶部 `if(NOT ENABLE_CORE_MODULES) set_obs_core_modules(); return()`；Android 分支末尾（`:137` 附近）确实补了 `set_obs_core_modules()` |
| V8 | `:733` 「非 debuggable 包实测 13 个插件、0 失败」 | ✅ **属实（内部自洽）** | 13 与 `plugins/CMakeLists.txt` 的 13 个 `add_subdirectory` 一致；但 `obs-core-modules-loader-android.c:49` 与 `PluginModuleLoader_Android.cpp:38` 写「14 个」，与文档冲突（见 P2-7） |
| V9 | `:674` 「上游 `.clang-format` / `.editorconfig` / `CODESTYLE.md` 全部适用」 | ⚠️ **存疑 / 未达标** | `obs.h:37-40` 与 `platform-nix.c:29-31` 是 `CODESTYLE.md:286` 禁止的平台 include 块；`PORTING.md:706` 自认约 24 处，但 `PORTING.md:774` 的待办清单只提「约 24 处」，未点名 libobs 这两处 |
| V10 | `:695-701,769-773` 「C++ 缺大括号约 60/82 处」 | ✅ **与本范围无关但属实** | 本范围全是 `.c` / `.h` / `.cmake` / `.effect`，`CODESTYLE.md:199` 的 `InsertBraces: true` 只对 C++（`.clang-format:108-110` `Language: Cpp`）；`.c` 走 Linux 内核风格，允许单语句不加大括号——`PORTING.md:701` 的说法正确 |
| V11 | `:712-716` 「include 块内未按字母序：18 个新增文件」 | ⚠️ **部分相关** | `obs-android.c:23-31` 的 include 块（`stdatomic.h` / `stdlib.h` / `string.h` / `unistd.h` / `sys/utsname.h` / `sys/system_properties.h` / `sys/sysinfo.h` / `inttypes.h`）确实未按 `CODESTYLE.md:285` 的 case-sensitive 字母序，且 `#include "obs-internal.h"`/`obsconfig.h`/`obs-android.h` 分块方式也与 `:244-272` 的推荐顺序不同。属实 |
| V12 | `:20` minSdk 29 / targetSdk 35 | ✅ **属实，且派生出一个问题** | 使 `util/platform.c:800` 的 `__ANDROID_API__ < 29` 分支恒为假 → 死代码（P2-1） |
| V13 | `os-android.cmake:2-8`（非 PORTING.md，相关声明）「只有两处必须替换：obs-nix.c→obs-android.c、os_generate_uuid→platform-android.c；obs-nix-platform.c 原样复用」 | ✅ **属实** | `os-android.cmake:16-28` 的 `target_sources` 清单与描述一致；`obs-nix-platform.c` 确实被加入（前端 `obs_get_nix_platform()` 需要，HEAD `git grep` 有 5 处前端调用点） |
| V14 | **全文** 关于两个 `.effect` 的 GLSL ES 改动 | ❌ **完全遗漏** | `git grep -n -i "effect\|shader\|GLSL\|着色\|Adreno\|float3\|Sample" HEAD -- PORTING.md` 无相关命中。780 行文档对 Qt/APK/ccache/尺寸处理极详尽，却未记载这两处**影响全平台 shader 产物**的无守卫改动，也无验收判据（P1-2） |
| V15 | `:758-759` 「探针日志 63 处…建议加 `OBS_ANDROID_PROBE`」 | ✅ **属实（本范围可见）** | `obs-android.c:283,360,417,472` 等 4 条总线注册日志 + `obs-core-modules-loader-android.c:30,92-104` 的 `CORE_LOG` 双写日志，均属该债（P2-11） |

**核验结论**：`PORTING.md` 对**插件分发/加载链路、core/plugin 分类、CMake 坑、Qt/打包/ccache** 的记录准确度很高（V3–V8、V13 属实）；**与本范围直接冲突的是 V2（有误）与 V14（遗漏）**，另有 V9/V11 的风格自述在 libobs 范围内同样未达标。修正成本很低（改 3~4 处注释 + 补一节 effect 说明）。

---

# 跨范围观察

1. **`cmake/common/osconfig.cmake:9` 的 Android 判据靠工具链文件名兜底**：`CMAKE_SYSTEM_NAME STREQUAL "Android" OR CMAKE_TOOLCHAIN_FILE MATCHES "[\\/]android[.]toolchain[.]cmake$"`。C 侧用 `__ANDROID__`，CMake 侧用 `OS_ANDROID`，是两套独立判据，靠 NDK 工具链保证一致。若换自写 toolchain（文件名不含 `android.toolchain.cmake`）且 `project()` 前 `CMAKE_SYSTEM_NAME` 未就绪，会漏判 → 建议前端 owner 关注。

2. **两个同名 `os-android.cmake`**：`libobs/cmake/os-android.cmake`（本范围，管 libobs 源文件/链接）与 `frontend/cmake/os-android.cmake`（管 Qt assets/打包/版本号）同名但职责不同，`PORTING.md:327` 也提到 `os-android.cmake` 在 `frontend/CMakeLists.txt:105` 读 `OBS_MODULES_ENABLED`——引用时容易混淆，建议文档里始终带全路径。

3. **`os-android.cmake:37` 的 `-Wl,--exclude-libs,ALL` 是全仓库级链接策略**：`plugins/CMakeLists.txt` 的 Android 分支注释也提到 `obs-ffmpeg` 用了同款措施（静态 FFmpeg 汇编 `adrp+add` 引用 `ff_*` 表导致 `R_AARCH64_ADR_PREL_PG_HI21`）。它作用于 libobs 链接的所有静态库，会收窄 `libobs.so` 导出面——若未来有插件需要直接解析 libobs 静态依赖里的符号会受影响。不属本范围深评，但建议记录为全局构建策略。

4. **全仓库 `Q_OS_ANDROID` 为 0（lead 提供）**，Qt 前端也一律用 `__ANDROID__`：这保证了 C/C++/CMake 三侧宏名统一，是加分项；与上游 `_WIN32`/`__APPLE__`/`__linux__` 的「编译器预定义宏」惯例一致，而非引入 Qt 专有宏。

5. **`obs-android.h` 的 4 张 host 回调表实质是「libobs 作为 Java ↔ 纯 C 插件的消息总线」**：`obs-android.h:10-26` 论证了「插件只链 libobs、拿不到 JavaVM」，因此把 Android 专有 API 放进 libobs 公共命名空间（`EXPORT`）。这是本范围**最大的架构侵入**——不是行数，而是「libobs 核心库开始承载宿主平台的应用层回调协议」。方案 A 能把它从 `obs.h` 摘掉，但符号本身仍留在 libobs 里。若上游未来新增同名 `obs_android_*` 符号会冲突；建议所有符号统一加 `obs_android_` 前缀（已做 ✔）并在 `PORTING.md` 明确「这些是移植私有 API，不追求上游合并」。

6. **`PORTING.md:441-489` 的「自适应原则」与 libobs 无关**，但文档整体「决策 + 实测证据 + 反例」的写法值得 libobs 侧借鉴——目前 libobs 范围内的决策证据全散落在源码注释里（`os-android.cmake:2-8`、`obs-android.c:254-271`、`obs-core-modules-loader-android.c:27-52`），`PORTING.md` 没有对应章节，导致 V2/V14 这类「文档与实现脱节」。

7. **给其他审查方向的接口提示**：`obs-android.h` 的 4 张表被 4 个 `plugins/android-*` 与 3 个 `frontend/widgets/OBSAndroid*.cpp` 使用；`OBS_ROOT_PATH` 由 `frontend/utility/AndroidRuntimeBootstrap.cpp:280` 用 `setenv()` 设置，被 `libobs/obs-android.c:54`、`libobs/obs-core-modules-loader-android.c:59`、`frontend/plugin-manager/PluginModuleLoader_Android.cpp:66`、`frontend/utility/platform-android.cpp:43` 共 4 处读取——**env 变量名是全仓库级隐式契约**，未见集中定义（各写字符串字面量），建议由负责「插件分发/前端」的 reviewer 一并核查是否收敛。

---

## 附：本报告用到的关键命令

```
git merge-base upstream/master HEAD                 # 65c320b46e1af5c7a555161aea6182cdf7a8c524
git diff upstream/master...HEAD --stat -- libobs/   # 16 files, +1163/-45
git diff upstream/master...HEAD --numstat -- libobs/
git diff upstream/master...HEAD -- <file>           # 逐文件 diff
git show 65c320b:<file>                            # 上游原版
git grep -n add_default_module_paths HEAD          # 仅 3 注释 + 1 定义
git grep -n add_default_module_paths 65c320b       # 无输出（上游无此函数）
git grep -n obs_add_module_path HEAD               # Android 前端无调用
git grep -n -i "effect|shader|GLSL|Adreno" HEAD -- PORTING.md   # 零命中
git show 65c320b:libobs/obs-module.c               # find_core_module / load_all_callback / make_data_dir
git show 65c320b:libobs-opengl/gl-shaderparser.c   # Sample 与 return_type 的代码生成
git show HEAD:plugins/CMakeLists.txt               # Android 白名单 13 个 add_subdirectory
```
