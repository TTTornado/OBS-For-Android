# OBS Studio for Android 移植 —— 总体代码审查报告

> **审查对象**：`D:\Agent工作区\OBS-For-Android`（`TTTornado/OBS-For-Android`），上游 `obsproject/obs-studio` tag **33.0.1**（`65c320b`）的 fork。
> **审查日期**：2026-11-05　**基线口径**：`git diff upstream/master...HEAD`（merge-base `65c320b`）。
> **方法**：6 个领域并行审查（构建/CI、libobs 核心、GLES、frontend 侵入性、frontend Android 实现、插件）+ Lead 全局量化与交叉核验；全程只读，未修改任何源码。
> **详细分报告**：[`.review/`](.review/) 下 **6 份领域报告** + [`.review/lead-global.md`](.review/lead-global.md)（全局量化与横向观察）。本报告是汇总与裁决。

---

## 修复状态（2026-11-05 实施）

用户要求修掉本报告 §0 / §5 列出的 P0。其中 ①②⑤⑥⑦ 已实施，③「依赖供应链 / 锁定依赖」经后续讨论按维护者决定**撤销**（改为跟随 `master`、不缓存整包依赖产物），④ 按用户决定保持现状；下表之后的 §0、§5 及后续章节是**修复前**的审查记录，保留作为证据链，其中关于「钉 commit SHA / SHA256SUMS」的建议已撤销，以本章节为准。

| P0 | 状态 | 实施要点 |
|---|---|---|
| ① FDK-AAC 与 GPL 不兼容 | ✅ 已修 | `compile-check.sh` 去掉 `-DENABLE_LIBFDK=ON`；`plugins/CMakeLists.txt` 白名单移除 `add_subdirectory(obs-libfdk)`；`obs-libfdk/CMakeLists.txt` 恢复上游原样；`deps-android` 的 fdk-aac 版本/URL/批次/`build_fdk_aac()` 全部移除。AAC 由 `obs-ffmpeg` 原生编码器提供。core 清单 9→8、内置插件 13→12，文档数字已同步 |
| ② keystore 逃生舱被写死 | ✅ 已修 | `build-apk.sh` 把 `KS_ALIAS`/`KS_PASS`/`KEY_PASS` 参数化（默认值逐字等于原硬编码）；`android-release.yml` 加 `secrets: inherit`；`android-compile.yml` 声明 4 个 `workflow_call.secrets`、job 级 env 中转判空、独立条件步骤把 base64 解到 `$RUNNER_TEMP` 并写 `$GITHUB_ENV`。未配置 secret 时签名行为与修改前完全一致 |
| ③ 依赖供应链 | ⏸ **按维护者决定撤销** | 不再钉 commit SHA、不提供 `SHA256SUMS`、不加 hash 校验、deps 缓存 key 不扩展；x264 / rnnoise 恢复跟随 `master`，配合「每次完整重建依赖」让上游更新自动生效。仅保留 `fetch.sh` 的 tar 全量列举（防截断 / HTML 错误页），它与是否锁定无关 |
| ④ CI 不验证 APK 内插件 | ⏸ **按用户决定保持现状** | 未新增任何插件清单/运行时验收步骤；作为「已接受的残余风险」记录，不再计入未完成 P0 |
| ⑤ 浮动 dock SIGSEGV | ✅ 已修 | Android 上从 `statsDock`（`OBSBasic.cpp`）与 `on_lockDocks_toggled` / `AddDockWidget` / `AddCustomDockWidget`（`OBSBasic_Docks.cpp`）共 4 个设置点去掉 `DockWidgetFloatable`，`#ifdef __ANDROID__` 隔离，`Closable`/`Movable` 按上游原值保留 |
| ⑥ `showEvent` 无条件 resize | ✅ 已修 | 删除 `OBSBasicFilters.hpp` 声明、`OBSBasicFilters.cpp` 整个实现与不再需要的 `<QStyle>` include；Android 尺寸仍由构造函数里的 `androidFitTopLevelToScreen()` 在 `show()` 前处理 |
| ⑦ 音频泵 acquire/release 竞态 | ✅ 已修 | 删除会自行加锁的 `audio_pump_start()`/`audio_pump_stop()` 包装，改为 acquire/release **整段持 `audio_life_mx`**、内部短暂嵌套 `audio_mx` 改计数、放开 `audio_mx` 后才直接调 `*_impl()`（含 `pthread_join`）。锁顺序分析确认无反向获取、无死锁，单源行为不变 |

**顺带清理**（审查中发现的 P1/P2）：删除死代码 `add_default_module_paths()` 及其 3 处错误引用、修正「14 个 core」等错误注释、`PORTING.md` 中 3 项已过时的待办、`release-notes.md` 的「Android 8.0（API 29）」事实错误。

### 关于第 ④ 项的讨论结论

用户观点：上游插件已由上游验证、新增插件可选、不影响正常使用，不必每次 CI 验证，新增插件单独真机验证即可。

我的意见与最终决定：
- 纠正一个前提 —— 13 个内置插件里 9 个是 **core**（禁掉 obs-x264 无法录制、禁掉 rtmp-services 连设置页都建不起来），可选的只有 4 个 `android-*`；且历史 P0 的根因是 **assets 打包链路**（Android 只解 `lib*.so`），不是插件代码，上游验证不到，`PORTING.md` 自己也写了「debuggable 包会全量解出，所以这个问题一直被掩盖」。
- 但完全同意「每次 CI 做运行时/功能验证」无必要且 CI 没设备；我原本的建议可拆成「不做功能验证」+「一条纯静态的产物断言（约 10 行 shell，只核对 MANIFEST 与 APK assets 一致）」。
- **按用户意见完全保持现状**：不加任何 CI 断言。风险记录：打包链路回归只能靠人工真机发现；若将来出现第二次「插件没进包」，建议在发布流程（非每次 CI）加一次性产物核对。

### 验证方式与残余风险

- 已做：`git diff --check` 通过；改动的 shell 脚本 `bash -n` 通过；YAML 结构/断言校验通过；`build-apk.sh` 无新环境变量时签名命令插桩比对与改前逐字一致；`fetch.sh` 的 tar 完整性校验用本地构造归档做了功能自检；音频泵锁配对逐行复核。
- **未做**：完整构建与真机运行 —— 环境无 NDK / Qt for Android / Android SDK，无法验证编译期与运行时行为。**这部分仍需在 CI/真机上过一遍**。
- 残余风险 / 需人工跟进：
  1. 依赖不锁定：x264 / rnnoise 跟随 `master`，上游一提交就会自动进入下一次 CI 构建；代价是构建不再可重现，且上游若引入回归会直接反映到 CI —— 这是维护者明确接受的选择。
  2. 4 个 release Secret（`RELEASE_KEYSTORE_B64` / `RELEASE_KS_ALIAS` / `RELEASE_KS_PASS` / `RELEASE_KEY_PASS`）需在仓库配置，**否则 tag 包仍会用临时 debug 密钥**、用户依旧不能覆盖安装；CI 里「解码 release keystore」步骤显示 `skipped`、构建照常成功且**无任何告警**，容易误以为已用正式密钥签名。
  3. 第 ④ 项按用户决定保持现状（见上）。
  4. `PORTING.md` 待办里其余工程债（`API` 未进缓存 key、`always()` 毒化缓存、paths 漏 `deps/**`、`.gitignore` 无规则、`apksigner`/`aapt2` 取最新版本）本次按 P0 范围未修。
  5. §5 的 P1/P2 清单（除顺带清理项外）均未实施。


### C/D 组清理（2026-11-06）

按维护者确认的边界实施，均为低风险机械项；架构级重构（前端平台委托、`.ui` 双份真相、`__ANDROID__` 宏统一、声明式插件注册）与功能性待办按决定跳过。

**C 组**：`libobs/obs.h` 删除平台 include 块并给 2 个使用 `obs_android_*` 的 .cpp 补直接 include；4 个 `OBSAndroid*.cpp` 去除整文件 `#ifdef`；16 个纯 include 块去宏（含代码的混合块保留）；18 个新增文件统一 GPLv2 版权头，`AndroidRuntimeBootstrap.*` 误署上游作者已修正；`cunminF` 来源只在 `PORTING.md` 说明，未擅自加 `Co-authored-by`；补大括号 54 处、include 字母序重排、`HOLD_MS`/`MOVE_SLOP` → `kHoldMs`/`kMoveSlop`；10 个只有自由函数的 `OBSAndroid*` 从 `widgets/` 移到 `utility/`，同步 16 个 include 引用点与 CMake 清单，消除反向依赖；删除死代码 `android-plugin-modules.h`（生成器 + 模板 + 废弃方案注释）；4 个插件 UI 串改英文 + `obs_module_text()` + locale + `obs_module_description()`（中文注释保留）；`PORTING.md` 补 GLES 能力矩阵专节、版本号四处同步说明、构建可重现性的有意决定说明。

