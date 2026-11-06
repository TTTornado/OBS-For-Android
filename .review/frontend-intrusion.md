# Qt 前端：对上游文件的修改侵入性审查

> 审查人：`frontend-intrusion`（只读审查，未修改任何源码文件）
> 基线：`upstream/master` = `65c320b46e1af5c7a555161aea6182cdf7a8c524`（33.0.1），HEAD = `dc6e156d60e94e65fb847630e0ef1769ab101b15`
> 主要命令：`git diff upstream/master...HEAD -- <path>`、`git show 65c320b:<path>`、`git grep -n ... HEAD -- frontend/`、`git diff --numstat`、`git diff --name-status`

---

# 范围

只审 `frontend/` 下被修改（M）的**上游文件**，共 **39 个**；新增（A）文件（如 `OBSAndroidDisplay.cpp`、`android-window-fit.hpp`、`os-android.cmake`、`CrashHandler_Android.cpp` 等）由 `frontend-android` 审，本报告只在需要佐证时引用其内容。

统计量（命令输出）：

```
git diff --numstat upstream/master...HEAD          → 137 files, +21932 / -148
git diff --numstat upstream/master...HEAD -- frontend/ → 67 files, +7389 / -76   (39 M + 28 A)
仅 39 个 M 文件                                    → 39 files, +1939 / -76
```

39 个 M 文件：`frontend/CMakeLists.txt`、`frontend/cmake/feature-plugin-manager.cmake`、`frontend/OBSApp.cpp`、`OBSStudioAPI.cpp`、`obs-main.cpp`、`components/{Multiview,SceneTree,SourceTreeDelegate}.cpp`、`dialogs/{OBSBasicFilters.cpp/hpp,OBSBasicProperties,OBSBasicSourceSelect,OBSLogViewer}.cpp`、`settings/{OBSBasicSettings.cpp,OBSBasicSettings_Stream.cpp}`、`utility/{CrashHandler.hpp,SurfaceEventFilter.hpp}`、`widgets/{AudioMixer,OBSBasic.cpp/hpp,OBSBasicControls.cpp/hpp,OBSBasic_ContextToolbar,OBSBasic_Docks,OBSBasic_MainControls,OBSBasic_Preview,OBSBasic_Projectors,OBSBasic_Recording,OBSBasic_ReplayBuffer,OBSBasic_SceneItems,OBSBasic_Scenes,OBSBasic_Streaming,OBSBasic_StudioMode,OBSBasic_Transitions,OBSBasic_VirtualCam,OBSProjector,OBSQTDisplay.cpp/hpp}.cpp`、`wizards/AutoConfigTestPage.cpp`。

关键规范依据：`CODESTYLE.md:199`（"All new C++ code needs to use curly braces … No exceptions"）、`CODESTYLE.md:190`（新常量 `kCamelCase`）、`CODESTYLE.md:285-287`（include 块内 case-sensitive 字母序；"Do not create platform specific include blocks"；"Instead of using a macro to conditionally compile code … well, don't do that at all"；应"Refactor the source file to be platform-specific and use CMake to add the appropriate file to the target"）、`.clang-format:84`（`SortIncludes: false`）、`.clang-format:110`（C++ `InsertBraces: true`）、`.clang-format:46`（`ColumnLimit: 120`）。

---

# 做了什么

按主题概括移植对这 39 个上游文件做的事：

| 主题 | 代表性改动 | 主要落点 |
|---|---|---|
| **尺寸/布局适配** | `.ui` 写死的 `minimumSize` 在构造函数里放宽（`setMinimumHeight(0)` / `setMinimumSize(0,0)` / `setSizeAdjustPolicy(AdjustIgnored)`）；`show()` 前调 `androidFitTopLevelToScreen()`；`OBSApp.cpp` 里 `Show` 后 `QTimer::singleShot(0)` 全局兜底 `clampTopLevelToScreen` + `CLAMP-BLOCKED` 诊断 | `dialogs/OBSBasicSourceSelect.cpp:239-265`、`OBSBasicProperties.cpp:65-91`、`OBSBasicFilters.cpp:68-79`、`settings/OBSBasicSettings.cpp:359-371`、`OBSApp.cpp:1016-1066` |
| **首页 dock / 工具栏 / 行高** | `ResizeAndroidDocks()` 按 `sizeHint()` 推导比例并带上界 280；工具栏按钮尺寸从 `transitionAdd->sizeHint()` 推导 + QSS；场景/源列表行高 32dp | `widgets/OBSBasic_Docks.cpp:24-58`、`OBSBasic.cpp:1276-1340`、`components/SceneTree.cpp:28-32`、`SourceTreeDelegate.cpp:16-20` |
| **触摸与事件过滤** | 4 个 app 级过滤器：BACK→Escape、组合框弹层补丁(P-8)、长按→右键、顶层窗 Show 后 clamp+挂 `QScroller`；`enableFlickScroll()` 对 `QAbstractItemView` 用 `LeftMouseButtonGesture`、`QScrollArea` 用 `TouchGesture` | `OBSApp.cpp:1156/1211/1265/1451`、`OBSApp.cpp:1087-1131`、`OBSApp.cpp:1487-1490` |
| **菜单定位** | 新增 `AndroidMenuPos.hpp`（A 文件）提供 `OBSMenuPos()` / `OBSMenuPosBelow()` / `OBSMenuPosCenter()`，把上游成片的 `exec(QCursor::pos())` 换成锚点定位；**非 Android 恒退回 `QCursor::pos()`**（`AndroidMenuPos.hpp:44-47`） | `OBSBasic_Preview.cpp:245/263`、`OBSBasic_Scenes.cpp:636`、`OBSBasic_StudioMode.cpp:114/172/395`、`OBSBasic_SceneItems.cpp:769/815`、`OBSBasicFilters.cpp:763/809/923`、`AudioMixer.cpp:874`、`OBSBasic_Transitions.cpp:553/673` |
| **预览显示层（路线 B）** | 预览从 Qt `winId` 换成 Java `SurfaceView`→`ANativeWindow`；`OBSQTDisplay` 拆出 `releaseDisplay()` / `createDisplayFromExistingSurface()` / `onAndroidSurfaceEvent()` / `syncAndroidSurfaceGeometry()`；`SurfaceEventFilter` 在 Android 上不再 `DestroyDisplay()`；`SurfaceEventFilter.hpp` 阻止 insets 抖动销毁预览 | `widgets/OBSQTDisplay.cpp:23-31,68-110,117-141,150-196,201-310,320-322,330-332`、`OBSQTDisplay.hpp:29-48`、`utility/SurfaceEventFilter.hpp:28-42` |
| **模态遮挡 / 显示层可见性** | `androidSyncPreviewVisibility()` 用 `activeModalWidget()/activePopupWidget()` + 扫 `topLevelWidgets()` 现算是否藏面；挂在 `OBSApp::notify()` 最前面 | `OBSApp.cpp:2052-2097`、`OBSApp.cpp:2099-2120`、`OBSApp.cpp:2133-2135` |
| **☰ 菜单 / 一键前后置 / 控制按钮两列** | Android 上 `QMenuBar` 不渲染，镜像成 ☰ 弹出菜单；新增"前后置"按钮；把竖列按钮重排成两列 | `widgets/OBSBasicControls.cpp:25-163`、`OBSBasic_ContextToolbar.cpp:209-224,321-397` |
| **录制/推流/容器格式限制** | 设置页下拉裁掉非 hybrid 容器（`#ifndef __ANDROID__` 保留桌面顺序）、去掉 Lossless / FFmpeg 自定义输出档；profile 落地时纠正 `RecFormat2` / `RecQuality` / `RecType`；旧配置回落第一档 | `settings/OBSBasicSettings.cpp:953-958,1117-1136,1832-1841,2064-2073,4960-4972`、`OBSBasic.cpp:854-897` |
| **投影仪** | `defaultProjectorSize()` 480×270 只在装不下时等比缩小；`SetMonitor` 按主屏几何摆；`show()` 之后补一次 resize；删除投影仪前先 `hide()` | `widgets/OBSProjector.cpp:25-46,76-77,126-135,161-171,474`、`OBSBasic_Projectors.cpp:144-150` |
| **崩溃处理桥接** | `PlatformType` 枚举追加 `Android`；新增 `CrashHandler_Android.cpp`（A）由 CMake 按平台选择；Android 上跳过 unclean-shutdown 模态框 | `utility/CrashHandler.hpp:33`、`OBSApp.cpp:140-150` |
| **构建系统** | `qt_add_executable` 替代 `add_executable`；`OBS::studio` 别名按库建；`include(cmake/os-android.cmake)`；`feature-plugin-manager.cmake` 按平台选 `PluginModuleLoader_Android.cpp` | `frontend/CMakeLists.txt:11-33,101-103,123-131,144-152`、`feature-plugin-manager.cmake:10-14` |
| **运行时路径 / 插件解包 / openUrl 替代** | `PrepareAndroidRuntimePaths()`（`main()` 最前）、`EnsureAndroidPluginBinaries()`（`loadAppModules()` 内）、`obsAndroidShowLocalPath()` 替代 `QDesktopServices::openUrl` | `obs-main.cpp:27-33`、`OBSApp.cpp:2698-2708`、`OBSBasic_MainControls.cpp:319-325,472-491`、`OBSLogViewer.cpp:129-135`、`OBSBasic_Recording.cpp:40-46`、`OBSApp.cpp:1984-1993` |
| **前台保活 / 权限总线 / USB 总线** | `OBSBasic` 构造函数里用信号+1 秒看门狗读 `StreamingActive()/RecordingActive()` 真相；注册相机/屏幕采集/USB 三条 libobs 总线 | `OBSBasic.cpp:183-275` |
| **向导 / 服务态探针** | 首启跳过模态自动配置向导（`#else` 保留桌面）；多处 `OBS_PROBE_LOG` 探针 | `OBSBasic.cpp:1372-1386`、`OBSApp.cpp:1087-1093` |
| **NULL 安全加固（多为无条件）** | `obs_frontend_get_*_output()` 加 `outputHandler` 空检查；`strcmp(type, …)` 加 `type &&`；未初始化局部变量清零；`Active()` 包装函数替代裸 `outputHandler->Active()` | `OBSStudioAPI.cpp:404/420/430/610/677`、`OBSBasicSettings_Stream.cpp:105-107,1075,1482+,1692+`、`wizards/AutoConfigTestPage.cpp:1038-1040`、`OBSBasic_Streaming.cpp:134/176/393` 等 |

