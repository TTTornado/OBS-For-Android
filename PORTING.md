# OBS Studio for Android — 移植说明

[![Android compile check](https://github.com/TTTornado/obs-studio-android/actions/workflows/android-compile.yml/badge.svg)](https://github.com/TTTornado/obs-studio-android/actions/workflows/android-compile.yml)

本仓库是 [obsproject/obs-studio](https://github.com/obsproject/obs-studio) 的一个 fork，
在根级布局上叠加 Android 移植（原移植工作由 [cunminF](https://github.com/cunminF/OBS_for_Android) 完成）。
上游基线是 **33.0.0**（`72e8665`），移植改动叠在它之上，用
`git log --oneline 33.0.0..HEAD` 看全量。

---

## 版本矩阵

| 组件 | 版本 | 说明 |
|---|---|---|
| OBS 上游基线 | **33.0.0** | `cmake/common/versionconfig.cmake` 需要 |
| Android NDK | **30.0.16138531** | sdkmanager 可装；runner 自带的 `30.0.16248370` 不是同一个 |
| FFmpeg | **8.0.3** | OBS 33.0 要求 ≥ 8.0 |
| Android SDK | platform **android-36**（compileSdk） | build-tools 用 runner 自带的最新版（未钉版本） |
| targetSdk / minSdk | **35** / **29** | 见下方「Android 平台要求」 |
| 目标 ABI | arm64-v8a | 只做真机；模拟器需要 x86_64 构建，不支持 |
| Qt | **6.11.1**（Android + 桌面） | 与上游一致，见下方「Qt 版本」 |

---

## CI 覆盖什么、不覆盖什么

`.github/workflows/android-compile.yml` 两个 job：

**`compile`（依赖 + 编译验证，不带 Qt）**

- 15 个第三方依赖的交叉编译（`android/deps-android/`），结果存 `actions/cache`
- libobs / libobs-opengl(GLES) / 全部 `android-*` 采集插件的编译与链接
- 用 `-DENABLE_FRONTEND=OFF` 单独验证「原生层不依赖 Qt」这条声明
- 产出 `android-native-libs` artifact（`.so` 清单 + `libobs.so` 的 `main` 符号）

**`apk`（Qt 前端 + 打包）**

- 安装 Qt 6.11.1（Android + 桌面宿主），结果存 cache（key 含版本号）
- 编译 Qt 前端 → `androiddeployqt` 打包 → debug 签名
- 产出 `OBS-Android-apk` artifact

**不覆盖**：任何运行时行为。相机、UVC、MediaProjection、AAudio、推流在 CI 里都验证不了。

依赖缓存命中时，`fetch.sh` 与 `build.sh` 两个步骤整体跳过（`if: cache-hit != 'true'`）。

---

## 本地构建

```bash
# 1) 依赖（产物在 android/deps-android/prebuilt/<abi>/）
bash android/deps-android/fetch.sh
bash android/deps-android/build.sh arm64-v8a

# 2) 编译验证（不需要 Qt）
NDK=$ANDROID_NDK_ROOT bash android/compile-check.sh

# 3) 带前端（需要 Qt for Android + 桌面 Qt）
ENABLE_FRONTEND=ON QT_DIR=~/Qt/6.11.1/android_arm64_v8a \
  QT_HOST_PATH=~/Qt/6.11.1/gcc_64 \
  NDK=$ANDROID_NDK_ROOT bash android/compile-check.sh

# 4) 出可安装的 APK（构建 + androiddeployqt + apksigner）
QT_DIR=~/Qt/6.11.1/android_arm64_v8a \
  QT_HOST_PATH=~/Qt/6.11.1/gcc_64 \
  NDK=$ANDROID_NDK_ROOT ANDROID_SDK_ROOT=$ANDROID_SDK_ROOT \
  bash android/build-apk.sh
# 产物：OBS-Android-arm64-v8a-<mmdd-HHMM>.apk
```

`fetch.sh` 的代理是**可选**的：本机开发设 `OBS_PROXY=http://127.0.0.1:7892`，CI 直连留空。

---

## 改代码前必读

### 1. `.gitignore` 的 `/*` 会静默吞掉新文件

`.gitignore` 第 2 行是 `/*` —— 忽略根下一切，再用逐条 `!` 放行。

**后果**：任何新增的顶层目录/文件，如果没在 `.gitignore` 里加 `!` 放行，`git add` 时会**无声消失**。
`android/` 和 `PORTING.md` 都是靠显式放行才进来的。

排查：

```bash
git check-ignore -v <path>   # 打印是哪条规则命中的
```

### 2. CI 里必须显式给 `OBS_VERSION_OVERRIDE`

`cmake/common/versionconfig.cmake:9` 的条件是：

```cmake
if(NOT DEFINED OBS_VERSION_OVERRIDE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
```

满足时用 `git describe --always --tags` 推版本。而 `actions/checkout` 默认 `fetch-depth: 1`
且不带 tag，`describe` 只能返回裸 SHA，正则匹配不到 `x.y.z`，于是 `list(GET ...)` 越界、
`project()` 收到非法 VERSION 直接失败。

**修法**：定义 `OBS_VERSION_OVERRIDE`，整段 git 逻辑被跳过。升级基线时要同步改这个值。

### 3. Android 分支提前 `return()` 会跳过上游新增的初始化

`plugins/CMakeLists.txt` 的 Android 白名单在 `:158` 就 `return()`，而上游在文件末尾（`:257`）
才调用 `set_obs_core_modules()`。33.0 新增该机制后，Android 路径没生成 `obs-core-modules.c`：

```
CMake Error at libobs/cmake/obs-core-modules.cmake:4 (target_sources):
  Cannot find source file: obs-core-modules.c
```

**修法**：在 `return()` 之前补 `set_obs_core_modules()`。

**通用检查项**：每次 rebase 上游后，检查移植代码里的提前 `return()` / `exit` 是否跳过了
上游新增的初始化步骤。

### 4. 编译开了 `-Werror`，Qt 的 deprecation 会直接变错误

处理原则：**逐个修掉死代码**，不要加 `-Wno-error=deprecated-declarations` ——
那会把后续所有弃用一起吞掉。例如 `Qt::Desktop` 是 Qt 6 起就 no-op 的窗口类型，
`androidSyncPreviewVisibility()` 里那个 `case` 永远匹配不到，删掉即可。

---

## Qt 版本与套件的获取

### 用哪个版本：**6.11.1**

上游 OBS 不钉 Qt 版本号，钉的是 obs-deps 的发布批次（`CMakePresets.json` 的
`"qt6": { "version": "2026-08-26" }`）。真正的 Qt 版本在 obs-deps 仓库的
`deps.qt/qt6.zsh` 里，该批次对应 **Qt 6.11.1**。

上游代码的最低要求是 `>= 6.9.0`（`os-linux.cmake:15`、`os-freebsd.cmake:12`），
但上游 CI 只测 6.11.1 —— 不要用更低版本，那是官方不覆盖的组合。

### 装在哪：6.8+ 的 Android 包在 `all_os` 分类

| 位置 | 状态 |
|---|---|
| `online/qtsdkrepository/linux_x64/android/` | ❌ 冻结在 **6.7.3**（2024-09-24） |
| `online/qtsdkrepository/all_os/android/` | ✅ 持续更新（6.8 → 6.12） |
| `online/qtsdkrepository/linux_x64/desktop/` | ✅ 仍在更新，桌面套件照旧 |

所以这两条命令的结果完全不同：

```bash
aqt list-qt linux android      → 5.9.0 … 6.7.3    # 看不到 6.8+
aqt list-qt all_os android     → … 6.11.1 … 6.12   # 真正的位置
```

装法：

```bash
pip install aqtinstall
aqt install-qt all_os android 6.11.1 android_arm64_v8a   # Android 套件
aqt install-qt linux desktop 6.11.1 linux_gcc_64         # 桌面宿主（提供 androiddeployqt）
```

aqt 原生支持 `all_os` + `android`（`aqt/metadata.py` 里
`TARGETS_FOR_HOST["all_os"] = ["wasm", "qt", "android"]`），目录名拼成
`qt6_6111/qt6_6111_arm64_v8a`，与服务器实际结构一致。

> 升级 Qt 是横切变更：edge-to-edge 输入映射、`QScroller` 在 item view 上的行为、
> `qSmartMinSize` / `sizeAdjustPolicy`、工具栏 `sizeHint` 推导、AGP/Gradle 版本
> 都可能漂移。必须走一遍真机回归，不要只等 CI 绿。

---

## Android 平台要求

### compileSdk 36 + targetSdk 35

两个值含义不同，可以不同：

- **compileSdk 决定「能用到哪些 API」** —— 必须是 **36**。Qt 6.11.1 的 Gradle 模板依赖
  `androidx.core:core:1.17.0`，它要求 compileSdk ≥ 36；停在 35 会在
  `:checkReleaseAarMetadata` 失败：
  ```
  Dependency 'androidx.core:core:1.17.0' requires libraries and applications
  that depend on it to compile against version 36 or later of the Android APIs.
  ```
- **targetSdk 决定「选择哪些运行时行为」** —— 保持 **35**（已在真机验证）。

### targetSdk 35 要求 Qt ≥ 6.9

Android 15 对 targetSdk ≥ 35 的应用强制 edge-to-edge（窗口属性出现 `EDGE_TO_EDGE_ENFORCED`）：
应用画到状态栏下面、内容从 y=0 开始。**Qt 6.9 起支持 edge-to-edge**，输入坐标会正确换算。

Qt 6.7.x 不支持，会仍按「内容在状态栏下方」换算，导致所有触摸被整体加上一个状态栏高度
（本机实测 111 px），症状是「界面显示正常、但大部分控件点不中」，只有预览区因为面积大
还能拖动。

> 不要用 `android:windowOptOutEdgeToEdgeEnforcement="true"` 绕过：它只是 Android 15 的
> 过渡开关，Android 16 起失效。

---

## APK 打包

### 平台必须显式指定

`build-apk.sh` 给 `androiddeployqt` 传 `--android-platform android-36`
（`ANDROID_PLATFORM` 变量，可用环境变量覆盖）。

解析链在 `androiddeployqt/main.cpp:1050-1068`：`--android-platform` >
settings 的 `android-compile-sdk-version` > 扫 `platforms/` 目录。
命令行参数排在最前，所以 runner 上装了什么平台、Qt 哪个版本、键名有没有变，都不影响结果。

**不要**靠卸载 SDK 里的其它平台来逼它选对：那个探测是反向字典序取第一个，
会挑到 `android-36-ext19` / `android-34-ext12` 这类 `*-ext*` 变体，
compileSdk 会变成 codename 或与 targetSdk 错位。

### 不要删 `android-build` 目录再重打包

Qt 的 CMake 已经把 `androiddeployqt` 挂成构建目标（`[450/451] Creating APK for obs-studio`），
但那一刻插件 `.so` 还没暂存，所以那次出的包**不含插件**。需要重打包一次带上插件；
而重打包时 `android-build/libs/<abi>/libobs_arm64_v8a.so` 必须还在，删目录会报
`Cannot find application binary`。

正确做法：保留目录、只换插件并清 Gradle 的 native 中间件（见 `android/build-apk.sh`）。

---

## 前端尺寸处理

手机横屏的逻辑尺寸只有 **873×393**（本机），而 OBS 的对话框是按桌面设计的。
所有尺寸处理集中在三处，职责不重叠：

### 1. 源码级放宽：去掉 `.ui` 里的桌面尺寸假设

每个对话框在自己的构造函数里，用 `#ifdef __ANDROID__` 放开写死的下限：

| 对话框 | 写死的值 | 处理 |
|---|---|---|
| 添加源 | `sourceTypeList` 的 `minimumSize` 高 460 | `setMinimumHeight(0)` + `setSizeAdjustPolicy(AdjustIgnored)` |
| 设置 | 对话框 `minimumSize` 700×512 | `setMinimumSize(0, 0)` |
| 源属性 | `windowSplitter` 的 `minimumSize` 高 400 | `setMinimumHeight(0)` |
| 滤镜 | `rightLayout` 的 `minimumSize` 高 400 | `setMinimumHeight(0)` |

**添加源那两行缺一不可**，它们堵的是两条不同的路。`QLayout` 算最小尺寸走
`qSmartMinSize()`（`qlayoutengine.cpp:321-333`）：

```cpp
s = max(sizeHint, minimumSizeHint);      // sizePolicy 无 ShrinkFlag 时
if (minSize.height() > 0) s = minSize;   // 非零则无条件覆盖
```

| 只做哪一行 | 460 从哪条路回来 | 布局最小高 |
|---|---|---|
| `setMinimumHeight(0)` | `sizeHint()`（`.ui` 把策略设成 `AdjustToContents`，跟随内容） | 460 ❌ |
| `setSizeAdjustPolicy(AdjustIgnored)` | `minSize`（`.ui` 的 `0x460`，非零时无条件覆盖） | 460 ❌ |
| **两行都有** | — | **~192** ✅ |

`AdjustIgnored` **不是**「Qt 默认值所以可删」：`OBSBasicSourceSelect.ui:160-162` 显式把策略
设成了 `AdjustToContents`。两种策略下 `sizeHint()` 行为完全不同
（`qabstractscrollarea.cpp:1426-1442`）：`AdjustIgnored` → 固定 `256×192`；
`AdjustToContents` → `frame + scrollbars + viewportSizeHint()`。

### 2. 显示前定尺寸：消除「先大后小」

`frontend/utility/android-window-fit.hpp` 提供 `androidFitTopLevelToScreen(QWidget *)`：
把窗口缩进 `screen()->availableGeometry()` 并居中。

**必须在 `show()` 之前调用**（对话框构造函数里）。Qt 的 `show()` 顺序是
`ensurePolished()` → `show_sys()`（创建并映射原生窗口）→ 发送 `QShowEvent`；
等 `QShowEvent` 到达时窗口已经按 `.ui` 的设计尺寸画出来了，此时再 resize 用户就会看到
「先大后小」闪一下。

四个对话框的构造函数里各调一次。**`OBSBasicProperties` 那处必须放在
`resize(cx, cy)` 之后**，否则会把从配置恢复的尺寸覆盖回 `.ui` 的设计值。

### 3. 全局兜底：`clampTopLevelToScreen`

`OBSApp.cpp` 里，顶层窗口 `Show` 之后延后一拍（`QTimer::singleShot(0, ...)`，等布局跑完）
调用同一个 helper。

- 尺寸已经合适 → 直接早退，**不产生任何可见动作**，也不打日志
- 只有还没做源码级修复的窗口才会被动，并打一条 `CLAMP-BLOCKED`：

```
CLAMP-BLOCKED OBSBasicProperties：目标 720x377 实际 720x456；
    最占高度=QSplitter/windowSplitter minH=400 sizeHint=430 —— 该对话框需要在源码里放宽最小尺寸
```

**每一条 `CLAMP-BLOCKED` 就是一个待修项**：它点名了最占高度的子控件，照着去那个
对话框的构造函数里放宽即可。

**验收判据**：打开对话框时日志里**不出现 `CLAMP` 行**。

> 不要用 `setMinimumSize(0,0)` + `layout()->setSizeConstraint(SetNoConstraint)` 硬压窗口。
> 那会造出「窗口比内容小」的不一致状态：窗口压到 377，而布局仍按 478 摆放子控件，
> 列表被摆成 460 高、裁在 377 的窗口里，viewport 比可视区大 ⇒ 滚动条范围算错，
> 列表既不能滚、命中判定也错位。

---

## 单指滚动

Qt 在 Android 上把单指触摸合成成鼠标事件，而 `QAbstractScrollArea` 只在 `wheelEvent` 里滚动
—— 默认只有**双指拖**（Qt 合成成 `QWheelEvent`）能滚。

`enableFlickScroll()` 在顶层窗口 Show 后给里面的滚动区挂 `QScroller`，按控件类型分开：

| 控件类型 | 手势模式 | 效果 |
|---|---|---|
| `QScrollArea`（设置页） | `TouchGesture` | 正常 |
| `QAbstractItemView`（QListWidget / QTreeView） | `LeftMouseButtonGesture` | 能滚，但**按下那一项会被选中一次** |

`QAbstractItemView` 上用 `TouchGesture` **不生效**：它自己处理触摸，识别器抢不到，
事件退回成「按住左键拖选」。添加源列表因此不滚，而且 `currentItemChanged` 接到了
`sourceTypeSelected`，拖选每改一次当前项右边面板就刷一次，看起来像「依次点开所有项」。

改用 `LeftMouseButtonGesture` 后 QScroller 抓走鼠标并消费移动事件，拖动变滚动。
**已知代价**：按下位置那一项仍会被选中一次（按下先到控件，QScroller 要超过阈值才接管）。
在设置对话框左栏拖动也会切一次页 —— 对导航列表可以接受。

---

## 首页布局

dock 比例由 `OBSBasic::ResizeAndroidDocks()`（`OBSBasic_Docks.cpp`）负责，
`OBSInit` 和 `on_resetDocks_triggered` 共用 —— 否则「重置界面」之后不会再套用。

比例按内容推导，不拍数字：

```cpp
const int toolbarWidth = qMax(ui->scenesToolbar->sizeHint().width(), ...);
const int side   = qMax(toolbarWidth + 8, qMin(width() * 28 / 100, 280));
const int bottom = qMax(mixer / transitions / controls 的 minimumSizeHint().height());
```

- `side` 的下界保证工具栏 5 个按钮放得下，上界 280 防止在宽屏上无限膨胀
  （没有上界时 1280 宽会是 358、1600 宽会是 448，把预览挤掉）
- 手写 `qMax`/`qMin` 而不用 `qBound`：下界可能大于上界，那种情况应该保工具栏完整

工具栏按钮尺寸从 `ui->transitionAdd->sizeHint()` / `iconSize()` 推导，再用 QSS 精确控制
（`border`/`padding` 归零，让 `min`/`max` 就是外尺寸），`QToolBar` 一起撑到
`buttonSize.height()+2` 覆盖主题给的 32px 上限。溢出按钮（`">>"`）不动：给它加尺寸会让
Qt 在算溢出时多留宽度，把后面的动作全收进菜单。

列表行高：场景列表的 `min-height` 在 `SceneTree::SetGridMode()` 里设（放 `OBSInit` 里会被
切网格模式覆盖）；源列表在 `SourceTreeDelegate::sizeHint()` 里夹（样式表盖不过 delegate
返回的行高）。

真机日志：

```
Android toolbar: scenes=5 sources=5 button=26x28 rows=34/32
Android layout: side=244 bottom=135 preview=648x162
```

---

## 自适应原则

判断某个尺寸该不该写死时按这张表：

| 类型 | 做法 |
|---|---|
| 窗口 / 对话框尺寸 | 从 `screen()->availableGeometry()` 取 |
| 比例分配 | 用 `%`，但**必须有上下限** |
| 下限 | 从 `sizeHint()` / `minimumSizeHint()` 推导，别拍数字 |
| 触摸目标 / 行高 | 固定 dp，**不要**按比例 |
| 间距（margin / gap） | 小的固定值（8px）可以接受 |

Qt 在 Android 上的逻辑像素 = dp，所以固定的 32（行高）、48（触摸目标）在任何设备上
物理尺寸都一致 —— 这些是设计值，改成比例反而错。

已经按这套改过的地方：

- `androidFitTopLevelToScreen` / `clampTopLevelToScreen` —— 全部从 `availableGeometry()` 取
- `ResizeAndroidDocks()` —— `width()` + `sizeHint()` + `minimumSizeHint()`，带上界 280
- 工具栏按钮 —— `transitionAdd->sizeHint()`
- 内嵌预览最小高 —— `androidPreviewMinHeight()`：可用高 / 5，夹在 `[60, 120]`
- 投影仪默认尺寸 —— `defaultProjectorSize()`：`480×270` 是 dp 设计值，**只在装不下时等比缩小**
  （按屏宽比例算的话，1280 宽平板上会变成 704×396，一个「小窗口」占半个屏）

---

## 待办

### 已知缺陷

- [ ] **浮动 dock 崩溃**：有源在渲染时浮动 dock → 新原生窗口销毁 Surface →
      `makeCurrent(): no EGLSurface` → `QRhiGles2` 失败 → SIGSEGV
      （`reason=5 (APP CRASH(NATIVE))` status=11）。倾向方案：Android 上直接去掉
      `DockWidgetFloatable`，一并解决「浮动后找不回」
- [ ] **嵌套子菜单坐标错位**：`视图 → 打开多视图 → 显示器 1` 点不动。Qt 报的子菜单几何
      （`geo=692x334+163+59`）与实际绘制位置不符，点击落到父菜单。影响所有二级菜单
      （场景列表模式 / 统计 等）；`frontend/utility/AndroidMenuPos.hpp` 只覆盖了部分场景
- [ ] **剩余 `CLAMP-BLOCKED` 对话框**：高级音频 1100×340、变换 900×280、关于 840×519、
      更新 611×526 … 逐个做源码级放宽。做法：跑一遍主界面打开各个对话框，
      日志里每一条 `CLAMP-BLOCKED` 就是一个待修项
- [ ] `ObsForegroundService.java` 缺 `microphone` 前台类型（Android 14+ 后台录音静默断）
- [ ] `AndroidManifest.xml:93` `debuggable="true"` + debug keystore 签 release

### 未做真机功能验证

- [ ] 录制 / 推流 / 屏幕采集 / 系统内录 / 相机方向（日志里 `渲染期旋转=N°`）

### 工程债

- [ ] `AndroidInputFilter`（`OBSApp.cpp`，约 500 行含 170 行注释、11 个 case）承担 5 件
      互不相关的事：长按=右键、BACK=Escape、组合框弹层补丁(P-8)、clamp、QScroller 挂载。
      建议拆成 4 个单责过滤器
- [ ] 探针日志 63 处：约 20 处有长期价值、约 21 处是每事件残留。建议加
      `OBS_ANDROID_PROBE` 运行时开关（默认关），长期项降到 `LOG_DEBUG`
- [ ] 提取 `plugins/android-common/`：uvc 与 nikon 有 6 组逐字重复（USB 枚举、采集线程
      三件套、JPEG→BGRA 整段…）。**复制已经造成过缺陷** —— nikon 漏了 uvc 已修好的 NULL 检查
- [ ] 容器格式限制 5 处各自编码、预览面所有权 5 处各自实现 —— 收敛成单一入口
- [ ] CI：缓存 key 不含 `NDK_VERSION`/`API`；`always()` 保存半成品 prebuilt 可能毒化缓存；
      paths 漏了 `deps/**`；`.gitignore` 对 `android/deps-android/{prebuilt,src}` 无规则
- [ ] 构建可重现性：x264/rnnoise 钉的是 `master` 分支且无 checksum；每次构建新生成 keystore；
      `apksigner`/`aapt2` 用 `sort -V | tail -1` 取最新