**D 组**：`push` / `pull_request` 的 `paths` 补 `deps/**`、根 `CMakeLists.txt`、`osconfig.cmake`、release workflow；`.gitignore` 忽略 `deps-android/{src,prebuilt}` 与 `frontend/cmake/android/libs/`；`build.sh` 的 `set -e` 子 shell「假成功」修复、`defs.sh` header-only `cp` 加显式检查、`build-apk.sh` 补空 APK 产物检查；`android-release.yml` 顶层 `permissions: contents: write` 收窄到 release job、actions 钉 commit SHA；ccache key 加 `github.run_attempt`，解决同提交 re-run 的 `Cache save failed`。`sort -V | tail -1` 与 glad 生成脚本未入库按决定只做文档说明。

**仍未做**：架构级重构、功能待办（CLAMP 对话框、子菜单、容器/预览面重复实现、探针收敛）、`sort -V` 钉版本、glad 生成脚本入库。

---

## 0. 执行摘要（TL;DR）

这是一份**完成度相当高、工程记录极其详实、但尚未达到可合规发版状态**的 Android 移植。

- **移植方式**：以 61 个新增文件 + 76 个上游文件的条件编译插入为主（+21932 / **-148** 行），几乎不删改上游逻辑；底层（libobs / libobs-opengl / 插件构建）**基本沿用了上游"按平台选源文件"的分层**，这是侵入性小的主要原因；但 Qt 前端层把大量 Android 逻辑散落到 60 个上游活跃 `.cpp` 里，是侵入性的主要来源。
- **上游风格符合度**：**底层符合、前端不符合**。CMake 的 `os-android.cmake` / `gl-android.c` / `find-font-android.c` / `PluginModuleLoader_Android.cpp` 都是标准的 OBS 平台后端组织；但 `obs.h` 的平台 include 块、整文件被 `#ifdef` 包裹的 `.cpp`、约 118 处 C++ 缺大括号、4381 行中文注释、以及不规范的提交历史，都不符合上游 `CODESTYLE.md` / `CONTRIBUTING.md`。
- **实现优雅性**：有少数称得上"抽象"的设计（统一签名 + 编译期恒值谓词、头文件自守卫、assets 插件分发），也有明显补丁式实现（前端 4 个过滤器仍集中在 `OBSApp.cpp`、APK"打两遍"、25 个 GL 常量重定义/置 0、5 处重复的预览面所有权）。
- **文档**：`PORTING.md` 780 行是罕见的优质工程记录（含负面结论与实测数据），但存在**滞后**（3 项待办/自述实际已完成）、**遗漏**（GLES 能力矩阵、两个 effect 的无守卫改动全无记载）与**错误**（`add_default_module_paths()` 声明、release workflow 机制解释、多处行号）。

### 发版前必须解决（P0）

| # | 问题 | 证据 | 领域 |
|---|---|---|---|
| 1 | **FDK-AAC 静态链进 GPLv2+ APK，许可证不兼容** | `android/deps-android/versions.sh:35,58`、`defs.sh:141-155`、`compile-check.sh:113`、`plugins/CMakeLists.txt:158`、`libobs/obs.h:3-14` | build-ci / lead |
| 2 | **debug keystore 每次新建，且 `KEYSTORE` 逃生舱被硬编码别名/密码废掉** —— 即便换成 release keystore 也签不上，用户永远无法覆盖安装 | `android/build-apk.sh:174-183,192-193` | build-ci |
| 3 | **依赖供应链**：`x264`/`rnnoise` 钉 `master`、下载无 SHA256/GPG 校验、缓存 key 不感知内容 → 可能永久复用旧/被篡改产物 | `versions.sh:25,31`、`fetch.sh:54-58`、`android-compile.yml:129` | build-ci |
| 4 | **CI 不验证 APK 内容**："插件确实进包"无任何自动化验收，而这是历史上唯一的发版阻塞项 | `build-apk.sh:138-171`、`PORTING.md:43` | build-ci / lead |
| 5 | **浮动 dock 在渲染时 SIGSEGV**（作者自列为未修待办） | `PORTING.md:734-737`、`OBSQTDisplay.cpp:150-159,310-322`、`ObsDisplayHost.java:177-188` | frontend / graphics |
| 6 | **`OBSBasicFilters::showEvent()` 无条件在 QShowEvent 里 resize**（领域报告定 P0）：改桌面行为、与 `PORTING.md` 自述原则冲突、Android 上二次 resize | `OBSBasicFilters.hpp:128`、`OBSBasicFilters.cpp:633-645`、`:68-79` | frontend-intrusion |
| 7 | **`android-screen` 模块级音频泵 acquire/release 竞态**：两颗源交叉时后一颗的音频静默丢失，且状态行误报 | `screen-capture.c:367-456,459-494` | plugins-android |

> 说明：P0 中 1/2/3 是合规与发布工程问题，4 是质量保障缺口，5 是唯一确定性的运行时崩溃，6 是"每次打开对话框都会触发的桌面行为改变"（领域审查员定 P0，Lead 认为影响可触发但非崩溃，按 P1 处理亦可），7 是多源/重建场景下的确定性功能失效。

### 总体裁决

| 维度 | 评价 |
|---|---|
| 移植完成度 | ★★★★☆ —— 从 libobs、GLES、Qt 前端、Java/JNI、4 个采集插件到 CI/发布，链路完整且经真机调试 |
| 上游风格符合度 | ★★★☆☆ —— 底层好、前端差；独立 fork 可接受，贡献上游不可接受 |
| 侵入性 | ★★★☆☆ —— 总量大（137 文件）但删除极少；底层干净、前端分散；76 个上游文件带来长期 rebase 成本 |
| 实现优雅性 | ★★★☆☆ —— 少数抽象设计亮眼，多数为"打补丁" |
| 文档与可追溯性 | ★★★★☆ —— `PORTING.md` 优秀，但有滞后/遗漏/错误 |
| 测试与质量保障 | ★☆☆☆☆ —— CI 只编译，零测试、零 APK 内容验收 |
| 发版就绪度 | ★★☆☆☆ —— 上述 P0 未清之前不建议发布正式包 |

---

## 1. 移植都做了什么

| 层 | 规模 | 主要内容 |
|---|---|---|
| **构建 / 依赖 / CI** | +1991 / -2（17 文件） | 新增 `android/`（build-apk.sh、compile-check.sh、deps-android 15 个第三方库交叉编译）、`cmake/android/` 三件套、`.github/workflows/android-{compile,release}.yml`；两个 CI job（compile 不带 Qt、apk 带 Qt 打包）、ccache / Gradle 缓存、tag 触发 pre-release |
| **libobs 核心** | 上游 11 文件 +75/-45；新增 5 文件 +1071 | `os-android.cmake` 复用 `platform-nix.c`/`threading-posix.c`/`obs-nix-platform.c`，只替换 `obs-nix.c`→`obs-android.c`、`os_generate_uuid`→`platform-android.c`；新增 core module loader、Android 总线（USB/相机/屏幕/音频）、`add_default_module_paths`（**死代码**）；`graphics.h`/`obs.h` 加平台守卫 |
| **libobs-opengl / GLES** | +981 / -16；`deps/glad-gles` +3258 | 新增 559 行 `gl-android.c`（NDK EGL + ANativeWindow，与上游 `gl-cocoa.m`/`gl-windows.c` 同惯例）；`gl-subsystem.h` 集中 33 个 `#define`（25 个 `GL_*` 常量重定义/置 0）、`convert_gs_upload_format()` + 双版本 `gs_format_needs_rb_swap()`；裁剪 timer / P010 / 显存查询 / border clamp / sRGB；GLAD GLES 生成代码 vendoring |
| **Qt 前端（对上游文件的修改）** | +7389 / -76（约 60 个 M 文件） | 触摸适配（长按=右键、BACK=Escape、单指 QScroller）、对话框尺寸放宽与 clamp、菜单定位、首页 dock 比例、预览显示层、录制/推流容器格式限制、投影仪、崩溃处理桥接、插件加载时序 |
| **Qt 前端（新增 Android 实现）** | 28 个 A 文件 | `AndroidRuntimeBootstrap`（运行时路径 + 插件 assets 解压）、5 个 `OBSAndroid*` 桥接组件、5 个 Java Host（Display/ForegroundService/Permission/Projection/Usb）、`AndroidManifest.xml`、窗口尺寸/菜单定位 helper、Android 崩溃处理与系统信息 |
| **插件** | +6568 / -9 | 新增 4 个采集插件：`android-audio`(AAudio, 752 行)、`android-camera`(Camera2 NDK, 1738 行)、`android-capture`(UVC-over-libusb 重写, 2267 行)、`android-screen`(MediaProjection, 857 行)；上游插件适配：obs-ffmpeg、rtmp-services、text-freetype2、obs-libfdk、obs-outputs/librtmp |
| **插件分发机制** | —— | `assets/obs-plugins/*.so + MANIFEST` → 启动时解到 `$OBS_ROOT_PATH/lib` → `obs_load_plugins()` + `os_glob()` 扫目录；`.managed` 清单 + `.bundle-stamp`(MANIFEST SHA1) 支持用户按需增减；绕开 Android 只把 `lib*.so` 解到 `nativeLibraryDir` 的限制 |