---

# 侵入性评估

## 量化表格

列说明：**Android 条件指令数** = HEAD 文件中形如 `#if/#ifdef/#ifndef/#elif/#else/#endif` 且含 `__ANDROID__` 的行数（`git grep` 统计）；**`Q_OS_ANDROID`** 全 frontend 为 **0**（命令输出：`git grep -n "Q_OS_ANDROID" HEAD -- frontend/` → 0 行）；全树 `__ANDROID__` 出现 **165** 次、其中条件编译指令 **147** 条（`git grep -n "__ANDROID__" HEAD` 统计）；frontend 内 `__ANDROID__` 出现 104 次。

风险等级：**高** = 非 Android 路径的行为被无条件改变；**中** = 非 Android 路径的编译产物/代码形态被改变、运行期行为在常见桌面条件下不变；**低** = 改动整体被 `#ifdef`/`#else` 或 `#ifndef` 正确隔离，桌面路径逐字保留；**无** = 仅 CMake 层 `if(OS_ANDROID)` 分支。

| 文件 | +行 | -行 | Android 条件指令 | `Q_OS_ANDROID` | 桌面回归风险 | 说明 |
|---|---:|---:|---:|---:|---|---|
| `frontend/OBSApp.cpp` | 629 | 3 | 12 | 0 | **中** | 4 个过滤器 + clamp/flick + 模态遮挡 + 保活全在 `#ifdef` 内；但 `notify()` 注入、`handleUncleanShutdown` `#else`、`#if … && !defined(__ANDROID__)` 扩展、include 块、`<QCoreApplication>` 等改变共享文件形态 |
| `frontend/widgets/OBSQTDisplay.cpp` | 251 | 2 | 12 | 0 | **中** | `#else` 保留桌面 `CreateDisplay`；但 `~OBSQTDisplay()`、`DestroyDisplay()`、`releaseDisplay()` 由内联改成外联、无条件编译进桌面 |
| `frontend/widgets/OBSBasic.cpp` | 228 | 0 | 7 | 0 | **中** | 全部 Android 逻辑在 `#ifdef` 内；无条件新增 `#include <QCoreApplication>`（`OBSBasic.cpp:71`） |
| `frontend/widgets/OBSBasicControls.cpp` | 149 | 0 | 2 | 0 | 低 | 整块 `#ifdef __ANDROID__` 构造函数内 |
| `frontend/settings/OBSBasicSettings.cpp` | 74 | 2 | 10 | 0 | **中** | 容器裁表用 `#ifndef` 保留桌面顺序；但 `DisableIncompatibleSimpleCodecs/Container` 的 NULL 加固无条件生效（diff `@@ -5311,7 +5377,8 @@`、`@@ -5338,7 +5405,12 @@`） |
| `frontend/widgets/OBSProjector.cpp` | 51 | 3 | 3 | 0 | **高** | `popup.exec(event->globalPosition().toPoint())`（`:340`）无条件；`defaultProjectorSize()`/`resize()` 无条件新增 |
| `frontend/settings/OBSBasicSettings_Stream.cpp` | 49 | 18 | 1 | 0 | **中** | 几乎全是空指针/未初始化加固，无条件生效；仅 1 处 `#ifdef __ANDROID__` 探针 |
| `frontend/widgets/OBSBasic_ContextToolbar.cpp` | 93 | 2 | 3 | 0 | **中** | `OBSSceneItem item = GetCurrentSceneItem();` 被无条件提到可见性闸门之前（`:212`） |
| `frontend/dialogs/OBSBasicFilters.cpp` | 36 | 3 | 2 | 0 | **高** | `showEvent()` 实现（`:633-645`）**无条件**在 QShowEvent 里 resize |
| `frontend/dialogs/OBSBasicSourceSelect.cpp` | 29 | 0 | 2 | 0 | 低 | 构造函数改动整块 `#ifdef` |
| `frontend/dialogs/OBSBasicProperties.cpp` | 28 | 0 | 3 | 0 | 低 | 两处 `#ifdef`，含 `resize(cx,cy)` 之后调 fit |
| `frontend/widgets/OBSBasic_Transitions.cpp` | 27 | 3 | 3 | 0 | **中** | `transitionUuids.empty()` 守卫（`:1507`）与两处菜单定位无条件 |
| `frontend/widgets/OBSQTDisplay.hpp` | 24 | 6 | 1 | 0 | **中** | 析构函数声明 `~OBSQTDisplay() override;`（`:29`）、`DestroyDisplay()` 去内联、`releaseDisplay()` 声明均无条件 |
| `frontend/widgets/OBSBasic_SceneItems.cpp` | 23 | 3 | 4 | 0 | **中** | `CreateSourcePopupMenu` 签名改（新增第 3 参）、`on_sources_customContextMenuRequested` 传坐标、`popup.exec(OBSMenuPos(...))` 无条件 |
| `frontend/OBSStudioAPI.cpp` | 20 | 0 | 0 | 0 | **高** | 5 处 `if (!main->outputHandler) return nullptr;` 无条件 |
| `frontend/widgets/OBSBasic_MainControls.cpp` | 17 | 2 | 4 | 0 | 低 | 3 处 `#ifdef/#else`，桌面分支逐字保留 |
| `frontend/widgets/OBSBasic.hpp` | 14 | 2 | 2 | 0 | **中** | `CreateSourcePopupMenu`（`:1269`）与 `ProgramViewContextMenuRequested`（`:1464`）签名无条件改（带默认实参） |
| `frontend/widgets/OBSBasic_Docks.cpp` | 39 | 0 | 2 | 0 | 低 | `ResizeAndroidDocks()` 与调用点均 `#ifdef` |
| `frontend/obs-main.cpp` | 11 | 0 | 2 | 0 | 低 | `PrepareAndroidRuntimePaths()` 与 include 均 `#ifdef` |
| `frontend/utility/SurfaceEventFilter.hpp` | 10 | 0 | 1 | 0 | 低 | `#ifdef __ANDROID__ break; #else DestroyDisplay(); #endif` 保留桌面 |
| `frontend/widgets/OBSBasicControls.hpp` | 9 | 0 | 2 | 0 | 低 | 成员与 signal 均 `#ifdef` |
| `frontend/widgets/OBSBasic_Recording.cpp` | 10 | 2 | 2 | 0 | **中** | openUrl `#ifdef/#else` 保留；3 处 `RecordingActive()` 包装调用无条件 |
| `frontend/dialogs/OBSLogViewer.cpp` | 9 | 2 | 2 | 0 | 低 | `#ifdef/#else` 保留桌面 |
| `frontend/wizards/AutoConfigTestPage.cpp` | 7 | 2 | 0 | 0 | **中** | `res_count = 0; maxFPS = 0;`（`:1038-1040`）无条件 |
| `frontend/components/Multiview.cpp` | 7 | 0 | 1 | 0 | 低 | `#elif defined(__ANDROID__)` 扩展上游平台链，桌面 `#else` 不变 |
| `frontend/widgets/OBSBasic_StudioMode.cpp` | 7 | 6 | 0 | 0 | **中** | include + 3 处 `OBSMenuPos` + `ProgramViewContextMenuRequested` 签名 + lambda 捕获全部无条件 |
| `frontend/widgets/OBSBasic_Projectors.cpp` | 7 | 0 | 1 | 0 | 低 | `hide()` 在 `#ifdef` 内 |
| `frontend/components/SourceTreeDelegate.cpp` | 6 | 1 | 1 | 0 | **中** | `return (QSize(item->sizeHint()));` 被无条件改写为局部变量 `hint`（`:16-20`） |
| `frontend/widgets/AudioMixer.cpp` | 5 | 1 | 0 | 0 | **中** | 无条件 include `AndroidMenuPos.hpp` + `popup(OBSMenuPos(...))` |
| `frontend/cmake/feature-plugin-manager.cmake` | 5 | 1 | 0 | 0 | 无 | `if(OS_ANDROID)/elseif(OS_WINDOWS)`，桌面分支不变 |
| `frontend/dialogs/OBSBasicFilters.hpp` | 4 | 0 | 0 | 0 | **高** | `virtual void showEvent(QShowEvent*) override;`（`:128`）无条件新增 |
| `frontend/components/SceneTree.cpp` | 4 | 0 | 1 | 0 | 低 | `#ifdef/#else` 保留 `setStyleSheet("")` |
| `frontend/widgets/OBSBasic_Preview.cpp` | 3 | 2 | 0 | 0 | **中** | include + `CreateSourcePopupMenu` 第 3 参 + `popup.exec(OBSMenuPos(...))` 无条件 |
| `frontend/widgets/OBSBasic_Streaming.cpp` | 3 | 3 | 0 | 0 | **中** | 3 处 `StreamingActive()` 包装调用无条件 |
| `frontend/widgets/OBSBasic_Scenes.cpp` | 2 | 1 | 0 | 0 | **中** | include + `popup.exec(OBSMenuPos(...))` 无条件 |
| `frontend/widgets/OBSBasic_VirtualCam.cpp` | 1 | 1 | 0 | 0 | **中** | `VirtualCamActive()` 包装调用无条件 |
| `frontend/widgets/OBSBasic_ReplayBuffer.cpp` | 1 | 1 | 0 | 0 | **中** | `ReplayBufferActive()` 包装调用无条件 |
| `frontend/utility/CrashHandler.hpp` | 1 | 1 | 0 | 0 | **中** | `enum class PlatformType { …, Android };`（`:33`）无条件追加枚举值 |
| `frontend/CMakeLists.txt` | 46 | 3 | 0 | 0 | 无 | 5 处 `if(OS_ANDROID)/else`；桌面 `add_executable`、`OBS::studio` 别名、`OUTPUT_NAME "$<IF:…>"` 逐字保留 |

