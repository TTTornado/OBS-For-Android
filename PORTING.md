# OBS Studio for Android — 移植说明

[![Android compile check](https://github.com/TTTornado/obs-studio-android/actions/workflows/android-compile.yml/badge.svg)](https://github.com/TTTornado/obs-studio-android/actions/workflows/android-compile.yml)

本仓库是 [obsproject/obs-studio](https://github.com/obsproject/obs-studio) 的一个 fork，
在根级布局上叠加 Android 移植（原移植工作由 [cunminF](https://github.com/cunminF/OBS_for_Android) 完成）。
上游基线是 **33.0.1**（`65c320b`），移植改动叠在它之上，用
`git log --oneline 33.0.1..HEAD` 看全量。

---

## 版本矩阵

| 组件 | 版本 | 说明 |
|---|---|---|
| OBS 上游基线 | **33.0.1** | `cmake/common/versionconfig.cmake` 需要 |
| Android NDK | **30.0.16248370** | ubuntu-24.04 runner 自带（`ANDROID_NDK_LATEST_HOME`），无需安装 |
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
`"qt6": { "version": "2026-10-23" }`）。真正的 Qt 版本在 obs-deps 仓库的
`deps.qt/qt6.zsh` 里，该批次对应 **Qt 6.11.1**。升级上游基线时这里要跟着看 ——
批次变了 Qt 版本可能跟着变。

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

### APK 是**打两遍**包出来的，不是「解包再压回去」

```
① compile-check.sh（ENABLE_FRONTEND=ON）
      CMake 构建结束时，Qt 的 CMake 自动调 androiddeployqt
      （日志里的 [450/451] Creating APK for obs-studio）
      ⇒ 这一遍的包【不含我们的插件】
② 插件暂存： rundir/lib/obs-plugins/*.so → frontend/cmake/android/assets/obs-plugins/
             （同时生成 MANIFEST 清单）
③ 第二遍： 删掉第一遍的 APK 和 Gradle 产物，再跑一次 androiddeployqt，让 assets 进包
④ apksigner 签名
```

**为什么第一遍不能带插件**：插件是 CMake 构建**过程中**才产出的，而 androiddeployqt 目标在
构建**末尾**才跑 —— 那一刻 assets 目录还是空的。所以需要第二遍。

**第二遍是真的重新打包**：删掉 `build/outputs/apk` 和 Gradle 的 native 中间件，逼 Gradle
重新执行 `packageRelease`。其余 Gradle 任务仍走缓存，所以第二遍不是全量。

### 两个不能碰的点

**① assets 目录必须"先清空再拷"**

```bash
rm -rf "$PKG_ASSETS" && mkdir -p "$PKG_ASSETS"
```

只做 `cp -f` 的话，**删掉的插件会一直留在包里**、旧 `.so` 覆盖新构建的 —— 这种「改了没生效」最难查。

**② 绝不能删 `android-build` 目录**

`androiddeployqt` 重跑时需要 `android-build/libs/<abi>/libobs_arm64_v8a.so`
（CMake 拷进去的应用库），目录一删就报 `Cannot find application binary`。

---

## 插件分发与加载

### 为什么插件不进 APK 的 `lib/<abi>/`

非 debuggable 包安装时，系统**只把 `lib*.so` 解到 `nativeLibraryDir`**。我们的插件叫
`obs-x264.so` / `android-camera.so`（OBS 插件惯例：名字不带 `lib` 前缀），一个都解不出来
—— 实测 34 个 `.so` 只解出 20 个，正好是那 20 个 `lib*` 的。这就是之前非 debuggable 包
「全部内置插件 `dlopen not found`」的根因；debuggable 包会全量解出，所以这个问题一直被掩盖。

> 曾怀疑是 Android 10+ 的 W^X 拦住了「从私有目录 dlopen」。**实测否证**：那条限制针对
> `execve`（执行程序），不拦 `dlopen`；从私有目录 dlopen 成功，`os_glob` 也能正常扫。

### 现在的链路

```
APK: assets/obs-plugins/*.so + MANIFEST      （保留原文件名，APK 照常压缩）
  ↓ 装载前（QApplication 之后）EnsureAndroidPluginBinaries()
$OBS_ROOT_PATH/lib/*.so                      （解压到应用私有目录）
  ↓ obs_load_plugins() → os_glob()
与 Linux/BSD 完全相同的「扫目录」装载
```

两个关键点：

* **解压必须晚于 QApplication**：Qt 的 `assets:` 文件引擎是平台插件初始化时注册的，而
  `PrepareAndroidRuntimePaths()` 跑在 `main()` 最前面（QApplication 之前），那时 `assets:`
  不可用。所以由 loader 在装载前调 `EnsureAndroidPluginBinaries()`（幂等，只成功才置位）。
* **装载器不再用编译期白名单**：就是 `obs_load_plugins()` + `os_glob($ROOT/lib/*.so)`。
  路径由 `libobs/obs-android.c` 的 `add_default_module_paths()` 注册，模板与桌面一致：
  `bin=$ROOT/lib`、`data=$ROOT/share/obs/obs-plugins/%module%`。

### 谁是 core、谁是 plugin

`PluginManager`（插件管理器）只显示 `OBS_MODULE_TYPE_PLUGIN` 的模块，**跳过 core**
（`PluginManager.cpp:42` 的 `obs_is_core_module()`）；core 模块还免疫用户的 disabled 配置
（`obs-module.c:502`）。所以桌面版全新安装的插件管理器是**空的** —— `plugins/` 里那些走
`add_core_module()` 的都是 core。

Android 上据此分两类：

| 类别 | 谁 | 装载类型 | 在插件管理器里 |
|---|---|---|---|
| **core** | 8 个上游插件（image-source / obs-x264 / obs-filters / obs-transitions / text-freetype2 / rtmp-services / obs-outputs / obs-ffmpeg）+ obs-libfdk | `CORE` | 不显示，也不可禁用 |
| **plugin** | 4 个移植自有采集插件（android-audio / android-camera / android-capture / android-screen） | `PLUGIN` | **显示，可禁用**（重启生效） |

为什么这样分：core 留给"随包发布的必需功能"，禁用会直接把应用弄残（禁 obs-x264 就没法录）；
移植那几个是 Android 特有的**可选**采集能力，用户应该能关掉（比如某个 USB 采集在特定机型
上闹毛病时）。

实现就在 `plugins/CMakeLists.txt` 的 Android 分支：`add_subdirectory()` 之后、
`set_obs_core_modules()` 之前，把这 4 个从 `OBS_MODULES_ENABLED` 里 `list(REMOVE_ITEM)` 掉
—— 它们就不进 `obs_core_modules[]`，随后由 `loadPlugins()` 以 PLUGIN 装载。
（`obs-encode-sink` 是 CI 用的测试 sink、`nikon-source` 已不维护，两者都已从工程删除。）

> 顺序很关键：这一步必须在 `add_subdirectory()` **之后** —— `target_install_resources()`
> 是在 `add_subdirectory()` 期间按"是否在 `OBS_MODULES_ENABLED` 里"决定插件数据放
> `share/obs/obs-plugins/<模块>` 的；先剔掉会让数据落到错的位置。
### 按需加载：`$ROOT/lib` 也是用户能写的地方

两条规矩（见 `extractPluginBinaries()`）：

1. **只动自己登记过的文件**。解压时把本版清单写进 `.managed`；下次只删「上版登记过、本版
   没有」的文件。用户自己放进去的 `.so` 从来不在 `.managed` 里，永远不会被碰。
2. **版本标记 `.bundle-stamp`**。存 MANIFEST 内容的 SHA1（不是版本号 —— 插件集合一变标记
   就变）；标记一致且登记的文件都在就跳过解压。

于是：往 `$ROOT/lib` 放一个 `.so` 就能装插件、删掉就卸载，不需要重新打包。

### 三个容易踩的构建坑（都已修）

* **前端应用库被当成插件**。`cmake/android/helpers.cmake` 原来靠 Qt 内部属性
  `_qt_is_android_executable` 判断"这是前端不是插件"，Qt 6.11.1 里该属性失效 ⇒ 前端库被
  拷进 `rundir/lib/obs-plugins/`（99 MB）并被追加进 `OBS_MODULES_ENABLED`。
  现在再加一条**按目标身份**的判断：`if(_is_qt_android_app OR target STREQUAL obs-studio)`。
  （没被发现是因为 `os-android.cmake` 在 `frontend/CMakeLists.txt:105` 就把
  `OBS_MODULES_ENABLED` 读去生成头文件了，而 `obs-studio` 在 `:159` 才进 helpers。）
* **`assets` 不经过 Gradle 的 strip**。`rundir` 里的 `.so` 带 `-g`，直接收进 assets 会比
  原来大一圈；`build-apk.sh` 用 NDK 的 `llvm-strip --strip-unneeded` 补这一步。
* **`lib/obs-plugins/` 的不变式**。打包时**不做文件名过滤** —— "lib 前缀就排除"会把未来
  一个叫 `libfoo.so` 的合法插件静默漏掉。改成写入前**断言**该目录里没有 `libobs_*` /
  `libQt*`：不变式被破坏时直接 `die`，而不是悄悄出错。

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

## 构建速度

apk job 冷缓存时 6.2 分钟，大头在「构建 + 打包 + 签名」。拆开看：

| 阶段 | 耗时 |
|---|---|
| CMake configure | ~20–40s |
| ninja 编译 453 个目标 | ~225s（冷缓存；ccache 热起来后多数变命中） |
| Gradle 第一次（下载 gradle 发行包 + 起 daemon + packageRelease） | ~80s |
| Gradle 第二次（带插件重打包，增量） | ~6s |
| 其余（拷插件、签名、上传） | ~20s |

### 已做

**① 缓存 `~/.gradle`**

托管 runner 每次全新，`gradle-9.3.1-bin.zip`（100+ MB）每轮重下一次，还要冷启动 daemon。
workflow 用 `actions/cache` 缓存 `~/.gradle/wrapper` 和 `~/.gradle/caches`：
实测 Gradle 第一次 **82s → 20s**。不缓存 daemon 和日志（没意义，还有并发写的风险）。

**② ccache**

`compile-check.sh` 在 `USE_CCACHE=1` 时挂
`-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`。
按「预处理后的源码 + 编译器 + 完整编译选项」的内容哈希复用 `.o`：改选项、改头文件、
换分支都自动失效；**只会"没命中→变慢"，不会"该编没编→产物是旧的"**。

实测（同一提交重跑，热缓存）：

| | 冷缓存 | 热缓存 |
|---|---|---|
| apk job | 6.6 min | **2.6 min** |
| 构建 + 打包 + 签名 | 359s | **112s** |

ninja 仍然是 414 次"编译"——它只看 mtime，git checkout 之后永远认为全部过期；
但每次都是 ccache 命中，所以很快。**这正是 ccache 和 build 目录缓存的本质区别**：
前者把"误判为需要重编"变成廉价操作，后者要靠我们自己消除误判。

四个配置，缺一个就全 miss（等于没装）：

| 变量 | 为什么 |
|---|---|
| `CCACHE_COMPILERCHECK=content` | NDK 是解压出来的，不同 runner 上 mtime 不同。默认按 **mtime** 认编译器身份 ⇒ 每轮全 miss |
| `CCACHE_BASEDIR=$GITHUB_WORKSPACE` + `CCACHE_NOHASHDIR=true` | 绝对路径归一化，换 workspace 路径也能命中 |
| `CCACHE_COMPRESS=true` | `.o` 带 `-g` 很大，压缩后缓存体积和上传时间都降下来 |
| `CCACHE_MAXSIZE=3G` | 兜底，别把缓存撑爆 |

缓存用 `actions/cache/restore` + `actions/cache/save` **分开**：`key` 带 `github.sha`，每轮存一份新的，
`restore-keys` 前缀命中最近一份。用单件 `actions/cache@v4` 的话 key 固定后缓存就 immutable 了。

workflow 里还有一步 `ccache -s` 把命中率打出来 —— ccache 配错时**不报错、只是变慢**，
没有这个数字你分不清"配好了"和"根本没生效"。

### 试过并放弃：直接缓存 build 目录（`build-android-apk-arm64-v8a/`）

**能压到 1.1 分钟，但正确性要靠我们自己维护，所以放弃了。**

它确实更快（命中后 ninja 几乎不编、连 configure 都省），但踩了三个坑：

1. **恢复成功 ≠ 生效**。ninja 按 mtime 判断，`git checkout` 把源文件刷成"现在"、`.o` 是旧的
   ⇒ 453 个目标全量重编（实测 414/414，收益 0）。要按"缓存那次构建的提交"把 mtime 调回去，
   且 checkout 得 `fetch-depth: 0`。
2. `pip install git-restore-mtime` 在 runner 上**装不上**（PyPI 没这个包）。
3. 就算 mtime 修对了，**`cmake configure` 每次重跑**会重新生成 `obsconfig.h` 之类的头文件、
   mtime 刷成"现在"，又带起 296 个重编 —— 还得补一个"CMake 输入没变就跳过 configure"。

补完这三层能到 1.1 分钟，但整套逻辑的隐含前提是「`git diff` 能覆盖所有构建输入」。实际覆盖不住：

* 改 `compile-check.sh` 里的 `-D` 选项、改 workflow 里的 `API` / `BUILD_TYPE` —— key 不变、
  diff 不匹配，会**跳过 configure 静默用旧配置**；
* `deps-android` 的 prebuilt 换了但脚本没改 —— key 里没有 deps hash，会复用旧构建目录。

**这类错误不报错，只悄悄打出旧产物。** ccache 内容寻址，改 flags 自动失效，只会变慢不会变错，
所以换成了 ccache。

### 试过但回退：用 Release（`-O3`、无 `-g`）构建

**结论是反的 —— 更慢，已回退。**

| | RelWithDebInfo（现状） | Release |
|---|---|---|
| apk 构建+打包+签名 | 351s | **413s（+62s）** |
| APK 体积 | 44.1 MB | **44.3 MB（没变）** |

* 体积没变：**`-g` 的调试符号在 Gradle 打包时就已经 strip 掉了**（这也解释了为什么
  debuggable 和非 debuggable 两个包的 `.so` 字节级相同）。想靠优化级别瘦身是徒劳的。
* 变慢：`-O3` 比 `-O2` 编译开销大。

所以 `build-apk.sh` **不传** `BUILD_TYPE`，沿用 `compile-check.sh` 的默认 `RelWithDebInfo`。

### 还没做

| 项 | 预期收益 | 说明 |
|---|---|---|
| 提高 apk job 的命中率（目前 48.67%） | 106s 还能再降 | 未查。怀疑 frontend 那批 C++ / moc 的某个输入每轮都在变（如 build number 宏）。**不影响正确性**，只是少省几秒 |
| 合并 compile / apk 两个 job | 省一次 libobs 重复编译 | 会失去「快速失败闸门」，不划算 |

### 两个 job 共用 ccache：只有 apk job 保存

两个 job 用**同一套 key 格式**、**逐字相同的 ccache 配置**，这样一份缓存两边都能命中。
但**只有 apk job 保存**，compile job 只恢复不保存。

原因是踩过一个坑：key 里带 `github.sha`，同一轮提交两个 job 算出来的 key **完全相同**，
后保存的会被拒绝：

```
Failed to save: Unable to reserve cache with key ..., another job may be creating this cache.
```

实测是 compile job 先赢（它先跑），于是 apk job 那 414 个 frontend `.o` **从来没进过缓存**，
缓存永远停在只有 libobs/插件的那一小份 —— 最终 apk job 命中率只剩 33%，比不共享还差。

现在的分工：

| | 恢复 | 保存 |
|---|---|---|
| compile job（先跑，只编 libobs + 插件） | ✅ 拿上一轮的完整缓存 | ❌ |
| apk job（后跑，编的是超集） | ✅ 先拿上一轮的完整缓存 | ✅ 唯一写入方 |

这样两个 job 都能吃到"上一轮的完整缓存"，谁都不抢 key。代价是：如果 apk job 失败，
这一轮就不更新缓存（`if: always()` 已尽量缓解）。

> 别给它俩各配一个带 job 名的 key —— 那样两边都会存，但 `restore-keys` 只能挑"最近生成的
> 一份"，结果 apk job 反而可能拿到 compile 那份小缓存。单一写入方更简单也更准。

---

## 发布

### 流程

```bash
git tag v33.0.1-pre1
git push origin v33.0.1-pre1
```

tag 推上去之后 `.github/workflows/android-release.yml` 会：

1. 通过 `workflow_call` 复用 `android-compile.yml`（依赖 → 编译 → 打 APK），
   构建步骤只有一份，不在两个文件里各写一遍
2. 下载 APK artifact，生成 `SHA256SUMS.txt`
3. `gh release create <tag> --prerelease`，把 APK 和校验和挂上去

**为什么 release 单独一个 workflow 文件**：`android-compile.yml` 的 push 触发器带 `paths`
过滤，而 paths 对 tag push 的匹配规则容易出意外 —— tag 指向的提交若没改到白名单里的路径，
整条流水线根本不会启动，表现是「打了 tag、什么都没发生」。

Release notes 在 `.github/release-notes.md`，改文案改那里。

### 版本号

两处一起改，保持一致：

| 位置 | 变量 |
|---|---|
| `frontend/cmake/os-android.cmake` | `QT_ANDROID_VERSION_NAME` / `QT_ANDROID_VERSION_CODE` |
| `frontend/cmake/android/AndroidManifest.xml` | `android:versionName` / `android:versionCode` |

`versionCode` 编码方案 `major*10000 + minor*100 + patch`（33.0.1 → `330001`），
必须**单调递增** —— 否则系统拒绝安装更新（`INSTALL_FAILED_VERSION_DOWNGRADE`），
用户只能卸载重装，而卸载会丢配置和场景。

### 签名：pre-release 用临时密钥，正式版必须换

现在 `android/build-apk.sh` 每次构建都**现生成一个新的 debug keystore**
（`--ks-key-alias androiddebugkey`）。pre-release 阶段可以接受，代价是
**用户升级前必须先卸载**（签名不匹配装不上），配置会丢。

**正式版之前必须换成固定的 release keystore**（存 GitHub Secrets，workflow 里解出来用）。
否则每个版本都是「另一个应用」，用户永远无法覆盖安装。

### debuggable 必须关

`AndroidManifest.xml` 的 `<application>` 上不能有 `android:debuggable="true"`。
留着的代价：包更大、运行更慢（ART 不做 AOT）、`run-as` 可用（任何人能进应用私有目录）、
应用商店直接拒收。默认就是 false，不用显式写。

---

## 代码风格

上游的 `.clang-format` / `.editorconfig` / `CODESTYLE.md` 全部适用，**新增代码要遵守**。

### 硬性规则（可自动检查）

| 规则 | 来源 | 当前状态 |
|---|---|---|
| 缩进用 **tab**，宽度 8 | `.editorconfig` + CODESTYLE | ✅ 全仓库一致 |
| 行宽 ≤ **120** | `.clang-format` `ColumnLimit` | ✅ 新增文件 0 处超标 |
| 文件末尾有换行 | `.editorconfig` `insert_final_newline` | ✅ |
| 行尾不留空白 | `.editorconfig` `trim_trailing_whitespace` | ✅ |
| UTF-8 | `.editorconfig` | ✅ |
| CMake 用 2 空格 | `.editorconfig` | ✅ |
| 类访问修饰符不缩进 | CODESTYLE | ✅ |
| 私有成员尾下划线 | CODESTYLE | ✅ |

> `.clang-format` 里那段 `IndentWidth: 4` / `UseTab: Never` 属于 `Language: ObjC`，
> **不是 C++ 的**。C++ 走的是文件开头那段基础配置（tab / 8 / 120）。
> 误读这一段会以为 C++ 该用 4 空格 —— 实际代码和 CODESTYLE 都是 tab。

### 已知未遵守的（新代码里仍存在）

**① C++ 的 if/for/while 缺大括号**

CODESTYLE：「All new C++ code needs to use curly braces for all looping or branching
statements. **No exceptions**」，`.clang-format` 也开了 `InsertBraces: true`。

新增的 C++ 文件里约 60 处、`OBSApp.cpp` 的过滤器区约 22 处是单语句无括号写法。
（`.c` 文件不受此约束 —— C 走 Linux 内核风格，允许单语句不加大括号。）

**② 平台相关的 include 块**

CODESTYLE 明确禁止用宏把 `#include` 包起来（原文：*"Instead of using a macro to
conditionally compile code ... well, don't do that at all"*）。仓库里约 24 处
`#ifdef __ANDROID__` 包着 `#include`。

多数可以去掉 —— 被包含的头文件自己带 `#ifdef __ANDROID__` 守卫（例如
`android-window-fit.hpp` 在非 Android 平台展开为空），无条件包含是安全的。

**③ include 块内未按字母序**

CODESTYLE 要求每个 include 块内 case-sensitive 字母序。抽查 4 个上游文件有 3 个完全有序，
我们新增的文件里有 18 个不满足。注意 `.clang-format` 里 `SortIncludes: false`，
clang-format **不会**自动排序，只能手动或用 `clang-format --sort-includes` 单独处理。

**④ 新常量命名**

CODESTYLE：新常量用 `kCamelCase`。`OBSApp.cpp` 的 `HOLD_MS` / `MOVE_SLOP`
是从旧代码搬过来的 `UPPER_SNAKE`，按新规范应为 `kHoldMs` / `kMoveSlop`。

---

## 待办

### 已知缺陷

- [x] ~~**【发版阻塞】非 debuggable 包里内置插件全部加载失败**~~
      **已解决**（见上面「插件分发与加载」一节）。根因是安装时系统只把 `lib*.so` 解到
      `nativeLibraryDir`，而我们的插件名不带 `lib` 前缀 ⇒ 一个都没解出来。改成插件走
      APK 的 `assets/`、启动时解到 `$OBS_ROOT_PATH/lib`、再用 `obs_load_plugins()` 扫目录
      加载之后，非 debuggable 包实测 **13 个插件、0 失败**，顺带支持用户按需增减插件。
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
- [x] `AndroidManifest.xml` `debuggable="true"` —— 已在 `58cc365b` 去掉。**但去掉后暴露出
      上面那条「非 debuggable 包插件全加载失败」的阻塞问题，修好之前不能发 release 包。**
- [ ] 正式版签名：现在每次构建都现生成 debug keystore，用户升级必须卸载重装（丢配置）。

### 未做真机功能验证

- [ ] 录制 / 推流 / 屏幕采集 / 系统内录 / 相机方向（日志里 `渲染期旋转=N°`）

### 工程债

- [ ] `AndroidInputFilter`（`OBSApp.cpp`，约 500 行含 170 行注释、11 个 case）承担 5 件
      互不相关的事：长按=右键、BACK=Escape、组合框弹层补丁(P-8)、clamp、QScroller 挂载。
      建议拆成 4 个单责过滤器
- [ ] 探针日志 63 处：约 20 处有长期价值、约 21 处是每事件残留。建议加
      `OBS_ANDROID_PROBE` 运行时开关（默认关），长期项降到 `LOG_DEBUG`
- [ ] 容器格式限制 5 处各自编码、预览面所有权 5 处各自实现 —— 收敛成单一入口
- [ ] CI：缓存 key 已含 `NDK_VERSION`（换 NDK 会整份重建），但仍不含 `API`；
      `always()` 保存半成品 prebuilt 可能毒化缓存；
      paths 漏了 `deps/**`；`.gitignore` 对 `android/deps-android/{prebuilt,src}` 无规则
- [ ] 构建可重现性：x264/rnnoise 钉的是 `master` 分支且无 checksum；每次构建新生成 keystore；
      `apksigner`/`aapt2` 用 `sort -V | tail -1` 取最新

**代码风格**（见上面「代码风格」一节，按影响排序）

- [ ] C++ 的 `if`/`for`/`while` 缺大括号：约 82 处（`OBSAndroidDisplay.cpp` 18、
      `OBSAndroidPermissions.cpp` 14、`OBSAndroidUsb.cpp` 12、`android-window-fit.hpp` 9、
      `OBSAndroidKeepAlive.cpp` 6、`OBSApp.cpp` 过滤器区 22、其余 1）。
      CODESTYLE 要求「no exceptions」，`.clang-format` 有 `InsertBraces: true` —— 跑一次
      `clang-format -i` 就能修。**只作用于我们改过的文件，别碰上游文件。**
- [ ] 平台相关 include 块：约 24 处 `#ifdef __ANDROID__` 包着 `#include`，CODESTYLE 明确禁止。
      多数可改成无条件包含（被包含的头文件自守卫，如 `android-window-fit.hpp` 非 Android 展开为空），
      逐个确认后再改。
- [ ] include 块内未按字母序：18 个新增文件。`.clang-format` 的 `SortIncludes: false`
      不会自动排序，需要手动或 `clang-format --sort-includes`。
- [ ] 新常量命名：`OBSApp.cpp` 的 `HOLD_MS` / `MOVE_SLOP` → `kHoldMs` / `kMoveSlop`
      （CODESTYLE 要求新常量 `kCamelCase`）