---

## 2. 对上游的侵入性有多大

### 2.1 量化

- 与上游差异 **137 文件、+21932 / -148 行**，其中 **A=61、M=76**。删除仅 148 行 ⇒ 没有重写上游逻辑，全是"新增文件 + 插入代码块"。
- 按目录：frontend +7389 / -76、plugins +6568 / -9、deps(glad-gles) +3258、libobs +1163 / -45、libobs-opengl +981 / -16、android +925、.github +595、cmake +265。
- **60 个文件**含 `__ANDROID__`，共 **163 次**提及、**126 条**条件编译指令（另一口径：全树 165 次 / 147 条）；**`Q_OS_ANDROID` 出现 0 次**。
- **29 个上游文件是无条件改动**（不含任何 `__ANDROID__` 守卫），这是最需要关注的一类。
- fork 保留了完整上游历史（`65c320b` 可达 331 个祖先提交），可以持续 merge/rebase；但 **58 个移植提交集中于 2026-10-29 ~ 11-05、仅 1 个 merge（同步 33.0.1）**，是"导入快照 + 叠加"而非长期伴随演进。

### 2.2 无守卫改动逐类核实（结论）

| 类型 | 代表文件 | 对桌面的实际影响 |
|---|---|---|
| 纯 bug 修复（方向正确，但属"借移植之名改上游"） | `OBSStudioAPI.cpp:150,161,172,183,194`（5 处 `outputHandler` 空指针）、`AutoConfigTestPage.cpp:444-448`（未初始化变量）、`obs-service.c:418,427`（补 `obs_service_valid`）、`OBSBasicSettings_Stream.cpp` 一批 NULL 加固 | 语义等价或更健壮，**良性**；建议拆成独立 bugfix 提交 / 直接提上游（`PORTING.md` 与分报告均建议） |
| 用上游已有的安全封装替换裸访问 | `OBSBasic_{Streaming,Recording,ReplayBuffer,VirtualCam}.cpp` 把 `outputHandler->XxxActive()` 改为 `XxxActive()` | **核实更正**：这 4 个包装函数**不是移植新增**，`git show 65c320b` 确认 33.0.1 已存在且已带空指针检查，移植只改了 6 个调用点。桌面正常路径等价、null 时由崩溃变 `false`；不应加平台守卫 |
| 为 Android 服务、但非 Android 恒等 | `AudioMixer.cpp:871`、`OBSBasic_Preview.cpp:242,260`、`OBSBasic_Scenes.cpp:633`、`OBSBasic_StudioMode.cpp:380-414` 把 `QCursor::pos()` 换成 `OBSMenuPos(...)` | `AndroidMenuPos.hpp:34-48` 非 Android 分支恒 `return QCursor::pos()`，**桌面行为不变**；但每个桌面构建都无条件 include 一份 Android 专有头，且锚点计算被求值后丢弃 |
| 为 Android 服务、桌面行为**确实改变** | `OBSBasicFilters.cpp:633-645` 无条件 `showEvent` resize（P0-6）；`OBSProjector.cpp:340` 改用 `event->globalPosition()` 且**未走** `OBSMenuPos` 统一入口；`rtmp-services-main.c:122-125` 把 `load_*` 移出 `#ifdef`；`OBSBasic_ContextToolbar.cpp:212` 把 `GetCurrentSceneItem()` 提到可见性闸门之前 | 见 §5；其中 `showEvent` 注释称"桌面不变"实际只是"通常不触发"，且与 `PORTING.md:377-380`「必须 show() 前调用」自相矛盾 |
| **跨后端共享资源被改写** | `libobs/data/deinterlace_base.effect`、`format_conversion.effect`：11 处 `float3`→`float4(rgb,1.)`、7 处 `.Sample(...,0)` 去第三参数、约 13 处 `/2`→`/2.0` | 见 §5 P1-1：两位审查员对风险程度判断不同，但共识是**无守卫、无验收记录、`PORTING.md` 零记载** |
| 共享头/签名扩展 | `CrashHandler.hpp:240` 枚举加 `Android`；`gl-helpers.h:251-253` 给 `gl_init_face()` 全局加 `bool swap_rb` | 前者无害；后者配 `gs_format_needs_rb_swap()` 桌面恒 `false`，运行时语义不变，但**桌面二进制不字节等价**（`gl_init_face` 定义在另一 TU、`compiler_common.cmake:116` 对 RelWithDebInfo 关闭 LTO，分支消不掉，每次上传多一次 `bfree(NULL)`） |
| 无意义残留 | `libobs/obs-display.c:259-261` 把 `if (render_display_begin(...))` 拆成 `const bool began = ...; if (began)`，`began` 之后无使用 | 纯多改一行上游代码，应删除 |
| CMake 平台分支 | `CMakeLists.txt:7`、`osconfig.cmake:5-13`、`frontend/CMakeLists.txt`、`libobs/CMakeLists.txt`、`libobs-opengl/CMakeLists.txt`、`plugins/*/CMakeLists.txt` | 桌面路径不受影响（`OS_ANDROID` 为假，`else` 分支与上游逐字相同）；`osconfig.cmake` 用"`CMAKE_SYSTEM_NAME` + NDK toolchain 文件路径"嗅探 Android，能工作但脆弱 |

**真正的桌面回归**只有几处，且都值得修：两个 `.effect`（跨后端语义）、`gl-subsystem.c:116-125` 无守卫删除 `GL_ARB_debug_output` 分支、`showEvent` 无条件 resize、`OBSProjector.cpp:340` 菜单定位孤例、`OBSBasic_ContextToolbar.cpp:212` 取场景项时机。

### 2.3 侵入性的结构性判断

- **最小侵入的范本**：`libobs/cmake/os-android.cmake:16-28` 只列入 3 个 Android 专属源文件，其余全部复用 Linux/POSIX 文件；`threading-posix.c:264` 只在既有条件链上追加 `|| defined(__ANDROID__)`（1 行）；`graphics.h:485` 把 `__ANDROID__` 排在 `__linux__` 之前，避开 NDK clang 同时定义 `__linux__` 的陷阱；`OBSBasicSettings.cpp:1117-1136` 用 `#ifndef __ANDROID__` 保留桌面容器下拉顺序"一字不动"。
- **最大侵入的来源**：frontend 的 60 个上游活跃文件，尤其 `OBSApp.cpp`（+629 行 / 10 处 `#ifdef`）、`OBSQTDisplay.cpp`（+251 行 / 12 条条件指令）、`OBSBasic.cpp`（+228 行 / 7 处）。这些是上游高频改动文件，每次 rebase 都可能冲突；`PORTING.md:105-118` 已记录"提前 `return()` 跳过上游新增初始化"等事故，印证同步成本。

---

## 3. 代码是否符合上游的多平台风格习惯

### 符合（verdict ✅）

1. **平台后端文件 + CMake 按 OS 选源**：`libobs/CMakeLists.txt:268` 的 `elseif(OS_ANDROID) include(cmake/os-android.cmake)`、`libobs-opengl/CMakeLists.txt:33` 的 `$<$<PLATFORM_ID:Android>:gl-android.c>`、`plugins/text-freetype2/CMakeLists.txt:28` 的 `find-font-android.c`、`frontend/cmake/feature-plugin-manager.cmake:209-213` 的 `PluginModuleLoader_Android.cpp` —— 与上游 `os-*.cmake`、`gl-*.c`、`find-font-*.c`、`platform-*.cpp` 的组织**同构**。
2. **复用而非重写 POSIX 实现**：`os-android.cmake:2-8` 明确论证"bionic 就是 POSIX libm/pthread 实现，几乎全部复用 Linux 文件，只有两处必须替换"。这是整个移植最干净的部分。
3. **core/plugin 分类复用上游 33.0 机制**：`plugins/CMakeLists.txt:175-179` 在 `add_subdirectory()` 之后、`set_obs_core_modules()` 之前把 4 个 `android-*` 从 `OBS_MODULES_ENABLED` 剔除，得到 9 个 core + 4 个 plugin（共 13 个 `add_subdirectory`）；`obs-core-modules-loader-android.c` 的 `load_core_modules()` 签名、`find_core_module()`、`%module%` 数据目录替换都与 nix/win 版一致。
4. **在既有平台链条里扩展**：`components/Multiview.cpp:46` 用 `#elif defined(__ANDROID__)` 接在 `_WIN32`/`__APPLE__` 之后；`OBSApp.cpp:1807` 把 `#if !defined(_WIN32) && !defined(__APPLE__)` 扩成 `&& !defined(__ANDROID__)`。
5. **退化策略"显式失败优于静默错色"**：GLES 侧 16-bit unorm 置 0 让 `glTexImage2D` 报错、P010 如实返回 false、timer 取不到如实返回 false。
6. **用 `__ANDROID__` 而非 `Q_OS_ANDROID`**：经与上游基线对照（`git grep -h -o "Q_OS_[A-Z_]*" 65c320b -- frontend/` 全 frontend 只出现 1 次 `Q_OS_UNIX`），上游 C++ 主流就是编译器宏 `_WIN32`/`__APPLE__`/`__linux__` ⇒ **这一选择符合上游 C++ 惯例**（可辩护）。但 165 处散落仍建议统一为 `Q_OS_ANDROID` 或单一 `OBS_PLATFORM_ANDROID` 宏。