> 表格中"Android 条件指令"为 0 的 C++ 文件即 lead 指出的「无守卫的上游改动」，共 **13 个纯无守卫 + 若干混合文件**，下面单列。

## 单列：无守卫的上游改动（最危险的一类）

逐个用 `git diff upstream/master...HEAD -- <file>` 核对：

| 文件 | 改了什么（无条件） | 桌面/Linux/macOS 影响 | 判定 |
|---|---|---|---|
| `dialogs/OBSBasicFilters.hpp:128` | 新增 `showEvent` 声明 | 头文件对所有平台改动，新增虚覆写 | **行为改变** |
| `dialogs/OBSBasicFilters.cpp:633-645` | `QShowEvent` 里 `resize()` 收高度 | 见 P0-1；**所有平台**每次 show 都可能改窗口尺寸 | **行为改变** |
| `OBSStudioAPI.cpp:404/420/430/610/677` | 5 个 getter 加 `if (!main->outputHandler) return nullptr;` | 原来 null 会解引用崩溃，现返回 `nullptr`；改变前端 API 契约，编译产物改变 | **行为改变（边缘），良性但无守卫** |
| `widgets/OBSProjector.cpp:340` | `popup.exec(QCursor::pos())` → `popup.exec(event->globalPosition().toPoint())` | 右键菜单定位点改成"按下点"；合成事件/平板/指针 warp 时与 `QCursor::pos()` 不等；且未走 `OBSMenuPos` 统一入口 | **行为改变** |
| `widgets/OBSProjector.cpp:25-46,76-77,474` | 新增 `defaultProjectorSize()`，两处 `resize(480,270)` → `resize(defaultProjectorSize(this))` | `#else Q_UNUSED(w); return QSize(480,270)` ⇒ 常规桌面等价，但共享函数新增、调用形态改变 | 编译产物改变 |
| `utility/CrashHandler.hpp:33` | `PlatformType` 追加 `Android` | 追加在末尾，既有枚举数值不变；`CrashHandler.cpp:81-91` 的 switch 有 `default:`（`CrashHandler.cpp:89`），不会触发 `-Wswitch`+`-Werror` | 低，但共享头文件被改 |
| `wizards/AutoConfigTestPage.cpp:1038-1040` | `size_t res_count;` / `int maxFPS;` → 带 `= 0` 初始化 | 修的是上游真实缺陷（service 无效时读栈垃圾），桌面同样受益 | **良性 bugfix，应独立提上游** |
| `settings/OBSBasicSettings_Stream.cpp:9,105-107,1075,1482+,1521+,1692+,1723+,1730+` | 无条件 include `widgets/OBSAndroidDisplay.hpp`；`type &&` 守卫；`vbitrate/abitrate/res_count/fps` 初始化；`output` 判空后才 `strlist_split` | 常见桌面路径等价；`output==NULL` 时原来崩溃/UB，现 `output_codecs` 保持 `nullptr` 由 `service_supports_codec(NULL,…)` 返回 true | **边缘行为改变，良性** |
| `widgets/OBSBasic_Preview.cpp:22,245,263`、`OBSBasic_Scenes.cpp:24,636`、`AudioMixer.cpp:22,874`、`OBSBasic_StudioMode.cpp:23,114,172,393`、`OBSBasic_SceneItems.cpp:17,577,769,815`、`OBSBasicFilters.cpp:22,763,809,923`、`OBSBasic_Transitions.cpp:31,553,673` | 无条件 include `AndroidMenuPos.hpp`；`exec(QCursor::pos())` → `exec(OBSMenuPos(锚点))`；`CreateSourcePopupMenu` 加第 3 参 | **结果桌面中性**：`AndroidMenuPos.hpp:44-47` 在非 Android 恒 `Q_UNUSED(androidGlobalPos); return QCursor::pos()`；锚点计算仍被求值但被丢弃。代价是桌面 TU 多了一个 Android 命名头文件、签名/调用形态改变 | **编译产物改变，行为中性** |
| `widgets/OBSBasic_Transitions.cpp:1507` | `UpdateCurrentTransition(transitionUuids.back(), true)` 包进 `if (!transitionUuids.empty())` | 桌面 `transitionUuids` 恒非空，注释自承"对它是 no-op"（`OBSBasic_Transitions.cpp:1501-1506`） | 行为中性 |
| `widgets/OBSBasic_ContextToolbar.cpp:212` | `OBSSceneItem item = GetCurrentSceneItem();` 从可见性闸门之后提到函数开头 | 原来 `!contextContainer->isVisible() && !force` 早退时**不调用** `GetCurrentSceneItem()`，现在每次都调 | **行为改变（轻微）** |
| `widgets/OBSBasic.hpp:1269,1464` / `OBSBasic_StudioMode.cpp:378` / `OBSBasic_SceneItems.cpp:577` | `CreateSourcePopupMenu`/`ProgramViewContextMenuRequested` 签名改（新增带默认值的 `QPoint` 参） | 默认实参保证既有调用点仍编译；moc 的 slot 签名改变 | 编译产物改变 |
| `widgets/OBSBasic_Streaming.cpp:134/176/393`、`OBSBasic_Recording.cpp:163/267`、`OBSBasic_ReplayBuffer.cpp:33`、`OBSBasic_VirtualCam.cpp:108` | `outputHandler->XxxActive()` → `XxxActive()` | **核实**：`OBSBasic::StreamingActive()` 等 4 个包装函数**上游基线已存在且已带空指针检查**（`git show 65c320b:frontend/widgets/OBSBasic_Streaming.cpp:473`、`…/OBSBasic_Recording.cpp:281`、`…/OBSBasic_ReplayBuffer.cpp:232`、`…/OBSBasic_VirtualCam.cpp:179`），移植只改了调用点。桌面正常路径完全等价，null 时由崩溃变 `false` | **行为中性（防御性）**；见 P1-3 关于"是否应作为独立 bugfix"的判断 |
| `widgets/OBSQTDisplay.cpp:78-82,124-140` / `OBSQTDisplay.hpp:29,38-48` | `~OBSQTDisplay()`、`DestroyDisplay()` 去内联，新增 `releaseDisplay()`；`display = nullptr` → `releaseDisplay()` | `releaseDisplay()` 非 Android 分支只有 `display = nullptr;`（`OBSQTDisplay.cpp:140-151`），等价；但共享显示核心被重构 | 编译产物改变 |
| `widgets/OBSBasic.cpp:71` | 无条件 `#include <QCoreApplication>` | 桌面多一个 include | 编译产物改变 |
| `CMakeLists.txt` / `feature-plugin-manager.cmake` | CMake 层 `if(OS_ANDROID)` 分支 | **不是 C 预处理意义的"无守卫"**：`frontend/CMakeLists.txt:23-25,31-33,74-76` 的 `else` 分支与上游逐字相同（可用 `git show 65c320b:frontend/CMakeLists.txt` 对比）；`feature-plugin-manager.cmake:14` 起 `elseif(OS_WINDOWS)` 链不变 | **桌面无影响** |

**结论**：真正会改变桌面运行期行为的无守卫改动集中在 4 处 —— `OBSBasicFilters.cpp:633-645`（P0）、`OBSProjector.cpp:340`（P1）、`OBSStudioAPI.cpp:404…`（P1，边缘）、`OBSBasic_ContextToolbar.cpp:212`（P2）；其余大多是"编译产物改变、行为中性"或"良性 bugfix 但没走守卫/没单独提上游"。

---

# 是否符合上游多平台风格（逐项 verdict）

上游惯例（命令证据）：

- `git show 65c320b:frontend/OBSApp.cpp | Select-String '#\s*if…'`：`_WIN32`、`__APPLE__`、`__linux__`、`Q_OS_UNIX` 混用；平台差异写成 `#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)` 这类**在既有链条里扩展**，而不是插一个孤立 `#ifdef`。
- `git ls-tree -r --name-only 65c320b frontend/utility`：`CrashHandler_Windows.cpp` / `CrashHandler_Linux.cpp` / `CrashHandler_MacOS.mm` / `CrashHandler_FreeBSD.cpp`、`platform-windows.cpp` / `platform-x11.cpp` / `platform-osx.mm`、`system-info-windows.cpp` / `system-info-posix.cpp` / `system-info-macos.mm` —— **平台专有实现收敛到独立编译单元**。
- `git show 65c320b:frontend/CMakeLists.txt`：`if(OS_WINDOWS) include(cmake/os-windows.cmake) elseif(OS_MACOS) … elseif(OS_LINUX) …` —— CMake 层按 `OS_*` 选源文件。
- `git show 65c320b:frontend/OBSApp.cpp`：`installNativeEventFilter(new OBS::NativeEventFilter)`、`OBSEventFilter`（`OBSApp.cpp:26` 附近）—— 上游已有 app 级事件过滤器，但**专用于原生事件且实现在独立头/编译单元**。

| 项 | verdict | 证据 |
|---|---|---|
| **把 ~629 行、4 大职责的 Android 逻辑塞进 `OBSApp.cpp`** | **不符合**。上游把平台专有实现放进 `platform-*.cpp` / `CrashHandler_*.cpp` / `os-*.cmake`；`OBSApp.cpp` 只放少量短 `#ifdef`。此处 4 个过滤器 + clamp/flick + 模态遮挡 + `notify()` 注入全部写在共享 app 文件 | `OBSApp.cpp:1016-1478`（约 460 行新增）、`OBSApp.cpp:2052-2120`、`OBSApp.cpp:2133-2135`、`OBSApp.cpp:1487-1490` |
| **PORTING.md 自承「`AndroidInputFilter` 约 500 行、11 个 case、承担 5 件事，建议拆成 4 个单责过滤器」** | **已过时（有误）**。`git grep -n "AndroidInputFilter" HEAD -- frontend/` → 0 命中；实际已拆成 `AndroidBackKeyFilter`/`AndroidComboPopupFilter`/`AndroidLongPressFilter`/`AndroidWindowFilter` 四个类 | `OBSApp.cpp:1156/1211/1265/1451`、`OBSApp.cpp:1133-1144` 的"四条单责过滤器"注释、`PORTING.md:755-757` |
| **是否真的"单责"** | **部分符合**。4 个类职责确实分开了，但 `AndroidWindowFilter` 仍同时做 clamp + 挂 `QScroller` 两件事（`OBSApp.cpp:1467-1468`），`clampTopLevelToScreen`/`enableFlickScroll` 也仍是同文件内两个静态函数 | `OBSApp.cpp:1016-1066,1087-1131,1451-1473` |
| **include 块用 `#ifdef __ANDROID__` 包裹** | **明确违反** `CODESTYLE.md:286-287`。仅 39 个 M 文件就有 **17 个 `#ifdef` 块包着 include、共 41 条 include**（全 frontend 含 A 文件为 23 块 / 82 条，与 PORTING.md「约 24 处」量级一致）。`PORTING.md:703-710` 自承违规 | 例：`OBSApp.cpp:51-63`（12 条）、`OBSBasic.cpp:28-36`（7 条）、`OBSBasicControls.cpp:5-16`（6 条）、`OBSBasic_Docks.cpp:24-28`、`OBSBasicFilters.cpp:24-26`、`OBSBasicProperties.cpp:21-23`、`OBSBasicSourceSelect.cpp:20-22`、`OBSLogViewer.cpp:11-13`、`obs-main.cpp:27-29`、`OBSBasicSettings.cpp:31-33`、`OBSBasic_ContextToolbar.cpp:36-38`、`OBSBasic_MainControls.cpp:50-52`、`OBSBasic_Recording.cpp:30-32`、`OBSBasic_SceneItems.cpp:24-26`、`OBSBasic_Transitions.cpp:20-22`、`OBSQTDisplay.cpp:20-22` |
| **`.ui` 尺寸在 `.cpp` 里用 `#ifdef` 覆盖** | **可接受但不理想**。上游自己就在 `OBSBasicSettings::showEvent`（`git show 65c320b:frontend/settings/OBSBasicSettings.cpp:3927-3937`）用 `availableGeometry()` 收高度，说明"运行时按屏幕推导"是上游认可的路子；但移植把它做成 4 个对话框各写一段 `#ifdef`（`OBSBasicSourceSelect.cpp:239-265`、`OBSBasicProperties.cpp:65-91`、`OBSBasicFilters.cpp:68-79`、`OBSBasicSettings.cpp:359-375`），且与全局 clamp 三套机制并存，违反 `PORTING.md:340` 自称的"职责不重叠" | 见「更优实现」 |
| **CMake 层按 `OS_*` 选源文件** | **符合**。`os-android.cmake`、`CrashHandler_Android.cpp`、`platform-android.cpp`、`system-info-android.cpp`、`PluginModuleLoader_Android.cpp` 都是独立编译单元由 CMake 选；`feature-plugin-manager.cmake:10-14` 直接沿用上游 `if/elseif` 链 | `frontend/CMakeLists.txt:101-103`、`feature-plugin-manager.cmake:10-14` |
| **在既有平台链条里扩展 `__ANDROID__`** | **符合**。`components/Multiview.cpp:46` 用 `#elif defined(__ANDROID__)` 接在 `_WIN32`/`__APPLE__` 之后；`OBSApp.cpp:1807` 把 `#if !defined(_WIN32) && !defined(__APPLE__)` 扩成 `&& !defined(__ANDROID__)`；`OBSQTDisplay.cpp:44` 同法；`OBSBasic.cpp:1372-1386` 用 `#else` 保留桌面向导 | 逐处 diff |
| **`#ifndef __ANDROID__` 保留桌面数据表顺序** | **符合**，是本移植最克制的一处。`OBSBasicSettings.cpp:1117-1136` 把非 hybrid 容器档包进 `#ifndef`，桌面下拉顺序"一字不动"（`OBSBasicSettings.cpp:1128` 注释） | `OBSBasicSettings.cpp:1117-1136` |
| **全用编译器宏 `__ANDROID__`，`Q_OS_ANDROID` 出现 0 次** | **可辩护，但非最优**。上游 frontend C++ 里 `Q_OS_*` 只出现 1 次（`Q_OS_UNIX`，`git grep -h -o "Q_OS_[A-Z_]*" 65c320b -- frontend/` 输出 `Count=1, Name=Q_OS_UNIX`），主流是编译器宏 `_WIN32`/`__APPLE__`/`__linux__`，所以 `__ANDROID__` 与之**一致**；但这是 Qt Widgets 代码，`Q_OS_ANDROID` 由 `<QtGlobal>` 提供、语义更明确、且与 Qt 版本解耦。verdict：**符合上游 C++ 惯例，但建议统一改用 `Q_OS_ANDROID` 或在 `OBSApp.hpp`/公共头里定义单一 `OBS_PLATFORM_ANDROID` 宏**，避免 165 处散落 | 命令输出见上；`git grep -n "Q_OS_ANDROID" HEAD -- frontend/` → 0 |
| **C++ 分支必须加大括号** | **违反**。`CODESTYLE.md:199` + `.clang-format:110`。`git diff --unified=0` 新增行启发式统计约 **118 处**缺大括号控制语句；`PORTING.md:769-773` 自承约 82 处，其分项数字与实测吻合（`OBSAndroidDisplay.cpp=18`、`OBSAndroidPermissions.cpp=14`、`OBSAndroidUsb.cpp=12`、`android-window-fit.hpp=9`、`OBSAndroidKeepAlive.cpp=6`、`OBSApp.cpp` 过滤器区=22），但**遗漏**了 `OBSApp.cpp` 其余 9 处及 `OBSBasic.cpp=3`、`OBSBasicControls.cpp=6`、`OBSBasic_ContextToolbar.cpp=4`、`OBSProjector.cpp=3`、`OBSQTDisplay.cpp=5`、`AndroidRuntimeBootstrap.cpp=5` 等 | 例：`OBSApp.cpp:1019/1089/1096/1104/1125`（clamp/flick 区）、`OBSApp.cpp:1163-1463`（过滤器区 22 处，如 `:1304 if (!armed_) return false;`）、`OBSBasicControls.cpp` 的 `for (QWidget *w : order) w->setSizePolicy(...);`（diff `:147-148`）、`OBSBasic_ContextToolbar.cpp` diff `:335` 的 `if (!*out && src && …)` |
| **新常量 `kCamelCase`** | **违反但自承属实**。`HOLD_MS`/`MOVE_SLOP` 仍为 `UPPER_SNAKE` | `OBSApp.cpp:1319-1320`、`CODESTYLE.md:190`、`PORTING.md:718-721` |
| **include 字母序** | **违反且不止 A 文件**。CODESTYLE.md:285 要求块内 case-sensitive 字母序、`:288` 要求非同目录用尖括号。M 文件里也有反例：`OBSApp.cpp:54-55` 把 `QAbstractScrollArea` 放在 `QAbstractItemView` 之前（应为 `ItemView` 先）、`:58-59` 把 `QGestureEvent` 放在 `QElapsedTimer` 之前；`OBSApp.cpp:52-53`、`AndroidMenuPos.hpp:22`、`OBSBasic_ContextToolbar.cpp:37` 用双引号包含非同目录头文件。`PORTING.md:712-716` 只说"18 个新增文件"，**遗漏了被修改的上游文件** | `OBSApp.cpp:51-63`、`CODESTYLE.md:285-288` |
| **`.clang-format` 里 `IndentWidth: 4` / `UseTab: Never` 误读** | **PORTING.md 澄清属实**。那是 `Language: ObjC` 段（`.clang-format:112-212`），C++ 走基础段 `IndentWidth: 8` / `UseTab: ForContinuationAndIndentation`（`:63,:106`）与 `--- Language: Cpp` 段（`:108-110`） | `.clang-format:63,106,108-110,112-212`；`PORTING.md:689-691` |