### 不符合（verdict ❌）

1. **`obs.h:37-40` 用 `#ifdef __ANDROID__` 包裹 `#include`** —— 直接违反 `CODESTYLE.md:286-287`，且冗余（4 个插件已直接 include，仅 3 个前端 .cpp 依赖传递包含）。`platform-nix.c:29-31`、`obs-nix-platform.c:22-31` 同类；仅 39 个 frontend M 文件就有 **17 个 `#ifdef` 块包着 41 条 include**（全 frontend 23 块 / 82 条）。
2. **整份 `.cpp` 被 `#ifdef __ANDROID__` 包裹**：`OBSAndroidDisplay.cpp:3/483`、`OBSAndroidKeepAlive.cpp:3/104`、`OBSAndroidPermissions.cpp:3/581`、`OBSAndroidUsb.cpp:3/170`。这些文件已由 `os-android.cmake:22-31` 只在 Android 挂入，宏是纯噪声；上游的正解是 CMake 选文件。
3. **前端散落 `#ifdef`**：约 460 行 Android 过滤器 + clamp/flick + 模态遮挡写在 `OBSApp.cpp` 共享文件，与上游"平台差异收敛到独立编译单元"的惯例相反。注：`PORTING.md:755-757` 把 `AndroidInputFilter` 列为"未拆"的工程债**已过时** —— 实际已拆成 `AndroidBackKeyFilter`/`AndroidComboPopupFilter`/`AndroidLongPressFilter`/`AndroidWindowFilter` 四个类（`git grep AndroidInputFilter HEAD` 0 命中），但 `AndroidWindowFilter` 仍同时做 clamp 与挂 `QScroller` 两件事，且文档没更新。
4. **目录分层错位**：10 个 `frontend/widgets/OBSAndroid*.{cpp,hpp}` 里只有**自由函数**，却按 widget 命名放进 `widgets/`；`PluginModuleLoader_Android.cpp:26` 反向 `#include "widgets/OBSAndroidDisplay.hpp"` 只为拿一个日志函数，形成 plugin-manager → widgets 的反向依赖。
5. **`.ui` 文件一个都没改**：39 个 M 文件里没有 `.ui`，所有桌面尺寸假设都在 `.cpp` 里用 `#ifdef` 覆盖，形成"`.ui` 仍是桌面值、运行时靠代码放宽"的**双份真相**；且与 4 个对话框各自的 `#ifdef`、全局 clamp 三套机制并存，违反 `PORTING.md:340` 自称的"职责不重叠"。
6. **CODESTYLE 批量违规**（`PORTING.md:693-721、769-780` 已自承但低估）：C++ 分支缺大括号实测约 **118 处**（自承 82 处，且遗漏了 `OBSApp.cpp` 其余 9 处、`OBSBasic.cpp`、`OBSBasicControls.cpp`、`OBSBasic_ContextToolbar.cpp`、`OBSProjector.cpp`、`OBSQTDisplay.cpp` 等 M 文件）；include 未按字母序也不止 18 个新增文件（`OBSApp.cpp:51-63` 等 M 文件同样违规，`SortIncludes: false` 需手动）；`HOLD_MS`/`MOVE_SLOP` 不用 `kCamelCase`。
7. **版权头**：新增 37 个 C/C++/Java 源文件仅 **12 个**带头（Java **0/5**、4 个新插件的 `.c` **全部无头**）；上游同类 C/C++ 普遍带 `Copyright (C) ...`。且 `AndroidRuntimeBootstrap.cpp:2` 等**全新文件**署成上游作者 `Lain Bailey`，属归属错误。
8. **中文注释 4381 行（占变更相关代码 5.8%）**，4 个新插件与 Java 层密度最高。上游代码全英文 —— 这决定了项目只能定位为**独立 fork**。
9. **提交历史**（对照 `CONTRIBUTING.md:44-63`）：58 个提交里 10 个标题超 50 字符、prefix 混用 `ci`(小写)/`build`/`qt`/`apk`/`frontend` 而非上游 `CI`/`cmake`/`libobs` 体系；4 个"一次性导入"大提交（+7292 / +4211 / +3782 / +2672 行）违反"每个提交是独立逻辑单元"。
10. **来源与署名风险**：`PORTING.md:6` 称原移植由 `cunminF/OBS_for_Android` 完成，但 58 个提交全署名 TTTornado、无 `Co-authored-by`、无原始版权声明。若代码源自他人 GPL 仓库，GPLv2 要求保留版权声明 ⇒ 建议核实并补齐。

---

## 4. 现有实现是否优雅，有没有更好的实现

### 4.1 亮点（值得保留 / 推广）

1. **`gl_init_face(... bool swap_rb)` + 桌面恒 `false` 的 `gs_format_needs_rb_swap()`**（`gl-helpers.h:251-253`、`gl-subsystem.h:203-223`）：调用点（`gl-texture2d.c:34-36`、`gl-texturecube.c:39-41`）**零 `#ifdef`**，把"是否需要 R/B 换序"表达成语义谓词而非平台宏。这是全项目最像"抽象"而非"补丁"的一处，建议推广到前端；其边界是桌面二进制并非字节等价（见 §2.2）。
2. **`AndroidMenuPos.hpp` 头文件自守卫**：非 Android 恒退回 `QCursor::pos()`，把 Qt/Android"触摸不更新光标缓存、`QCursor::setPos` 是空操作"的修复集中在一个头里。
3. **插件 assets 分发 + `.managed`/`.bundle-stamp`**：只删自己登记过的文件、用 MANIFEST SHA1 判版本、用户自放 `.so` 永不碰，既绕开 Android 的限制又支持按需增减插件。
4. **`PORTING.md` 的工程记录**：ccache 四个必需变量的论证、耗时与命中率的 CI 实测数值 —— 决策可追溯。
5. **CI 缓存设计**：`CCACHE_COMPILERCHECK=content` / `BASEDIR` / `NOHASHDIR` / 压缩 / 上限，以及"两个 job 只由 apk job 保存缓存"的单写入方取舍（经过实测，正确性优先）。
6. **`obs-android.c` 的防御性细节**：出参先清零再问、枚举结果强制 NUL 兜底、fail-open/fail-closed 逐格论证、ABI 用编译期宏而非运行期 `ro.product.cpu.abi`、把借用指针改为按值拷贝 + 原子指针；4 个插件注册源后还"回读确认"（因 `obs_register_source` 返回 void）。
7. **UVC 插件重写而非沿用 libuvc**：`uvc-input.c` 头注释记录真机实测 —— `uvc_init()` 要建 `NETLINK_KOBJECT_UEVENT` 热插拔 socket，Android SELinux 直接拒（`LIBUSB_ERROR_IO`），因此自写最小 UVC-over-libusb。这是有依据的深度适配。
8. **4 个插件符合 OBS 异步源惯例**：`obs_source_info` 的 `get_name`/`destroy`/`update`/`get_defaults`/`get_properties` 字段完整，且在 `obs_register_source()`（返回 void）之后主动回读 `obs_get_source_output_flags()` 确认注册没被拒 —— 比上游常见写法更防御。

### 4.2 不优雅 / 补丁味