---

# 问题清单

## P0

**P0-1 `OBSBasicFilters` 的 `showEvent()` 无条件在 QShowEvent 里 resize，既改桌面行为、又违反 PORTING.md 自己的尺寸处理原则**

- 证据：`frontend/dialogs/OBSBasicFilters.hpp:128` 无条件新增 `virtual void showEvent(QShowEvent *event) override;`（基线 `git grep -n showEvent 65c320b -- frontend/dialogs/OBSBasicFilters.*` → 0 命中）；实现在 `frontend/dialogs/OBSBasicFilters.cpp:633-645`：

  ```cpp
  const int titleBarHeight = QApplication::style()->pixelMetric(QStyle::PM_TitleBarHeight);
  const int maxHeight = round(screen()->availableGeometry().height() - titleBarHeight);
  if (size().height() >= maxHeight) { resize(size().width(), maxHeight); }
  ```

  全函数无 `#ifdef`。而同文件 `OBSBasicFilters.cpp:68-79` 的 Android 构造函数块里**已经**调了 `androidFitTopLevelToScreen(this)`（`:78`）。
- 与文档冲突：`PORTING.md:377-380` 明确写"**必须在 `show()` 之前调用**……等 `QShowEvent` 到达时窗口已经按 `.ui` 的设计尺寸画出来了，此时再 resize 用户就会看到『先大后小』闪一下"；`android-window-fit.hpp:16-20` 重复同一判据。`OBSBasicFilters.hpp:127` / `OBSBasicFilters.cpp:639` 的注释却说"桌面大屏上条件不成立、行为不变"——这只是"通常不触发"，不是"不变"。
- 影响：① 桌面（尤其 720p 笔记本 / 高分屏缩放 / 有全局菜单栏的 macOS，正是上游 `OBSBasicSettings.cpp:3993` 那条注释点名的场景）滤镜对话框每次 show 都会被强制 resize，可能与用户从配置恢复的尺寸打架；② Android 上构造函数 fit 用 `avail.height()-16`（`android-window-fit.hpp:53-54`），showEvent 用 `avail.height()-titleBarHeight`，两者公式不同，show 时极可能**再 resize 一次**，正是文档禁止的"先大后小"。
- 缓解因素：这段是照抄上游 `OBSBasicSettings::showEvent`（`git show 65c320b:frontend/settings/OBSBasicSettings.cpp:3927-3937` 逻辑逐字相同），不是凭空发明；所以它是"把上游小屏兜底复制到另一个对话框"，而非新机制。
- 建议：**删除 `OBSBasicFilters::showEvent` 声明与实现**（Android 已由 `:78` 的 `androidFitTopLevelToScreen` 在 show 前定尺寸，桌面由用户/`.ui` 决定）；若确需保留小屏兜底，则把整个 override 包进 `#ifdef __ANDROID__`，并把判据统一到 `android-window-fit.hpp` 的 `availableGeometry()-16`、放在构造函数里完成。同时修正 `PORTING.md`「前端尺寸处理」与「待办」两节，说明这里曾有过一个反模式、为什么删除。

**P0-2 无守卫的"桌面中性"改动缺少统一回归护栏，且与文档声称的隔离策略不一致**

- 证据：`OBSProjector.cpp:340` 把 `popup.exec(QCursor::pos())` 改成 `popup.exec(event->globalPosition().toPoint())`，**未走** `AndroidMenuPos.hpp` 的统一入口；其余同类改动都走了 `OBSMenuPos`（`OBSBasic_Preview.cpp:245/263`、`OBSBasic_Scenes.cpp:636`、`AudioMixer.cpp:874`、`OBSBasicFilters.cpp:763/809/923` 等）。`OBSProjector.cpp:337-339` 注释称"桌面端鼠标按下点 == 光标点，语义不变"，但对合成事件/触摸板/指针 warp 不成立，且造成同一份代码里两种定位策略并存。
- 影响：桌面右键菜单定位在边缘场景改变；同时给"探针日志/菜单定位"这条链路留下无法用 `OBSMenuPos` 统一开关的孤例。
- 建议：改回 `popup.exec(OBSMenuPos(event->globalPosition().toPoint()))`（`OBSMenuPos` 非 Android 恒退回 `QCursor::pos()`），或至少补 `#ifdef __ANDROID__` / `#else` 显式分支。

## P1

**P1-1 `OBSStudioAPI.cpp` 5 个 getter 无守卫地改变前端 API 契约**

- 证据：`OBSStudioAPI.cpp:404/420/430/610/677` 新增 `if (!main->outputHandler) { return nullptr; }`（`git diff` 全文 20 行新增、0 平台守卫）。`obs_frontend_get_streaming_output/recording_output/replay_buffer_output/virtualcam_output/get_last_recording` 在 `outputHandler` 为空时由"崩溃/UB"变为 `nullptr`。
- 影响：桌面插件若依赖"非 null"契约（或把 `nullptr` 当作异常），行为改变；所有平台编译产物改变。属于为 Android 生命周期（`outputHandler` 可能为 null）打的补丁，但没有守卫也没有单独提交上游。
- 建议：这类**与平台无关的空指针加固应作为独立 bugfix 提到上游**（或至少单独一个 commit，便于上游 cherry-pick）；若必须留在移植里，则在 PR 描述里显式列明"改变了所有平台"。不必加 `#ifdef`——加守卫反而会把一个正确修复变成平台分叉。

**P1-2 `OBSBasicSettings_Stream.cpp` / `AutoConfigTestPage.cpp` / `OBSBasicSettings.cpp` 的一批 NULL/未初始化加固被无条件混进移植**

- 证据：`OBSBasicSettings_Stream.cpp:105-107`（`type &&`）、`:1075`（`int vbitrate = 0, abitrate = 0;`）、`diff @@ -1461,12 +1479,16 @@` 与 `@@ -1496,11 +1518,13 @@` 与 `@@ -1665,19 +1689,26 @@`（`output` 判空）；`AutoConfigTestPage.cpp:1038-1040`（`res_count=0; maxFPS=0;`）；`OBSBasicSettings.cpp` diff `@@ -5311,7 +5377,8 @@`、`@@ -5338,7 +5405,12 @@`（`encCodec`/`vCodecCStr` 判空）。`AutoConfigTestPage.cpp:1035-1038` 注释自承修的是 `obs-service.c:372/:389` 的守卫顺序导致读栈垃圾。
- 影响：都是**真实上游缺陷**（`obs_service_valid` 守卫早退不回填出参），桌面同样受益；但混在 Android 移植的 137 文件大 diff 里，上游永远不会单独看到、也无法单独 revert。
- 建议：拆成独立上游 PR / 独立 commit；在移植侧保留但标注"platform-independent bugfix"。同时核查 `libobs/obs-service.c`、`obs-display.c` 的同类无条件改动（见「跨范围观察」）。

**P1-3 `outputHandler->XxxActive()` → `XxxActive()` 的判断（回应 lead 提问）**

- 核实结论与 lead 的表述不同，请以此为准：`OBSBasic::StreamingActive()` / `RecordingActive()` / `ReplayBufferActive()` / `VirtualCamActive()` **不是移植新增**，上游基线已存在且已带空指针检查：
  ```
  git show 65c320b:frontend/widgets/OBSBasic_Streaming.cpp:473   → bool OBSBasic::StreamingActive() { if (!outputHandler) return false; return outputHandler->StreamingActive(); }
  git show 65c320b:frontend/widgets/OBSBasic_Recording.cpp:281 / OBSBasic_ReplayBuffer.cpp:232 / OBSBasic_VirtualCam.cpp:179 → 同构
  ```
  `git diff` 对这 4 个文件**没有**包装函数定义的新增 hunk；移植只把 6 个调用点换成已有的包装函数（`OBSBasic_Streaming.cpp:134/176/393`、`OBSBasic_Recording.cpp:163/267`、`OBSBasic_ReplayBuffer.cpp:33`、`OBSBasic_VirtualCam.cpp:108`），另有 `OBSBasic.cpp:207-208` 的保活 lambda 也用包装函数。
- 判断：**不应加平台守卫**。这是"用上游已有的、更安全的封装替换裸成员访问"，属于纯粹的健壮性改进，桌面正常路径等价；加 `#ifdef` 会把所有平台的正确调用降级成只有 Android 安全，是倒退。**应作为独立 bugfix（或至少独立 commit）**，并顺手把剩下的裸调用点统一（`OBSBasic_ReplayBuffer.cpp:73/127/159/176`、`OBSBasic_VirtualCam.cpp:33/53/118`、`OBSBasic_Streaming.cpp:50`、`OBSBasic_Recording.cpp:123`、`OBSBasic_Hotkeys.cpp:145-243` 仍是 `outputHandler->…`），消除同一文件里两种写法。

**P1-4 `OBSBasic_ContextToolbar.cpp:212` 把 `GetCurrentSceneItem()` 提到可见性闸门之前**

- 证据：`git diff` hunk `@@ -205,13 +209,24 @@`：原来 `if (!ui->contextContainer->isVisible() && !force) return;` 之后才 `OBSSceneItem item = GetCurrentSceneItem();`；现在无条件提到最前（Android 块要用 `item`）。
- 影响：桌面在 context bar 隐藏时，原本早退不取当前场景项，现在每次 `UpdateContextBar()` 都取一次；若 `GetCurrentSceneItem()` 有额外开销/日志/副作用，行为改变。
- 建议：把 `OBSSceneItem item = GetCurrentSceneItem();` 留在原位置，Android 分支内单独取一次（或把 Android 块整体移到 `item` 之后并让桌面路径保持不变）。

## P2

- **P2-1 `CrashHandler.hpp:33` 无条件追加 `Android` 枚举值**。追加在末尾、既有值不变，`CrashHandler.cpp:81-91` 的 switch 有 `default:`（`:89`）故不会触发 `-Wswitch`+`-Werror`；但桌面编译会得到一个"本平台永远不会返回"的枚举值。建议：可接受（与 `CrashHandler_Android.cpp` 配套），但应在头里注明 `Android` 仅由 Android TU 返回；或改成由 `os-android.cmake` 注入平台枚举，避免共享头被改。
- **P2-2 `OBSQTDisplay` 共享显示核心被无条件重构**。`OBSQTDisplay.hpp:29`（`~OBSQTDisplay() override;`）、`:38-48`（`DestroyDisplay()` 去内联、新增 `releaseDisplay()`）、`OBSQTDisplay.cpp:78-82,124-151`。`releaseDisplay()` 非 Android 分支只有 `display = nullptr;`，行为等价，但这是渲染显示这一最敏感核心的 ABI/代码形态变化，且桌面构建也编译 `releaseDisplay()` 里的 `#ifdef` 骨架。建议：把 `releaseDisplay()` 的 Android 部分拆到 `OBSQTDisplay_Android.cpp`（由 CMake 选），桌面保留 `display = nullptr;` 单行；至少在 PR 里单列该重构。
- **P2-3 `OBSBasic_Transitions.cpp:1507` 的 `transitionUuids.empty()` 守卫无条件**。注释自承桌面 no-op（`OBSBasic_Transitions.cpp:1501-1506`），属良性；但同样是"修 UB 却没单独提上游"。建议同 P1-2 处理。
- **P2-4 `OBSApp.cpp:1074-1084` 与 `:1107-1123` 两段注释自相矛盾**。`:1074` 写"用 `TouchGesture`（而不是 `LeftMouseButtonGesture`）"，而实际代码 `:1122` 是 `isItemView ? LeftMouseButtonGesture : TouchGesture`；`:1107-1123` 的第二段注释才与代码、`PORTING.md:417-428` 一致。第一段是回退前的残留，会误导后续维护者把 item view 改回 `TouchGesture`（`PORTING.md:422-424` 明确说那样不生效）。建议删除 `:1074-1084` 那段过时注释。
- **P2-5 `feature-plugin-manager.cmake:11-13` 的注释与实现矛盾（文档/实现不一致）**。注释写"按构建期白名单从 APK 内按裸文件名 dlopen"，但 `PluginModuleLoader_Android.cpp:31-45` 明确说改用 `obs_load_plugins() + os_glob()` 扫目录，且 `PORTING.md:280` 也写"装载器不再用编译期白名单"。建议同步注释。
- **P2-6 `PORTING.md:755-757` 的「AndroidInputFilter 待拆」条目已过时**（见上表）。建议改成"已拆成 4 个过滤器；剩余工作是把它们移出 `OBSApp.cpp` 到独立编译单元"，并把 `AndroidWindowFilter` 的 clamp/flick 再拆开。
- **P2-7 `PORTING.md:769-773` 的缺大括号统计低估**。分项数字准确，但按 `git diff --unified=0` 新增行统计约 118 处（启发式，含少量误报），遗漏了 `OBSApp.cpp` 过滤器区之外的 9 处及 `OBSBasic.cpp`/`OBSBasicControls.cpp`/`OBSBasic_ContextToolbar.cpp`/`OBSProjector.cpp`/`OBSQTDisplay.cpp`/`AndroidRuntimeBootstrap.cpp` 等 M/新增文件中的约 27 处。建议按"只作用于我们改过的文件跑一次 `clang-format -i`"重算，并把 M 文件纳入范围。
- **P2-8 `PORTING.md:712-716` 的 include 未排序只统计"18 个新增文件"，漏了被修改的上游文件**。`OBSApp.cpp:54-55`（`QAbstractScrollArea` 在 `QAbstractItemView` 前）、`:58-59`（`QGestureEvent` 在 `QElapsedTimer` 前）即为反例；`OBSApp.cpp:52-53`、`AndroidMenuPos.hpp:22`、`OBSBasic_ContextToolbar.cpp:37` 还违反 `CODESTYLE.md:288`（非同目录用双引号）。
- **P2-9 `settings/OBSBasicSettings_Stream.cpp:9` 无条件包含 Android 专有头 `widgets/OBSAndroidDisplay.hpp`**。该头无内部 `#ifdef`（`OBSAndroidDisplay.hpp:6-122`），桌面能编译（只有声明 + `OBS_PROBE_LOG` 宏），但把 `obsAndroidDisplayLog` 等符号声明带进每个桌面 TU，且 `OBS_PROBE_LOG` 在桌面一旦被无守卫调用就会链接失败（`OBSBasic_Transitions.cpp:99-104` 的注释正是在防这件事）。建议：把 include 收进 `#ifdef __ANDROID__`，或让该头自守卫（`android-window-fit.hpp:3` 的做法）。
- **P2-10 探针日志 63 处散落**。`PORTING.md:758-759` 自承；`OBSApp.cpp` 的 4 个过滤器里 `OBS_PROBE_LOG` 密集（`:1192`、`:1230-1232`、`:1342-1357`、`:1395-1417` 等）。`OBSAndroidDisplay.hpp:67-80` 已有 `obsAndroidProbeEnabled()` 开关且默认关，但大量常驻 `obsAndroidDisplayLog`（`CLAMP`/`FLICK`/`BACK→Escape`）不走开关。建议按文档做运行时开关分级。
- **P2-11 `OBSBasic.cpp:71` 无条件 `#include <QCoreApplication>`**。桌面不需要，属无守卫的编译产物变化，建议并入 `#ifdef` 块或确认桌面确实需要后保留并说明。

---

# 更优实现方案

按优先级与代价排序：

| 优先级 | 方向 | 具体做法 | 代价 |
|---|---|---|---|
| **P0** | 立刻消除"无守卫改变桌面行为" | 删 `OBSBasicFilters::showEvent`（P0-1）；`OBSProjector.cpp:340` 改走 `OBSMenuPos`（P0-2）；`OBSBasic_ContextToolbar.cpp:212` 把 `item` 移回闸门后（P1-4） | 极小，纯删改 |
| **P0** | 平台无关 bugfix 独立化 | 把 `OBSStudioAPI.cpp`、`OBSBasicSettings_Stream.cpp`、`AutoConfigTestPage.cpp`、`OBSBasicSettings.cpp` 的 NULL/未初始化加固、`*Active()` 包装调用统一，拆成独立 commit 并提上游 PR；移植侧保留但标注 | 小（commit 拆分），收益是上游可 cherry-pick、可单独 revert |
| **P1** | Android 专有逻辑收敛到独立编译单元 | 新建 `frontend/utility/android-platform.{hpp,cpp}`（或 `AndroidPlatform::` 命名空间桥），把 `OBSApp.cpp` 的 4 个过滤器、`clampTopLevelToScreen`、`enableFlickScroll`、`androidSyncPreviewVisibility`、`androidResyncPreviewOnWindowToggle` 全部搬进去；`OBSApp.cpp` 只保留 3 行：include 一个头 + `#ifdef __ANDROID__ AndroidPlatform::install(this); #endif` + `notify()` 里一次 `AndroidPlatform::onEvent(receiver,e)` 调用（同样 `#ifdef`）。CMake 按 `OS_ANDROID` 把该文件加入 target（`frontend/CMakeLists.txt:101-103` 已有 `os-android.cmake` 入口） | 中：一次大搬运（约 460 行），但纯机械；收益是 `OBSApp.cpp` diff 从 629 行降到 ~10 行，且符合上游 `platform-*.cpp` / `CrashHandler_*.cpp` 惯例 |
| **P1** | 少量虚/回调钩子替代散落 `#ifdef` | 对必须留在共享文件的点，定义窄接口而非内联分支：例如 `OBSQTDisplay` 用 `DisplaySurfaceProvider` 抽象（`attach/acquire/claim/release/updateRect`），桌面实现走 `QTToGSWindow`（`OBSQTDisplay.cpp:24-30`），Android 实现走 `OBSAndroidDisplay*`；由 CMake 选实现文件。同理 `openUrl`→`PlatformPaths::showLocalPath()`、`getPlatformType()`→已有 `CrashHandler_*.cpp` 模式、`PluginModuleLoader_*.cpp` 已做对 | 中高：需要动 `OBSQTDisplay` 构造/析构；但能一次性收敛 `PORTING.md:760` 自承的"预览面所有权 5 处各自实现" |
| **P1** | 尺寸适配统一成一条策略 | 现状是三套并存：4 个对话框构造函数里各一段 `#ifdef` + `androidFitTopLevelToScreen`（`PORTING.md:340` 称"职责不重叠"实则重叠）+ 全局 `clampTopLevelToScreen` + `OBSBasicFilters` 的 `showEvent`。建议只保留"show 前 fit"一条策略，把它做成 `OBSPlatformWindowPolicy::fitBeforeShow(QWidget*)`，由各对话框在构造末尾无条件调用一行（该函数在非 Android 是 no-op、内部 `#ifdef` 只在实现文件里出现一次）；删除全局 clamp 的兜底逻辑或降级为纯诊断（保留 `CLAMP-BLOCKED` 日志但不 resize）。同时把 4 个 `.ui` 尺寸下限的放宽集中到一个 `relaxDesktopMinimums(Ui::*)` | 中：能删掉约 6 处 `#ifdef` 与整个 `showEvent` 反模式；`.ui` 的桌面假设仍需在 `.ui`/策略层说明 |
| **P1** | 去掉 17 处 `#ifdef` 包裹的 include 块 | 三步：① 让 `android-window-fit.hpp`（已有 `:3`）、`AndroidMenuPos.hpp`、`OBSAndroidDisplay.hpp`、`OBSAndroidPath.hpp`、`AndroidRuntimeBootstrap.hpp` 全部自守卫（非 Android 展开为空或仅留声明），使能**无条件包含**；② 纯 Qt 头（`QScroller`/`QComboBox`/`QTimer`…）本来就可无条件包含；③ 于是 `OBSApp.cpp:51-63` 等 17 个块可整块删除，include 按 `CODESTYLE.md:244-285` 的分组与字母序重排 | 小-中：改 5 个 A 头 + 17 处 include；直接消除 `CODESTYLE.md:286` 的明确违规 |
| **P2** | 用 Qt 原生 API 减少 hack | 菜单定位统一 `OBSMenuPos`（已做，只差 `OBSProjector.cpp:340`）；`QScroller`/`QStyle::PM_TitleBarHeight`/`QScreen::availableGeometry()`/`sizeHint()` 已用得较好；剩余硬编码可用 `QStyle::PM_ScrollBarExtent`、`QFontMetrics` 行高替代固定 `32`/`48`；`AndroidMenuPos.hpp` 的 `OBS_PROBE_LOG` 可换成 `qCDebug` 分类日志 | 小 |
| **P2** | 统一平台宏 | 定义单一 `OBS_PLATFORM_ANDROID`（或全面改 `Q_OS_ANDROID`），把 165 处 `__ANDROID__` 收敛到一个公共头；便于未来支持 Qt for iOS/嵌入式时替换 | 中（机械替换），但需在 CMake 里同时定义以覆盖 `.c`/CMake |
| **P2** | 风格一次清理 | 对本次改动过的 39 个 M + 相关 A 文件跑 `clang-format -i`（`.clang-format:110 InsertBraces: true` 自动补大括号），再手工处理 `SortIncludes: false` 的字母序（`:84`）与 `HOLD_MS/MOVE_SLOP` → `kHoldMs/kMoveSlop`（`OBSApp.cpp:1319-1320`） | 小；但注意 `PORTING.md:773` 强调"别碰上游文件"——只 format 改过的文件 |