- `OBSApp.cpp` 仍是 Android 逻辑的最大集中地（+629 行、约 460 行为 4 个过滤器 + clamp/flick + 模态遮挡）；过滤器虽已拆成 4 个类，但 `AndroidWindowFilter` 仍做两件事，且 `PORTING.md` 未更新（见 §3.3）。
- **APK"打两遍"**：因为 `androiddeployqt` 目标在构建末尾跑、那时 assets 还是空的，需要先产插件 → 清 Gradle 产物 → 再打一次。方案可行但依赖"Gradle 中间产物删除清单"与 `find | head -1`，且无插件进包验收。
- `gl-subsystem.h` 里 **25 个 `GL_*` 常量被重定义或置 0**（`GL_RGBA16=0`、`GL_TEXTURE_RECTANGLE=0` 等）；`struct gs_device` 在 Android 与桌面布局不同（`gl-subsystem.h:804-812`）。
- 预览面所有权 5 处各自实现（`PORTING.md:760` 自认）；容器格式限制 5 处各自编码。
- **死代码**：`obs-android.c:61-82` 的 `add_default_module_paths()` 无原型、全仓无调用者（`PORTING.md:281-282` 却说路径由它注册）；`os-android.cmake:150-156` 生成的 `android-plugin-modules.h` 全树无引用，且 `feature-plugin-manager.cmake` / `os-android.cmake:150` 注释仍描述已被废弃的"编译期白名单 dlopen"方案。
- 探针/诊断日志与业务代码耦合（新增源码 222 处日志调用），虽有 `OBS_PROBE_LOG` 默认关的开关，但 Java 侧 `lastAction`/`describe()` 与注册函数里"带副作用的探针"仍在。
- **音频总线未接通**（见 §5 P1-19）：`obs_android_set_audio_host()` 全仓零调用点，而 `android-audio` 插件把"音频总线未接通"写成了 UI 降级文案 —— 属于"实现未完成但被文档化为现状"。

### 4.3 更好的实现（按性价比排序）

| 优先级 | 方案 | 代价 | 收益 |
|---|---|---|---|
| **P0·零成本** | **在 `gl-shaderparser.c` 里为 GLES 补 `0.0`/`float(lod)` 转换，而不是改共享 `.effect`**（该文件 `:760` 已有 `float(lod)` 手法，说明作者知道怎么做） | 数行 | 让 `libobs/data/*.effect` 与上游字节一致，彻底消除跨后端语义风险与 rebase 冲突 |
| **P0·零成本** | 删除 `obs.h:37-40` 的平台 include 块（补 3 处直接 include）；还原 `obs-nix-platform.c`，Android 用独立实现文件；删除 `platform.c:800-810` 的 `timespec_get` 死 shim（minSdk 29 恒不生效） | 3 个文件 | 消除 3 处 CODESTYLE 违规与死代码 |
| **P0·小** | keystore 参数化（`KS_ALIAS`/`KS_PASS`/`KEY_PASS` + `secrets: inherit`）、固定 release keystore；依赖钉 commit SHA + `SHA256SUMS` 校验并纳入缓存 key；CI 加"APK 内插件清单验收" | 约 10~30 行脚本 + workflow | 清掉 3 个发版阻塞项 |
| **P1·小** | 插件解压改为"全部解到 `.staging-<stamp>/` → 成功后原子换目录"，并把结果作为 `bool` 上报给 `loadAppModules()`；`.managed` 回读加与 MANIFEST 对称的文件名白名单 | ~40 行 | 消除新旧 `.so` 混装崩溃与路径穿越删除风险 |
| **P1·小** | `OBSAndroidUsb.cpp:35` 的 `activity()` 签名改 `()Landroid/app/Activity;`；`ObsUsbHost.java:190` 用 `FLAG_MUTABLE \| FLAG_UPDATE_CURRENT`；`QJniEnvironment` 统一 `isValid()` 判空；补注册音频总线 | ~30 行 | 消除 USB/屏幕采集的必失败分支、段错误与音频设备选择失效 |
| **P1·小** | **先给 4 个无自守卫的头补整体守卫，再执行 `PORTING.md:703-710` 的"去掉 `#ifdef` 包裹 include"整改**；否则照文档改会引入桌面编译/链接问题（文档方案本身有陷阱） | ~10 行 | 避免"按文档整改反而破坏桌面" |
| **P1·中** | **插件构建改回上游声明式写法**：用 `add_obs_plugin(... PLATFORMS Android)` / `add_core_module(...)` 取代 `plugins/CMakeLists.txt` 的 159 行白名单 + 提前 `return()` + `list(REMOVE_ITEM)` —— 后者会二次篡改被 3 处消费的 `OBS_MODULES_ENABLED`（还连带把 4 个插件移出 `os-android.cmake:127` 的资源清单），是 `PORTING.md:105-118` 那类 rebase 地雷的根源；同时把 `file-updater` stub 收敛为 `shared/file-updater` 内按 `PLATFORM_ID` 选源、抽 `plugins/android-common/` 静态库去重 4 个插件的 AImageReader 拆除协议 | 中等重构 | 消除提前 return 与全局属性二次篡改，插件适配回到上游惯例 |
| **P1·中** | 前端按上游惯例收敛：抽出 `AndroidPlatformDelegate`（或独立编译单元 + 少量虚/回调钩子），把 `OBSApp.cpp` 的过滤器与 clamp/flick 拆开；`OBSAndroid*` 自由函数从 `widgets/` 移到 `utility/`，日志通道收敛为 `AndroidLog.hpp`；把 `.ui` 尺寸假设改为平台无关的 `QSizePolicy`/布局策略 | 中等重构 | 把 126 条 `#ifdef` 收敛到少量桥接点，消除".ui 双份真相"，降低长期 rebase 成本 |
| **P2·大** | GLES 兼容层：**能力表（`device->caps`）+ `gl-gles-compat.h` + CMake 决定 include 目录**（graphics-gles 方案 A），或拆出独立 `libobs-gles/` 后端目录（方案 B：约 4k 行永久分叉，但之后上游 bugfix 互不干扰） | 300~400 行 / 4k 行 | 桌面 GL 编译产物与上游完全等价、GLES 代码零 `#ifdef` |
| **P2·中** | 把 `PORTING.md` 拆成"当前状态 + 变更日志"，补 GLES 能力矩阵与 effect 改动专节，把散落在未入库 `plan.md` / `0907摄像头问题.md` / `.qoder/*.sh` 的知识收回仓库 | 文档工作 | 消除已发现的 3 项滞后、6 处错误/不完整声明 |

---

## 5. 问题清单（按严重度）

### P0 —— 发版阻塞 / 合规 / 确定性崩溃