---

# 实现亮点

1. **`#else` / `#ifndef` 正确保留桌面路径**：这是本移植最值得肯定的一点。`OBSBasic.cpp:1372-1386`（首启向导）、`OBSBasicSettings.cpp:1117-1136`（容器下拉顺序）、`OBSBasicSettings.cpp:953-958`（去掉 FFmpeg 档）、`OBSBasic_ContextToolbar.cpp`、`OBSBasic_MainControls.cpp:319-325/472-491`、`OBSLogViewer.cpp:129-135`、`OBSBasic_Recording.cpp:40-46`、`OBSApp.cpp:1984-1993`、`SurfaceEventFilter.hpp:28-42`、`SceneTree.cpp:28-32`、`OBSQTDisplay.cpp:44` 都是 `#ifdef … #else <原上游代码> #endif`，桌面分支逐字保留。
2. **`AndroidMenuPos.hpp:44-47` 的"桌面零影响"设计**：`OBSMenuPos()` 在非 Android 恒 `Q_UNUSED(androidGlobalPos); return QCursor::pos();`，让十几处 `exec(QCursor::pos())` 的替换在桌面结果完全等价；`OBSMenuPosBelow/Center` 也只是纯几何计算。这是"把平台差异收敛到一个可降级的 helper"的正确思路，可惜没推广到 `OBSProjector.cpp:340`。
3. **`android-window-fit.hpp` 自守卫 + 单一实现**：`:3` 用 `#ifdef __ANDROID__` 把整个头包住，非 Android 展开为空，`:35-89` 与 `:96-103` 把"show 前 fit"和"预览最小高"做成两个纯函数，供 4 个对话框和全局 clamp 共用。
4. **CMake 层按平台选源文件完全符合上游**：`frontend/CMakeLists.txt:101-103` 的 `elseif(OS_ANDROID) include(cmake/os-android.cmake)` 直接延续 `65c320b` 的 `if(OS_WINDOWS)/elseif(OS_MACOS)/elseif(OS_LINUX)` 链；`feature-plugin-manager.cmake:10-14` 同法；新增的 `CrashHandler_Android.cpp`、`platform-android.cpp`、`system-info-android.cpp`、`PluginModuleLoader_Android.cpp` 与上游 `CrashHandler_*.cpp` / `platform-*.cpp` / `system-info-*.cpp` 一一对应。
5. **在既有平台条件链里扩展**：`components/Multiview.cpp:46` 的 `#elif defined(__ANDROID__)`、`OBSApp.cpp:1807` 的 `&& !defined(__ANDROID__)`、`OBSQTDisplay.cpp:44` 同法 —— 比另起一个孤立 `#ifdef` 更符合上游读法。
6. **`-Werror` 意识**：`OBSQTDisplay.cpp:20-23,31` 用 `#ifndef __ANDROID__` 排除 `QTToGSWindow` 以避免 `-Wunused-function`；`OBSBasic_Transitions.cpp:99-104` 注释解释为何 `OBS_PROBE_LOG` 必须包 `#ifdef`（`OBSAndroidDisplay.cpp` 只在 Android 挂进 target）；`OBSApp.cpp:2054-2057` 解释删掉 `Qt::Desktop` case 是为避开 Qt 6 deprecated + `-Werror`。
7. **诊断设计**：`OBSApp.cpp:1016-1066` 的 `CLAMP-BLOCKED` 会点名"最占高度的子控件 + `minH`/`sizeHint`"，把"哪个对话框还没做源码级放宽"变成可 grep 的日志；`PORTING.md:401` 给出"不出现 CLAMP 行"的验收判据。
8. **4 个单责过滤器已落地**：`OBSApp.cpp:1156/1211/1265/1451` 的拆分本身是好的（PORTING.md 却仍把它列成待办），`OBSApp.cpp:1133-1144` 还写明了"最后安装的最先收到"的安装顺序理由（`:1487-1490`）。
9. **预览面生命周期**：`OBSQTDisplay.cpp:150-196,201-310` 的 claim / preemptive claim / serial 回读 / Revoked-Granted 门铃，是本次最复杂的部分但注释把实测现象（`surfaceDestroyed` 顺序、`BufferQueue abandoned`、对话框 preview 抢槽导致主预览黑屏）写得很完整，可追溯性强。

---

# PORTING.md 声明核验表