1. **FDK-AAC 与 GPLv2+ 不兼容**（build-ci / plugins-android / lead 三方一致）。`android/compile-check.sh:113` 显式 `-DENABLE_LIBFDK=ON`、`versions.sh:35,58` 取 `mstorsjo/fdk-aac v2.0.3`、`defs.sh:141-155` 编成静态 `libfdk-aac.a`、`plugins/obs-libfdk/CMakeLists.txt:19-21` 强制 CXX 链接器链进插件随 APK 分发，白名单还把它列为 core，`.github/release-notes.md:15` 对外宣称"音频编码：AAC（FFmpeg / FDK-AAC）"。**上游默认就是 `option(ENABLE_LIBFDK "Enable FDK AAC support" OFF)`**（`git show 65c320b:plugins/obs-libfdk/CMakeLists.txt:3`），原因正是许可证：Fraunhofer FDK AAC 被 Debian 归入 non-free，其版权文件明写 "*It is incompatible with any version of the GNU GPL*"，GNU 许可证列表也列为 GPL-incompatible，且第 3 节不授予任何专利许可（[Debian fdk-aac copyright](https://metadata.ftp-master.debian.org/changelogs/non-free/f/fdk-aac/testing_copyright)、[GNU 许可证列表](https://www.gnu.org/licenses/license-list.html.en#fdk)）。**建议**：发版构建 `ENABLE_LIBFDK=OFF` 并移除白名单 —— `obs-ffmpeg.c:356` 的 FFmpeg 原生 AAC 已足够，**关掉 libfdk 不丢 AAC 编码能力**；或完成法律/专利评估并把完整许可证文本与源码可得性随包提供。
2. **签名链路被写死**（build-ci P0-1）。`build-apk.sh:174-183` 每次新建 debug keystore，`:192-193` 把别名/库密码/密钥密码硬编码为 `androiddebugkey/android` ⇒ `KEYSTORE` 环境变量逃生舱失效，`PORTING.md:661` 描述的"换固定 release keystore"路径被代码堵死。用户升级必须卸载重装、丢配置。**建议**：三项参数化 + `secrets: inherit` + `versionCode` 单调。
3. **依赖供应链**（build-ci P0-3）。`x264`/`rnnoise` 钉 `master`、`fetch.sh` 无校验和、deps 缓存 key 不含 master 内容 ⇒ 可能永久复用旧产物且无法发现篡改。**建议**：钉 commit SHA、`SHA256SUMS` 校验、把脚本与依赖内容纳入 key。
4. **CI 零运行时/产物验收**（lead / build-ci P1-6）。`PORTING.md:43` 坦承"不覆盖任何运行时行为"；grep 确认 workflow 无 `ctest`/测试；"打两遍"打包没有验证插件确实进包 —— 而"非 debuggable 包插件全加载失败"正是历史上唯一的发版阻塞项。**建议**：加约 10 行 shell 断言 APK 内 `assets/obs-plugins/` 与 MANIFEST 一致，并对纯逻辑（`OBSMenuPos`、stamp、路径）加 host 侧单测。
5. **浮动 dock 渲染时 SIGSEGV**（`PORTING.md:734-737`，未修）。Surface 销毁 → `makeCurrent(): no EGLSurface` → `QRhiGles2` 失败。graphics-gles 判定**主因不在 `gl-android.c`**（错误来自 Qt `QRhiGles2` + 前端共享 Java SurfaceView 所有权，`OBSQTDisplay.cpp:150-159,310-322`），本层只有加剧因素（无 `EGL_CONTEXT_LOST` 恢复、`eglMakeCurrent` 失败仅记日志）。作者倾向的"Android 上去掉 `DockWidgetFloatable`"方向正确。
6. **`OBSBasicFilters::showEvent()` 无条件 resize**（frontend-intrusion P0-1）。`OBSBasicFilters.hpp:128` 无条件新增 override、`:633-645` 实现里按 `availableGeometry()-titleBarHeight` 收高；同文件 `:68-79` 的 Android 构造函数块已调 `androidFitTopLevelToScreen(this)`，两者公式还不同（`avail-16` vs `avail-titleBarHeight`）。既违反 `PORTING.md:377-380`「必须 show() 前调用」，又让桌面小屏/macOS 菜单栏场景每次 show 都被强制 resize。**建议**：删除该 override（或整体包进 `#ifdef __ANDROID__` 并统一到构造函数的 fit）。
7. **`android-screen` 模块级音频泵 acquire/release 竞态**（plugins-android P0-2）。`screen-capture.c:459-494` 的 `audio_pump_release()` 在 `audio_mx` 外调 `audio_pump_stop()`，而 `:367-456` 的 `audio_pump_start_impl()` 以 `audio_thread_started` 判幂等。两颗源 A/B 交叉时：A 算出 `need_stop=true` 但未执行 stop，B acquire→start 看到 `audio_thread_started=true` 直接返回，随后 A 的 stop 发信号/join/销毁 `audio_stop`/`bfree(audio_buf)`/调 `obs_android_screen_stop_audio()` ⇒ **B 的音频静默丢失**且 `audio_state_line` 可能误报。**建议**：把 `audio_users` 增减与泵启停放进同一把 `audio_life_mx`（或显式引用计数 + 单一 owner 状态机），并给泵侧 `audio_buf` 生命周期加锁。

### P1 —— 明显缺陷

| # | 问题 | 证据 | 来源 |
|---|---|---|---|
| 1 | **两个 `.effect` 共 46 行无守卫改写，影响 D3D11/桌面 GL/Metal** | `format_conversion.effect` 11 处 `float3`→`float4`、`deinterlace_base.effect` 约 13 处 `/2`→`/2.0`；`gl-shaderparser.c:199-207,409-445` 按 `return_type`/参数生成 GLSL | libobs-core / graphics-gles |
| 2 | **`GL_EXT_texture_sRGB_decode` 被当硬门槛**，缺该非核心扩展的设备 `obs_reset_video()` 整体失败；同文件对 `sRGB_write_control` 却用"告警+降级"，处理不一致 | `gl-subsystem.c:139-143,158-163,620` | graphics-gles P1-2 |
| 3 | **`gl_enable_debug()` 无守卫删除桌面 `GL_ARB_debug_output` 分支** —— 只暴露老扩展的桌面 Debug 构建永久失去调试回调 | `gl-subsystem.c:116-125` | graphics-gles P1-3 |
| 4 | **glad 生成脚本 `.qoder/gen-glad-gles.sh` 三处引用但未入库**，3258 行生成代码不可复现；手写新文件缺 GPLv2/SPDX 头 | `deps/glad-gles/` | graphics-gles P1-4 |
| 5 | **`add_default_module_paths()` 是死代码，`PORTING.md:281-282` 声明有误**；实际路径注册在 `PluginModuleLoader_Android.cpp:79-86` | `obs-android.c:61-82` | libobs-core P1-3 |
| 6 | **四张 host 表并发写构成 C11 数据竞争 UB**（普通静态 `usb_host_store` + 原子指针，重注册时读写无 happens-before）；残留注释仍写"relaxed store/load"自相矛盾 | `obs-android.c:272-285,349-361,406-418,461-473` | libobs-core P1-4 |
| 7 | **插件解压失败无上报、无集合级原子性** —— 中途失败留下新旧 `.so` 混装并继续 `loadAllPlugins()`，ABI 不一致直接崩；`static bool done` 无同步；`unpack()` 丢弃 `extractTree()` 成败 | `AndroidRuntimeBootstrap.cpp:99-104,198-255` | frontend-android P1-1 |
| 8 | **`.managed` 回读未校验，存在路径穿越删除任意私有文件** | `AndroidRuntimeBootstrap.cpp:190-195`（MANIFEST 侧 `:158` 有 `!contains('/')`，账本侧无） | frontend-android P1-2 |
| 9 | **`ObsDisplayHost` 静态持有 Activity**，重建后崩溃/泄漏 | `ObsDisplayHost.java:88,301,631` | frontend-android P1-3 |
| 10 | **前台服务类型只在启动那一刻判定**；`START_STICKY` 后台重建带 while-in-use 类型 `startForeground` 会抛 `SecurityException` 后静默 `stopSelf()`；`specialUse` 基类型有 Play 审核被拒风险 | `ObsForegroundService.java:181-187,203,263-289`、`AndroidManifest.xml:112-115` | frontend-android P1-4 / lead |
| 11 | **`QtNative.activity()` JNI 签名错误**（用了 `()Landroid/content/Context;`，真实为 `()Landroid/app/Activity;`），第二 getter 必然失败 | `OBSAndroidUsb.cpp:35-41` | frontend-android P1-5 |
| 12 | **`QJniEnvironment` 多处未判 `isValid()` 就解引用**（插件视频/音频线程最需要判空的位置） | `OBSAndroidPermissions.cpp:271-274,315-317,389-398,413-416` | frontend-android P1-6 |
| 13 | **`android:allowBackup="true"` 且无备份排除规则**，推流密钥/场景随自动备份与设备迁移外泄 | `AndroidManifest.xml:100-101` | frontend-android P1-7 / lead |
| 14 | **死代码 `android-plugin-modules.h` + 停留在废弃方案的注释** | `os-android.cmake:150-156`、`feature-plugin-manager.cmake`、`plugins/CMakeLists.txt:40` | frontend-android P1-8 |
| 15 | **`always()` 保存依赖缓存会毒化后续构建**；`build.sh:162` 的 `set -e` 在 `if` 子 shell 中不生效导致 header-only 依赖"假成功"打永久 stamp；全脚本缺 `-e`（`build-apk.sh` 无产物仍可 exit 0） | `android-compile.yml:150-157`、`build.sh:162`、`defs.sh:25-38` | build-ci P1-1/2/3 |
| 16 | **CI `paths` 白名单漏核心构建文件**（根 `CMakeLists.txt`、`osconfig.cmake`、`deps/**` 等），改这些文件不触发构建；`.gitignore` 未忽略 `deps-android/{src,prebuilt}`，`git add -A` 会吞入数百 MB | `android-compile.yml:30-41`、`.gitignore` | build-ci P1-4/5 |
| 17 | **`OBSStudioAPI.cpp` 5 个前端 API getter 无守卫地把"崩溃/UB"改成 `nullptr`**（契约变化）；一批 NULL/未初始化加固混在 137 文件大 diff 里，上游永远看不到、也无法单独 revert | `OBSStudioAPI.cpp:404/420/430/610/677`、`OBSBasicSettings_Stream.cpp:105-107,1075`、`AutoConfigTestPage.cpp:1038-1040` | frontend-intrusion P1-1/2 |
| 18 | **`OBSProjector.cpp:340` 菜单定位孤例**：改 `event->globalPosition()` 却未走 `OBSMenuPos` 统一入口，桌面合成事件/触摸板/warp 场景行为改变，且两种定位策略并存 | `OBSProjector.cpp:337-340` | frontend-intrusion P0-2 |
| 19 | **音频总线未注册（Lead 与 plugins-android 双向核实）**：`obs_android_set_audio_host()` 全仓零调用点（`OBSBasic.cpp:250-259` 只注册了 Camera/Screen/USB 三条）；`obs_android_audio_ready()` 恒 false、`enum_devices()` 恒返回 -1、`has_permission()` 恒 false。而 `android-audio/aaudio-input.c:273-300,660-688` 完全依赖这三条做设备枚举与权限检查，`:665` 的 UI 文案「系统默认输入（音频总线未接通…）」正是降级提示，`:687-688` 还会误报"RECORD_AUDIO 未授予"；注释提到的 `ObsAudioHost.java` / `obs_audio_host.cpp` 全树不存在 ⇒ **Java 侧音频宿主也没实现**。定级 P1 而非 P0：采集可走 `AAUDIO_UNSPECIFIED` 默认设备，不崩不全静音，死掉的是"选哪台麦克风/耳机麦"整个 A4 功能 | `OBSBasic.cpp:248-264`、`aaudio-input.c:660-688`、`libobs/obs-android.c:363-398` | lead / plugins |
| 20 | **rtmp-services 两处无守卫改动污染桌面**：`rtmp-services-main.c:122-125` 把 `load_twitch_data()`/`load_amazon_ivs_data()` 移出 `#if defined(ENABLE_SERVICE_UPDATES)`（`git show 65c320b` 证实原在门禁内），桌面 `ENABLE_SERVICE_UPDATES=OFF` 也会 push 默认 ingest 并读本地缓存；`rtmp-common.c:373-379` 把 `LOG_DEBUG` 改成 `LOG_WARNING` 亦为全平台 | 同上 | plugins-android / lead |
| 21 | **obs-ffmpeg 4 个「注册但必然失败」的输出**：`obs-ffmpeg.c:351-355` 无条件注册 `ffmpeg_muxer`/`ffmpeg_hls_muxer`/`replay_buffer`/`ffmpeg_mpegts_muxer`，而 `obs-ffmpeg/CMakeLists.txt:24-33` 不构建伴生 `obs-ffmpeg-mux`（注释自认"一启动就报错"）⇒ 设置里可选但必失败；另 `ENABLE_NEW_MPEGTS_OUTPUT` 的 `set()` 在 `option()` 之前（依赖 CMP0077 NEW 才生效） | `obs-ffmpeg.c:351-355`、`obs-ffmpeg/CMakeLists.txt:8-16,24-33` | plugins-android / lead |
| 22 | **Dacast 静默回退到硬编码假 URL**：stub 下 `update_info_create_single` 恒 NULL、等待又被 `#ifndef __ANDROID__` 去掉，用户选 Dacast 会得到 `rtmp://dacast` + `"fake_key"` 且无任何告警 | `dacast.c:23,122-128`、`rtmp-common.c:913-919,936-941` | plugins-android |
| 23 | **UVC 拆除路径最长 5 秒阻塞 + 正常关闭也可能泄漏传输缓冲**：`uvc-input.c:1929-1941` 的 drain 循环最多 250×20ms 且**不检查 `shutting_down`**，`uvc_stop_thread()` 在 deactivate/destroy 线程 `pthread_join` ⇒ UI/视频线程 ANR；已完成挂在 `done[]` 的传输 `libusb_cancel_transfer` 返回 `NOT_FOUND` 不产生回调，回收判据 `drained >= submitted` 收不齐 ⇒ 走 `:1951-1954` 的"故意泄漏"分支（最多 32×64KB + 传输对象） | `uvc-input.c:1130-1149,1929-1954,2037-2046` | plugins-android |
| 24 | **camera2 / screen 的回调超时路径与重开共用同一 context**：等 2s 回调未退出就置 `cb_thread_maybe_live`、泄漏 reader、`reader=NULL`，`screen-capture.c:548` 还把 `shutting_down` 复位为 0；下次 `try_start` 用同一个 `c` 再挂 listener，泄漏的旧回调恢复执行 ⇒ 双线程同时写 `frames_out`/`in_callback`、同时 `obs_source_output_video` | `camera2-input.c:983-999,1062,1460-1467`、`screen-capture.c:528-548,734-741` | plugins-android |
| 25 | **JNI 从插件原生线程高频调用 + Surface 局部引用处置**：`screen-capture.c:303-362` 的泵每 10ms 在自己 `pthread_create` 的线程上经 `QJniObject::callStaticMethod` 跑到 Java（Qt 每次 attach/detach），`OBSAndroidPermissions.cpp:495-499` 自认"这条线程不在 JVM 的线程表里"；`ObsProjectionHost.java:149,292` 的 `captureSurface` 静态字段 `stopCapture` 只置 null、未 `Surface.release()` | 同上 | plugins-android |

> **关于 `.effect` 的风险程度，两位审查员判断不同**：libobs-core 认为"经论证语义等价、回归风险低但非零"；graphics-gles 认为 `float3→float4` 使 alpha 从未定义/0 变成 1.0，只要下游有 alpha 混合/预乘 alpha 路径画面就会变，且三个后端零测试覆盖。**共识**：无守卫、无桌面验收记录、`PORTING.md` 780 行 `git grep "effect|shader|GLSL|Adreno"` 零命中；应按 §4.3 首选方案（改 parser 而非改 effect）处理，或至少补齐三后端回归验证矩阵。

### P2 —— 风格 / 可维护性 / 稳健性（择要）

- **文档滞后/矛盾（5 项）**：① `PORTING.md:744` 待办「缺 microphone 前台类型」实际已实现（`ObsForegroundService.java:263-277`、`AndroidManifest.xml:76,112`），残留缺口只是"只在启动时判定"（P1-10），待办应**改写**而非直接勾掉；② `:758-759`「探针日志建议加默认关开关」已实现（`OBSAndroidDisplay.hpp:67-80`）；③ `:755-757`「`AndroidInputFilter` 未拆」实际已拆成 4 个类；④ `:296` 声称 4 个 Android 插件"可选设备/可禁用"，但音频总线根本没注册（P1-19），相机/屏幕/USB 三条也需逐项核实；⑤ `:280-282` 说"装载器不再用编译期白名单"，但 `os-android.cmake:150-156` 仍生成 `android-plugin-modules.h`、且注释称被 `OBSApp::loadAppModules()` 使用 —— 与 `PluginModuleLoader_Android.cpp:31-54` 的 `os_glob()` 实现冲突（该头文件全树无引用，是死代码，见 P1-14）。
- **文档错误/遗漏**：`:281-282` `add_default_module_paths()` 声明有误；`:636-639` release workflow 机制解释不准确（真因是 `on.push.branches` 过滤，paths 对 tag push 不评估）；`:643-653` 版本号实际散落 **4 处**；`:107`、`:158/:257`、`:327-328` 等多处行号漂移；`:703-710` 声称"被包含头自带守卫、无条件包含安全"**部分不成立**（`OBSAndroidDisplay.hpp` 等 4 个头没有整体守卫，照文档整改会破坏桌面）。
- **`PORTING.md` 缺 GLES 专节**：能力矩阵（GLES 3.1 必需、扩展门槛、P010/timer/dmabuf 不支持、CLAMP_TO_BORDER 降级、BGRA 换序）只存在于代码注释和**未入库**的 `plan.md` / `0907摄像头问题.md` / `.qoder/*.sh` 里。
- **`.github/release-notes.md:31` 事实错误**："Android 8.0（API 29）"（API 29 是 Android 10）。
- **CODESTYLE 批量违规**（见 §3.6）、**版权头归属错误**（全新文件署上游作者、4 个插件无头）、**中文注释**、**提交规范**（见 §3.9）。
- **AndroidManifest**：`requestLegacyExternalStorage="true"` + `WRITE_EXTERNAL_STORAGE` 在 targetSdk 35 下已无效（应转 MediaStore/SAF）；包名 `com.obsproject.studio` 用上游组织名；`sensorLandscape` 硬编码只做横屏。
- **分发合规**：仓库无 `THIRD-PARTY-LICENSES.md` / `NOTICE`，APK 实际随包分发 FFmpeg(LGPL)、x264(GPLv2+)、mbedtls(Apache-2.0)、curl、freetype、libusb/libuvc(LGPL)、glad(WTFPL/CC0 AND Apache-2.0)、Qt(LGPLv3/GPL)、fdk-aac(自定义)。LGPL/Apache 的"保留声明"义务需要汇总清单。
- **已知未修待办**（`PORTING.md:727-765`）：浮动 dock 崩溃、嵌套子菜单坐标错位、剩余 `CLAMP-BLOCKED` 对话框、正式版签名、录制/推流/屏幕采集/相机方向未做真机功能验证、工程债（过滤器拆分、探针、5 处重复实现、CI 缓存、构建可重现性）。
- **插件侧未验证项（从文件头注释可见）**：`camera2-input.c` 头注释自承"欠真机判据：色彩对不对、NV12 半平面、会话参数、被别的应用抢走相机"；`screen-capture.c` 头注释记录"系统内录在 MuMu 上样本恒零（AudioFlinger 没往 REMOTE_SUBMIX 发帧）"—— 这两项与 `PORTING.md:751` 的"未做真机功能验证"一致。
- **4 个插件的共性偏离**：属性页文案全是**硬编码中文**（未走 `obs_module_text()`/locale）、没有 `obs_module_description()`、没有 `data/` 目录资源、多处单行声明多变量违反 CODESTYLE；`PluginModuleLoader_Android.cpp:38` 与 `obs-core-modules-loader-android.c:49` 注释里的"14 个 core"实际是 **9 个**（与 `PORTING.md:295` 的口径不符）。
- **`REMOVE_ITEM` 的连带影响**：`plugins/CMakeLists.txt:175-179` 把 4 个 `android-*` 从 `OBS_MODULES_ENABLED` 剔除时，会连带把它们移出 `frontend/cmake/os-android.cmake:127` 的资源清单与其它消费者；本次因这 4 个插件没有 `data/` 故资源无影响，但属于"二次篡改全局属性"的隐式契约，见 §4.3 的声明式改造建议。

---

## 6. 其他需要审查的方面

- **测试与质量保障**：CI 只做编译与打包，**没有任何测试**（上游测试、`ctest`、静态分析、APK 内容验收均无）。4 个新采集插件零自动化测试，只靠真机人工。这是当前最大的工程缺口 —— 与代码质量本身无关，但直接决定了"已修的 P0 会不会回归"。作者自己修过 `plugins: 修 4 个 P0（screen 死锁 / 相机旋转 / AAudio 自愈 / screen OOM）` 与 `fix: 整体审查后的 P0（5 处会崩/UAF + 4 处 clamp 回归 + 1 处性能）`，说明崩溃类问题确实靠人工发现，回归护栏缺失的风险是实的。
- **合规与法律**：FDK-AAC（P0）、第三方许可证清单缺失、原始移植来源署名不清（`PORTING.md:6` 提到 cunminF 但提交无署名）、每次新建 keystore、`allowBackup` 数据外泄、Play 的 `specialUse` 前台服务申报。
- **可重现性**：`x264`/`rnnoise` 钉 `master`、无 checksum、`apksigner`/`aapt2` 用 `sort -V | tail -1` 取最新、每次新 keystore、glad 生成脚本未入库 —— 同一提交两次构建可能产出不同 APK。
- **提交与协作**：58 提交 / 1 merge / 4 个超大导入提交；若未来要向"上游贡献"或做代码审计，需大幅整理历史。
- **长期维护成本**：76 个上游文件被修改、其中 60 个是 frontend 高频文件；`PORTING.md` 已记录多次"rebase 上游后踩坑"（提前 `return()` 跳过 `set_obs_core_modules()`、Qt 版本、`OBS_VERSION_OVERRIDE`）。这是 fork 模式的固有成本，建议 §4.3 的"平台委托收敛"作为中期目标。

---

## 7. 结论与行动建议

### 定位判断

- **目标 A：长期维护的独立 fork**（当前事实定位）—— 现状**基本成立**：改动以新增文件与条件编译为主、上游历史完整、文档与 CI 齐备、真机验证驱动。按下面的清单补齐后可以发布。
- **目标 B：贡献回上游** —— 按现状**几乎不可能**：需拆成数十个逻辑提交、英文化全部注释、移除 FDK、把前端散落 `#ifdef` 重构为上游式平台后端、补版权头与测试、处理 `specialUse` 等政策问题；且 OBS 上游目前没有 Android 支持的既定计划。

### 分阶段行动清单

| 阶段 | 必做项 |
|---|---|
| **发版前（阻塞）** | ① 移除/替换 FDK-AAC 或取得法律意见；② 参数化 keystore + 固定 release 密钥；③ 依赖钉 SHA + checksum + 缓存 key；④ CI 加 APK 插件清单验收；⑤ 修浮动 dock 崩溃或禁用 `DockWidgetFloatable`；⑥ 删除 `OBSBasicFilters::showEvent`；⑦ 修 `android-screen` 音频泵竞态（P0-7）与 `uvc-input.c` 拆除 5 秒阻塞（P1-23）；⑧ 补 `THIRD-PARTY-LICENSES.md`；⑨ 至少一轮录制/推流/屏幕采集/相机方向的真机功能验证 |
| **短期（1~2 个迭代）** | ① 改 `gl-shaderparser.c` 而非共享 `.effect`（零成本）；② 删 `obs.h` include 块 / 死代码 / 生成模板；③ 插件解压原子化 + 失败上报 + `.managed` 白名单；④ JNI 签名与 `isValid()` 判空；⑤ 注册音频总线；⑥ `allowBackup` 与 `START_STICKY`/FGS 类型；⑦ 修 `OBSProjector.cpp:340`、`rtmp-services-main.c`、`OBSBasic_ContextToolbar.cpp:212` 的无条件改动；⑧ 同步 `PORTING.md` 的 3 项滞后待办与错误声明；⑨ **先补头守卫再执行 include 整改** |
| **中期** | ① 把 frontend 的 126 条 `#ifdef` 收敛为平台委托，`OBSApp` 过滤器与 clamp/flick 彻底拆开；② GLES 兼容头 + 能力表（消除 `struct gs_device` 分叉）；③ 加 host 侧单测与静态检查；④ 统一日志通道与目录分层；⑤ 把 `.ui` 尺寸假设改为平台无关布局策略；⑥ 修版权头、提交规范、中文注释策略 |
| **长期** | ① 评估独立 `libobs-gles/` 后端目录（一次分叉、之后互不干扰）；② 建立上游 rebase 回归清单；③ 若计划上架，处理 Play 的 FGS 声明、分区存储、包名与签名策略 |

### 一句话结论

> 这**不是**一个"把 OBS 硬塞进 Android"的粗暴移植：底层平台抽象、core module 机制、插件分发与 CI 设计都显示出对上游架构的理解，个别抽象（`gs_format_needs_rb_swap` 模式）甚至优于常见的 `#ifdef` 打法。它的问题集中在三处：**Qt 前端层的条件编译散落**、**合规/发布工程链路的硬编码**、**文档与实现之间的漂移**。前两者是"能不能长期维护 / 能不能发版"的问题，不是"移植对不对"的问题。

---

## 附录 A：数据口径与复现命令

```bash
git merge-base HEAD upstream/master                      # 65c320b = tag 33.0.1
git diff --stat upstream/master...HEAD                   # 137 files, +21932 / -148
git diff --name-status upstream/master...HEAD            # A=61, M=76
git diff --numstat upstream/master...HEAD                # 按目录汇总
git log --oneline 65c320b..HEAD                         # 58 commits, 1 merge
git grep -c '__ANDROID__' HEAD -- $(git diff --name-only upstream/master...HEAD)   # 60 文件 / 163 次
git show 65c320b:frontend/widgets/OBSBasic_Streaming.cpp | sed -n '470,480p'     # 确认包装函数上游已有
git grep -n 'obs_android_set_audio_host' HEAD -- '*.c' '*.cpp' '*.h'             # 音频总线零调用点
```

## 附录 B：详细分报告索引

| 报告 | 内容 | 规模 |
|---|---|---|
| [`.review/build-ci.md`](.review/build-ci.md) | 构建/CI/打包/依赖；3×P0、7×P1、12×P2；`PORTING.md` 33 条核验（属实 22 / 存疑 5 / 有误或不完整 6） | 542 行 |
| [`.review/libobs-core.md`](.review/libobs-core.md) | libobs 核心与平台抽象；上游风格逐项 verdict；更优方案 A~F | 409 行 |
| [`.review/graphics-gles.md`](.review/graphics-gles.md) | GLES 后端；跨后端 effect 分析；方案 A/B/C；4×P1、11×P2 | 204 行 |
| [`.review/frontend-intrusion.md`](.review/frontend-intrusion.md) | frontend 对上游文件的侵入性；逐文件量化表、无守卫改动单列、风格 verdict | ~300 行 |
| [`.review/frontend-android.md`](.review/frontend-android.md) | 新增 Android 实现；8×P1、19×P2；JNI/生命周期/`PORTING.md` 核验表 | 350 行 |
| [`.review/plugins-android.md`](.review/plugins-android.md) | 4 个采集插件逐个评估；2×P0、8×P1；FDK-AAC 法律证据链；`PORTING.md` 核验 | ~370 行 |
| [`.review/lead-global.md`](.review/lead-global.md) | Lead 全局量化、提交历史、合规与定位 | ~150 行 |

> 6 个方向的领域报告全部齐备。其中"音频总线未注册"由 Lead 与 plugins-android 双向独立核实；`add_default_module_paths()` 死代码由 libobs-core 与 frontend-android 的注释交叉印证；FDK-AAC 合规由 build-ci 与 plugins-android 分别给出证据链。