| # | PORTING.md 声明（行号） | verdict | 证据 |
|---|---|---|---|
| 1 | 上游基线 33.0.1 = `65c320b`（`:7-8`） | **属实** | `git rev-parse upstream/master` / `git rev-parse 65c320b` 均 = `65c320b46e1af5c7a555161aea6182cdf7a8c524` |
| 2 | 全量 137 文件 / +21932 / -148 | **属实** | `git diff --numstat upstream/master...HEAD` 汇总输出 `137 files, +21932 / -148` |
| 3 | 「前端尺寸处理」三处职责不重叠、四个对话框构造函数各调一次 `androidFitTopLevelToScreen`（`:337-340,382-383`） | **属实但描述不完整** | 4 处调用：`OBSBasicSourceSelect.cpp:265`、`OBSBasicProperties.cpp:91`、`OBSBasicFilters.cpp:78`、`OBSBasicSettings.cpp:371`；`OBSBasicProperties` 确实放在 `resize(cx,cy)` 之后（`:80-91`）。但 `OBSBasicFilters.cpp:633-645` 又多了第五处 show 后 resize，实际是**四套机制** |
| 4 | 「必须在 `show()` 之前调用……此时再 resize 用户会看到『先大后小』闪一下」（`:377-380`） | **属实（原理正确），但被自身实现违反** | 与 `OBSBasicFilters.cpp:633-645` 的无条件 `showEvent` resize 直接矛盾；`android-window-fit.hpp:16-20` 也重复该判据。见 P0-1 |
| 5 | 添加源 `setMinimumHeight(0)` + `setSizeAdjustPolicy(AdjustIgnored)`「两行缺一不可」（`:353-370`） | **属实** | `OBSBasicSourceSelect.cpp:261-262` 两行都在，注释与 `qSmartMinSize` 推理一致 |
| 6 | 全局兜底 `clampTopLevelToScreen` 在 `QTimer::singleShot(0, …)` 后调用、尺寸合适则早退（`:385-401`） | **属实** | `OBSApp.cpp:1467-1470`（`AndroidWindowFilter`）、`OBSApp.cpp:1016-1019`（`if (!r.resized) return;`）、`:1057-1066`（`CLAMP` / `CLAMP-BLOCKED` 日志） |
| 7 | 「单指滚动」：`QScrollArea` 用 `TouchGesture`、`QAbstractItemView` 用 `LeftMouseButtonGesture`（`:415-428`） | **属实（代码），但代码内注释自相矛盾** | `OBSApp.cpp:1122` 为 `isItemView ? LeftMouseButtonGesture : TouchGesture`，与文档一致；但 `OBSApp.cpp:1074-1084` 的注释写"用 TouchGesture（而不是 LeftMouseButtonGesture）"，是残留。见 P2-4 |
| 8 | 「首页布局」`ResizeAndroidDocks()` 由 `OBSInit` 与 `on_resetDocks_triggered` 共用、比例按内容推导带上界 280（`:432-451`） | **属实** | `OBSBasic_Docks.cpp:24-58`（`qMax(toolbarWidth+8, qMin(width()*28/100, 280))`）、`OBSBasic_Docks.cpp:137`（`on_resetDocks_triggered` 里 `singleShot`）、`OBSBasic.cpp:1308`（`OBSInit` 里 `singleShot(0, this, &OBSBasic::ResizeAndroidDocks)`） |
| 9 | 工具栏按钮尺寸从 `sizeHint()`/`iconSize()` 推导，溢出按钮不动（`:449-452`） | **属实** | `OBSBasic.cpp:1276-1340`（`configureToolbar` 跳过 `act->isSeparator()` 与 `widgetForAction`，QSS 只作用于 `QToolButton[androidTouchAction="true"]`） |
| 10 | 列表行高放在 `SceneTree::SetGridMode()` 与 `SourceTreeDelegate::sizeHint()`（`:454-456`） | **属实** | `SceneTree.cpp:28-32`、`SourceTreeDelegate.cpp:16-20` |
| 11 | 「自适应原则」`androidPreviewMinHeight()` = 可用高/5 夹 `[60,120]`；`defaultProjectorSize()` = 480×270 只在装不下时缩小（`:484-489`） | **属实** | `android-window-fit.hpp:96-103`、`OBSProjector.cpp:25-46` |
| 12 | 容器格式限制：设置页下拉裁表用 `#ifndef __ANDROID__`，桌面顺序不变（`:139-141` 引用、`OBSBasicSettings.cpp:1128` 注释） | **属实** | `OBSBasicSettings.cpp:1117-1136`（两组 `#ifndef`）、`:4960-4972`（Lossless）、`:953-958`（`advOutRecType->removeItem(1)`）、`OBSBasic.cpp:854-897`（profile 纠正） |
| 13 | `Qt::Desktop` 是 Qt 6 起 no-op，`androidSyncPreviewVisibility()` 里那个 case 删掉不改变行为（`:120-124`） | **属实** | `OBSApp.cpp:2054-2057` 注释、`OBSApp.cpp:2058-2067` 的 switch 已无 `Qt::Desktop` |
| 14 | 「代码风格」缩进 tab/8、行宽 120、`InsertBraces: true`、`SortIncludes: false`、ObjC 段不是 C++（`:676-691,697-699,715-716`） | **属实** | `.clang-format:46,63,84,106,108-110,112-212`；`CODESTYLE.md:198-199,285` |
| 15 | 缺大括号约 82 处，分项 18/14/12/9/6/22/1（`:769-773`） | **分项属实，总量低估** | 按 `git diff --unified=0` 新增行启发式统计约 118 处；分项实测吻合（`OBSAndroidDisplay.cpp=18`、`OBSAndroidPermissions.cpp=14`、`OBSAndroidUsb.cpp=12`、`android-window-fit.hpp=9`、`OBSAndroidKeepAlive.cpp=6`、`OBSApp.cpp` 过滤器区=22），但漏计 `OBSApp.cpp` 其余 9 处与 6 个 M/A 文件约 27 处。见 P2-7 |
| 16 | 约 24 处 `#ifdef __ANDROID__` 包着 `#include`（`:703-707,774-776`） | **量级属实，位置不完整** | 全 frontend（含 A）= **23 块 / 82 条 include**；仅 39 个 M 文件 = **17 块 / 41 条**。文档只提"新增文件"，实际重灾区是 `OBSApp.cpp:51-63`、`OBSBasic.cpp:28-36`、`OBSBasicControls.cpp:5-16` 等被修改的上游文件 |
| 17 | include 未排序 18 个新增文件（`:712-716,777-778`） | **存疑/不完整** | 除 A 文件外，M 文件也有反例：`OBSApp.cpp:54-55`、`OBSApp.cpp:58-59`；且 `OBSApp.cpp:52-53`、`AndroidMenuPos.hpp:22`、`OBSBasic_ContextToolbar.cpp:37` 违反 `CODESTYLE.md:288`（非同目录用双引号）。见 P2-8 |
| 18 | `HOLD_MS`/`MOVE_SLOP` 应为 `kHoldMs`/`kMoveSlop`（`:718-721,779-780`） | **属实** | `OBSApp.cpp:1319-1320` 仍为 `UPPER_SNAKE`；`CODESTYLE.md:190` |
| 19 | 「待办」`AndroidInputFilter`（约 500 行、11 个 case、5 件事）建议拆成 4 个单责过滤器（`:755-757`） | **有误（已过时）** | `git grep -n "AndroidInputFilter" HEAD -- frontend/` → 0 命中；`OBSApp.cpp:1156/1211/1265/1451` 已是 4 个类。见 P2-6 |
| 20 | 探针日志 63 处、建议运行时开关（`:758-759`） | **存疑（未逐一计数）** | `OBSAndroidDisplay.hpp:67-80` 已有 `obsAndroidProbeEnabled()` 开关且默认关，说明部分已做；但 `OBSApp.cpp` 的 `CLAMP`/`FLICK`/`BACK→Escape` 走 `obsAndroidDisplayLog` 常驻。文档"约 20 处长期价值 / 21 处每事件残留"未给统计口径 |
| 21 | 「容器格式限制 5 处各自编码、预览面所有权 5 处各自实现」（`:760`） | **属实（定性）** | 容器限制见 #12 共 5+ 处；预览面所有权散在 `OBSQTDisplay.cpp:150-196,201-310`、`SurfaceEventFilter.hpp:28-42`、`OBSApp.cpp:2052-2097`、`OBSProjector.cpp:161-171`、`OBSBasic_Projectors.cpp:144-150` |
| 22 | 「剩余 `CLAMP-BLOCKED` 对话框：高级音频/变换/关于/更新」（`:741-743`） | **属实（待办，非错误）** | `OBSApp.cpp:1057-1066` 的日志点名机制存在；属于未修项 |
| 23 | 「嵌套子菜单坐标错位」「浮动 dock 崩溃」（`:734-740`） | **属实（待办，非错误）** | `OBSBasicControls.cpp:59-77` 的 ☰ 二级菜单用 `singleShot(0)` + `sub->exec(at)` 缓解；`AndroidMenuPos.hpp:1-9` 自承只覆盖部分场景 |

---

# 跨范围观察

以下不属于本范围（A 文件 / `libobs` / CI / 文档），但审查中撞到，交给 lead 与其他审查员：

1. **`libobs/obs-service.c`、`obs-display.c` 的无条件改动**：lead 已列为无守卫改动。`AutoConfigTestPage.cpp:1035-1038` 与 `OBSBasicSettings_Stream.cpp:1070-1074` 的注释都指向 `obs-service.c:372/:389` 的 `obs_service_valid` 守卫"!早退不回填出参"，前端因此必须初始化 `res_count/maxFPS`。**建议上游修法是在 libobs 里把"清零出参"移到守卫之前**（`obs_service_get_max_bitrate` 已是这样），这样前端 4 处初始化都不必要。请 libobs 审查员核实这两处改动是否也无条件、是否改了桌面行为。
2. **`frontend/cmake/feature-plugin-manager.cmake:11-13` 的注释与 `PluginModuleLoader_Android.cpp:31-45` 矛盾**（见 P2-5），且 `PORTING.md:280` 与注释二选一。属 A 文件范围，请 `frontend-android` 一并修。
3. **`PORTING.md:755-757` / `:769-780` 两处待办与现状脱节**（AndroidInputFilter 已拆、缺括号统计低估、include 统计漏 M 文件），建议 lead 汇总时统一修订文档。
4. **`.ui` 文件未被修改**：39 个 M 文件里没有 `.ui`（`git diff --name-status upstream/master...HEAD -- frontend/` 输出无 `.ui`）。所有桌面尺寸假设都在 `.cpp` 里用 `#ifdef` 覆盖，导致"`.ui` 仍是桌面值、运行时靠代码放宽"的双份真相。请 `frontend-android`/UI 审查员评估是否该由平台无关的 `QSizePolicy`/布局策略或 `.ui` 层解决。
5. **全树 `__ANDROID__` 165 处 / 147 条条件编译指令、`Q_OS_ANDROID` 0 次**：跨 libobs / plugins / frontend 是否统一宏口径，建议 lead 定一个全局结论；单看 frontend C++，与上游 `_WIN32`/`__APPLE__` 惯例一致，可接受。
6. **`PORTING.md:703-710` 声称"被包含的头文件自己带守卫，无条件包含是安全的"**：核实结果是**部分成立** —— `android-window-fit.hpp:3` 有守卫，但 `OBSAndroidDisplay.hpp`（`:6` 起直接 `#pragma once`）、`AndroidRuntimeBootstrap.hpp`、`OBSAndroidPath.hpp` 没有整体守卫；`settings/OBSBasicSettings_Stream.cpp:9` 已无条件包含 `OBSAndroidDisplay.hpp`（见 P2-9）。这条"多数可去掉"的整改方案需要先给这 4 个头补自守卫，否则照文档改会引入桌面链接/编译问题。
7. **`OBSApp.cpp:1133-1144` 与 `PORTING.md:755-757` 关于过滤器数量的口径**：代码注释说"四条单责过滤器"，`PORTING.md` 说"5 件事 / 建议拆 4 个"——实际是 4 个类承担 5 件事（`AndroidWindowFilter` 做 clamp+flick 两件）。汇总时以代码为准。
