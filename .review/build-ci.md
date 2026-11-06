# 构建系统 / CI / 打包 / 依赖 —— 代码审查报告

> 审查员：`build-ci`（只读审查，未修改任何源码文件）
> 仓库：`D:\Agent工作区\OBS-For-Android`，上游基线 `65c320b`（33.0.1），`git merge-base upstream/master HEAD = 65c320b46e1af5c7a555161aea6182cdf7a8c524`
> 差异查看方式：`git diff upstream/master...HEAD -- <path>`；上游原版 `git show 65c320b:<path>`
> 全量差异：137 文件 / +21932 / -148。本方向共 17 个文件 / **+1991 / -2**（`git diff upstream/master...HEAD --numstat` 逐项求和）。

---

# 范围

本次只审以下文件（其余方向由其他审查员负责）：

| 类别 | 文件 |
|---|---|
| Android 顶层脚本 | `android/build-apk.sh`、`android/compile-check.sh` |
| 依赖交叉编译 | `android/deps-android/{build.sh,defs.sh,fetch.sh,versions.sh}` |
| CMake 新增平台层 | `cmake/android/{compilerconfig.cmake,defaults.cmake,helpers.cmake}` |
| 上游文件（修改） | `cmake/common/osconfig.cmake`、`CMakeLists.txt`、`.gitignore` |
| GLES 依赖集成 | `deps/glad-gles/CMakeLists.txt`（只看 CMake 集成，代码本体由 graphics-gles 审） |
| CI / 发布 | `.github/workflows/android-compile.yml`、`.github/workflows/android-release.yml`、`.github/release-notes.md` |
| 交叉界面 | `plugins/CMakeLists.txt`（只评估与 CI 构建流程的交互，主审为 plugins-android） |

同时完整阅读了 `PORTING.md`（780 行）、`CODESTYLE.md`、`.clang-format`、`.editorconfig`，并按需调取上游 33.0.1 的 `cmake/common/osconfig.cmake`、`cmake/linux/`、`cmake/common/bootstrap.cmake`、`plugins/CMakeLists.txt`、`frontend/CMakeLists.txt`、`.github/workflows/*`、`CMakePresets.json`、`build-aux/` 作为对照。

**核验手段**：所有结论均给出 `文件:行号` 或命令输出。使用过的命令包括 `git diff/numstat/show/ls-tree/grep/ls-files/check-ignore`、`git check-ignore -v --no-index`、`Select-String`，以及一次 Git Bash 实测（`& 'C:\Program Files\Git\bin\bash.exe' -c '...'`）。

---

# 做了什么

1. **建立一套与上游平行的 Android 顶层构建体系**：新增 `android/` 目录，含 `compile-check.sh`（配置+编译验证）、`build-apk.sh`（出可安装 APK）、`deps-android/`（15 个第三方依赖的下载与交叉编译）。不复用上游的 `CMakePresets.json` / `build-aux/` zsh 脚本（`git diff upstream/master...HEAD --name-only -- build-aux CMakePresets.json` 输出为空，即两者零改动）。

2. **按上游分层补了 Android 的 CMake 平台模块**：`cmake/android/` 下新增 `compilerconfig.cmake` / `defaults.cmake` / `helpers.cmake` 三件套，文件名与 `cmake/linux/`（`git ls-tree --name-only 65c320b:cmake/linux`）逐字一致；由 `cmake/common/osconfig.cmake:5-12` 把 `cmake/android` 追加进 `CMAKE_MODULE_PATH`，让根 `CMakeLists.txt:14-16` 的 `include(compilerconfig/defaults/helpers)` 自动解析到 Android 版。

3. **改了 3 个上游文件做 OS 分派**：`cmake/common/osconfig.cmake` 在最前面插入 Android 判定分支（+9/-1）；根 `CMakeLists.txt:7` 给 Windows 架构检查加 `AND NOT OS_ANDROID`（+1/-1）；`.gitignore` 放行 `!/android`、`!/PORTING.md` 并忽略 `frontend/cmake/android/assets/`（+7/-0）。

4. **新增 GLES 版 glad 并挂到 libobs-opengl**：`deps/glad-gles/CMakeLists.txt`（39 行，新建），`libobs-opengl/CMakeLists.txt` 用 `if(OS_ANDROID)` 在 `deps/glad` 与 `deps/glad-gles` 之间切换，两个目录的 target/alias 名字刻意相同（`obsglad` / `OBS::glad`），使下游无需加平台分支。

5. **插件白名单 + 提前 `return()`**：`plugins/CMakeLists.txt` 在 `if(OS_ANDROID)` 分支（`@@ -21,6 +21,165 @@`，+159/-0，纯插入）里只 `add_subdirectory()` 15 个模块，`list(REMOVE_ITEM)` 掉 4 个可选采集插件，补调 `set_obs_core_modules()` 后 `return()`，跳过上游全部 `add_core_module()`。

6. **两套 GitHub Actions workflow**：`android-compile.yml`（503 行）含 `compile`（依赖+原生层编译、无 Qt）与 `apk`（Qt 6.11.1 前端 + androiddeployqt + 签名）两个 job；`android-release.yml`（53 行）在 tag push 时通过 `workflow_call` 复用前者，下载 artifact、生成 `SHA256SUMS.txt`、`gh release create --prerelease`。

7. **依赖供应链**：`versions.sh` 作为"唯一真源"钉 15 个依赖的版本与 URL；`fetch.sh` 在 Windows Git Bash 侧下载（GitHub 域名可走 `OBS_PROXY`）；`build.sh` 在 WSL2 Ubuntu 侧交叉编译成静态库，产物同步到 `android/deps-android/prebuilt/<abi>/`，并把 CMake 包里的绝对路径重定位成 `${_IMPORT_PREFIX}`。

8. **缓存/加速体系**：deps 缓存（`actions/cache/restore`+`save` 分离，key 含 ABI/NDK/versions+defs hash）、Qt 缓存（单件 `actions/cache@v4`）、`~/.gradle` 缓存、ccache（`restore`/`save` 分离、`key` 带 `github.sha`、仅 apk job 保存、`CCACHE_COMPILERCHECK=content` 等 4 项配置）。

9. **APK「打两遍」方案**：第一遍 `compile-check.sh(ENABLE_FRONTEND=ON)` 由 Qt 的 CMake 自动调 androiddeployqt（此时插件未产出）；随后把插件 `.so` 暂存到 `frontend/cmake/android/assets/obs-plugins/`（清目录+拷+`llvm-strip`+生成 MANIFEST），删 Gradle 产物后第二遍跑 androiddeployqt，最后 `apksigner` 用自动生成的 debug keystore 签名。

10. **发布与合规相关**：`.github/release-notes.md`（39 行）描述功能/安装/已知问题；`PORTING.md` 用大量篇幅记录构建坑、`OBS_VERSION_OVERRIDE`、`compileSdk 36 + targetSdk 35`、版本号双处维护、ccache 设计取舍、以及"正式版必须换固定 release keystore"等已知债务。

---

# 侵入性评估（含量化：改动行数）

## 量化

`git diff upstream/master...HEAD --numstat -- android/ cmake/android/ .github/ .gitignore CMakeLists.txt cmake/common/osconfig.cmake deps/glad-gles/CMakeLists.txt plugins/CMakeLists.txt` 输出：

```
39  0  .github/release-notes.md
503 0  .github/workflows/android-compile.yml
53  0  .github/workflows/android-release.yml
7   0  .gitignore
1   1  CMakeLists.txt
235 0  android/build-apk.sh
127 0  android/compile-check.sh
182 0  android/deps-android/build.sh
228 0  android/deps-android/defs.sh
65  0  android/deps-android/fetch.sh
88  0  android/deps-android/versions.sh
24  0  cmake/android/compilerconfig.cmake
42  0  cmake/android/defaults.cmake
190 0  cmake/android/helpers.cmake
9   1  cmake/common/osconfig.cmake
39  0  deps/glad-gles/CMakeLists.txt
159 0  plugins/CMakeLists.txt
```

- **本方向合计：17 文件，+1991 / -2**。其中 **13 个纯新增文件共 +1815 行**，**4 个上游文件共 +176 / -2**。
- 上游文件逐处：

| 文件 | 改动 | 具体内容 |
|---|---|---|
| `CMakeLists.txt:7` | +1/-1 | `if(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")` → `if(... AND NOT OS_ANDROID)`；避免 Windows 宿主交叉编 Android 时进入 `cmake/windows/architecture.cmake`（该文件 `:10` 有 `return()`，会直接终止整个 configure）。 |
| `cmake/common/osconfig.cmake:5-13` | +9/-1 | 把原 `if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")` 改成 `if(Android 判定) ... elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")`；新增分支设 `CMAKE_C/CXX_EXTENSIONS FALSE`、追加 `cmake/android` 到 module path、置 `OS_ANDROID TRUE`。 |
| `.gitignore:6,31-32,56-58` | +7/-0 | `!/android` 放行新顶层目录；`!/PORTING.md` 放行文档；忽略 `/frontend/cmake/android/assets/`。注意第 2 行 `/*` 是上游原有规则（`git show 65c320b:.gitignore` 一致），移植未改。 |
| `plugins/CMakeLists.txt:24-181` | +159/-0 | `git diff` 唯一 hunk 头 `@@ -21,6 +21,165 @@ endif()`，即**纯插入**、零删除；插在上游 `ENABLE_CORE_MODULES` 检查之后、原有 `macro(check_obs_browser)` 之前。 |

- 另有 `libobs-opengl/CMakeLists.txt`（+12/-2）与 `frontend/CMakeLists.txt`（+49/-2，hunk 为 `@@ -28,8 +28,29 @@`、`@@ -80,6 +101,8 @@`、`@@ -100,6 +123,14 @@`、`@@ -113,4 +144,16 @@`）等目录外的改动承担 GLES/前端接线，属其他方向，本报告只在"跨范围观察"提示。

## 对桌面 / 其他平台构建的影响

**结论：桌面构建路径未被破坏，但存在 1 处隐性耦合风险。**

- `cmake/common/osconfig.cmake` 在 `project()` **之前**被 include（`cmake/common/bootstrap.cmake:62` → 根 `CMakeLists.txt:3-5`），此时 `CMAKE_SYSTEM_NAME` 尚未由工具链文件设置（该文件注释 `:7-8` 自述）。对原生 Linux/Windows/macOS 构建：未传 `-DCMAKE_TOOLCHAIN_FILE`（或路径不以 `android.toolchain.cmake` 结尾）且未显式传 `-DCMAKE_SYSTEM_NAME=Android` → `osconfig.cmake:5` 条件为假 → 落到 `:13/:18/:23` 原有的 host 分支。**上游逻辑一字未改，桌面三平台不受影响。**
- 根 `CMakeLists.txt:7` 的 `AND NOT OS_ANDROID`：非 Android 时 `OS_ANDROID` 未定义 → `NOT OS_ANDROID` 为真 → 与原行为完全一致（已核对，`if()` 对未定义变量按假处理）。
- `.gitignore` 是纯增量，不影响任何已跟踪文件（`git diff` 无删除行）。
- `plugins/CMakeLists.txt` 的 Android 分支由 `if(OS_ANDROID)`（`:24`）包裹，`return()`（`:180`）只在该分支内执行；桌面路径从 `:183` 继续跑上游原文（`git show 65c320b:plugins/CMakeLists.txt` 为 120 行，当前 279 行，差 159 行正好是插入量）。
- `deps/glad-gles/` 是新增目录，`libobs-opengl/CMakeLists.txt` 的切换被 `if(OS_ANDROID)` 包住；非 Android 仍 `add_subdirectory(deps/glad)`。

**风险点**：`OS_ANDROID` 的识别依赖"工具链文件名"这一启发式（详见 P2-1）。若上游日后给 `osconfig.cmake` 增加新的宿主分支或 `include_guard` 语义变化，这个插在最前面的分支会首先被求值，需要 rebase 时专项回归——这正是 `PORTING.md:117-118` 自己列作"通用检查项"的那类隐患。

## 提前 `return()` 排查

| 位置 | 是否跳过上游初始化 | 现状 |
|---|---|---|
| `plugins/CMakeLists.txt:180` | **是**：跳过上游 `:205-277` 全部 `add_core_module()` 与 `:279` 的 `set_obs_core_modules()` | 已在 `:179` 补调 `set_obs_core_modules()` 补偿；`PORTING.md:105-118` 描述的 `obs-core-modules.c` 缺失故障已修。**但这是全仓库最脆弱的一处**：任何上游在 `:183-279` 之间新增的全局初始化，Android 分支都会再次静默漏掉。 |
| `cmake/common/osconfig.cmake:5-12` | 否（只新增分支，无 return） | 安全 |
| 根 `CMakeLists.txt:7` | 否 | 反而是**防止** `cmake/windows/architecture.cmake:10` 的 `return()` 误伤 Android 交叉构建 |
| `frontend/CMakeLists.txt:5-8` | 否（`-DENABLE_FRONTEND=OFF` 时 `return()`，但这是**上游原版**，`git show 65c320b:frontend/CMakeLists.txt` 前 10 行一致；`git diff` 的 4 个 hunk 都不涉及这一段） | CI `compile` job 依赖此上游行为跳过 Qt，属实 |

---

# 是否符合上游多平台风格（对照具体上游文件与行号）

## 遵循得好的部分（CMake 分层）

上游 `cmake/common/osconfig.cmake`（`git show 65c320b:cmake/common/osconfig.cmake`）用一个 `if/elseif` 链按 `CMAKE_HOST_SYSTEM_NAME` 分派，每支做三件事：关扩展、把 `cmake/<os>` 追加进 `CMAKE_MODULE_PATH`、置 `OS_<XXX> TRUE`。移植的 `cmake/common/osconfig.cmake:5-12` **完全照抄这个形状**，只是把判定换成 Android 专用条件。

上游 `cmake/linux/` 的文件集合（`git ls-tree --name-only 65c320b:cmake/linux`）是：

```
compilerconfig.cmake / cpackconfig.cmake / defaults.cmake / ecmconfig.cmake /
helpers.cmake / toolchain-aarch64-clang.cmake / toolchain-aarch64-gcc.cmake / toolchain-x86_64-gcc.cmake
```

移植 `cmake/android/` 取了其中与编译相关的三件套 `compilerconfig.cmake`（24 行）、`defaults.cmake`（42 行）、`helpers.cmake`（190 行），并明确在 `cmake/android/helpers.cmake:3-8` 注释"以 cmake/linux/helpers.cmake 为蓝本裁剪"。根 `CMakeLists.txt:14-16` 的 `include(compilerconfig)` / `include(defaults)` / `include(helpers)` 无需改动即解析到 Android 版（module path 由 `osconfig.cmake:11` 追加）——**这是最贴合上游风格的一处设计**。

`os-*.cmake` 组织也对齐：上游 `git ls-tree --name-only 65c320b:libobs/cmake` 含 `os-freebsd/os-linux/os-macos/os-windows.cmake`，`frontend/cmake` 同名；移植新增 `libobs/cmake/os-android.cmake` 与 `frontend/cmake/os-android.cmake`，并在 `libobs/CMakeLists.txt:268-269` 用 `elseif(OS_ANDROID) include(cmake/os-android.cmake)` 接入（`grep` 结果）。

`deps/glad-gles/CMakeLists.txt:16-17,27,37,39` 的结构（`cmake_minimum_required` → `add_library(obsglad OBJECT)` → `add_library(OBS::glad ALIAS obsglad)` → `target_sources` → `target_include_directories` → `target_link_libraries` → `set_target_properties(... FOLDER deps)`）与上游 `deps/glad/CMakeLists.txt`（`git show 65c320b:deps/glad/CMakeLists.txt`）几乎逐行对应，只把 `find_package(OpenGL REQUIRED)` 换成 NDK 的 `EGL GLESv3`。`libobs-opengl/CMakeLists.txt` 的 `if(OS_ANDROID)` 切换也保持了上游 `if(NOT TARGET OBS::glad)` 的框架。

## 自成一套平行体系的部分

1. **顶层构建入口**：上游用 `CMakePresets.json` + `build-aux/` 下的 zsh 脚本 + `.github/workflows/*.yaml`（注意扩展名是 `.yaml`）组织一切。移植新增 `android/*.sh`（bash）、完全不碰 `CMakePresets.json` / `build-aux/`（diff 为空），workflow 用 `.yml`。`PORTING.md:49-70` 的"本地构建"是 4 条裸 `bash` 命令，不是 preset。**结论：CMake 内部遵循了上游分层，但"人怎么发起一次构建"是另一套平行体系。**

2. **CI 组织方式**：上游 `git show 65c320b:.github/workflows/push.yaml`（前 35 行）用 `paths-ignore: '**.md'` 黑名单、`branches` + `tags` 同时声明、job 级 `permissions: contents: read`、`uses: ./.github/workflows/build-project.yaml` + `secrets: inherit`，并把 action 钉到 SHA（`actions/checkout@de0fac... # v6.0.2`）。`build-project.yaml` 用 `defaults.run.shell: bash`、`fetch-depth: 0`。移植的 `android-compile.yml` 用 `paths` 白名单（`:30-41`）、`workflow_dispatch` + `workflow_call` + `push` + `pull_request` 混在一个文件、全部 `actions/*@v4` 版本标签、无 `permissions` 块。**风格差异明显，且白名单策略已经造成过实际漏跑**（`android-compile.yml:35-38` 自述"改 OBSApp.cpp 时一个 run 都没触发"）。

3. **OS 判定维度**：上游全用 `CMAKE_HOST_SYSTEM_NAME`；移植必须改用 `CMAKE_SYSTEM_NAME` + 工具链文件路径正则（`osconfig.cmake:5`），因为它是交叉编译。这是合理让步，但确实是"平行"的第二套判定逻辑（见 P2-1）。

4. **依赖管理**：上游依赖由 obs-deps 预编译包 + `cmake/finders/Find*.cmake` 提供；移植自建 `deps-android/` 下载+编译+同步到 `prebuilt/`，并自己写了 `_relocate_cmake_package`（`build.sh:118-128`）来修补 WSL→Windows 的绝对路径。上游没有对应机制。

5. **打包**：上游 `cpackconfig.cmake` / `cmake/linux/cpackconfig.cmake`（`git ls-tree` 可见）负责分发；移植完全绕开，用 `androiddeployqt` + 自写 `build-apk.sh` 的"打两遍"流程。

**总评**：CMake 目录级分层与命名**符合**上游多平台风格（这是本方向最值得肯定的一点）；但构建入口、依赖获取、CI、打包四层是**自成一套的平行体系**。这是 Android 交叉编译的客观约束使然，代价是每次 rebase 上游时，这四层都要人工同步（`PORTING.md:117-118` 已意识到 CMake 层的同步问题，但未覆盖这四层）。

---

# 问题清单

> 每条格式：`[编号] 标题` → 证据 / 影响 / 改进建议（含替代方案与代价）。

## P0 发版阻塞

### P0-1 每次 CI 构建新生成 debug keystore，且 `KEYSTORE` 逃生舱被硬编码别名/密码废掉

**证据**
- `android/build-apk.sh:174-183`：`KS="${KEYSTORE:-}"`，为空时 `KS="$BUILD/debug.keystore"`，`[ ! -f "$KS" ]` 才 `keytool -genkeypair ... -alias androiddebugkey -storepass android -keypass android`。
- `android/build-apk.sh:192-193`：`"$APKSIGNER" sign --ks "$KS" --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out "$SIGNED" "$SIGNED"` —— 别名、库密码、密钥密码**全部写死**。
- `.github/workflows/android-compile.yml:303` 每个 job 都是全新 `actions/checkout@v4` + 全新 runner，`$BUILD` 目录不会跨 run 保留 → CI 每次都新建 keystore。
- `PORTING.md:655-662`、`.github/release-notes.md:32`、`PORTING.md:747` 均承认此问题。

**影响**
1. 每个 pre-release APK 的签名证书都不同，用户覆盖安装报 `INSTALL_FAILED_UPDATE_INCOMPATIBLE`，只能卸载重装 → 丢场景/配置；`PORTING.md:652-653` 的 `versionCode` 单调递增设计因此形同虚设。
2. **更严重**：即便按 `PORTING.md:661` 把固定 release keystore 放进 GitHub Secrets 并设置 `KEYSTORE`，`--ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android` 会让真实 keystore（别名/密码几乎必然不同）签名直接失败。**"正式版换固定 keystore"这条路现在被代码堵死**，不是"还没做"而是"做了也不生效"。
3. `keytool` 的 `>/dev/null 2>&1`（`:181`）吞掉全部诊断，失败时只剩一句无上下文的 `die`。

**改进建议（推荐 A）**
- **A（最小改动、立即可用）**：把 `:174-195` 参数化为 `KS_ALIAS="${KS_ALIAS:-androiddebugkey}"`、`KS_PASS="${KS_PASS:-android}"`、`KEY_PASS="${KEY_PASS:-android}"`，`apksigner` 用这三个变量；`keytool` 生成 debug key 时用同样的变量并去掉 `>/dev/null 2>&1`。CI 里从 Secrets 解出 base64 keystore 到 `$RUNNER_TEMP/release.keystore`，设 `KEYSTORE` + 三个密码 env；`android-release.yml:22` 的 `uses:` 需加 `secrets: inherit`（否则被调用 workflow 拿不到 secrets）。**代价**：需要托管密钥与轮换流程；debug 默认路径仍保留，但要在日志里显式打印"⚠ 使用临时 debug 密钥，禁止发布"并在 release workflow 里加断言（`if: github.ref_type == 'tag' && !env.KEYSTORE` → `exit 1`）。
- **B**：改用 Android Gradle Plugin 的 `signingConfigs` 在 Gradle 层签名（`androiddeployqt --release` 产出未签名 APK，由 Gradle 用 release config 签）。**代价**：要接管 Qt 生成的 `build.gradle`（`android-compile.yml:478-483` 显示它是生成物），与"打两遍"流程耦合更深，且要处理 Qt 升级时模板漂移；不推荐在当前阶段引入。
- **C**：继续用 debug key 但把 keystore 提交进仓库/CI 缓存。**代价**：私钥进版本库，等于没有签名，任何人都可签名冒充；明确否决。

### P0-2 APK 静态链接 fdk-aac，与 OBS 的 GPL 许可证不兼容（需法务确认）

**证据**
- OBS 许可证为 **GPLv2 or later**：`libobs/obs.h:3-14` 明确 "either version 2 of the License, or (at your option) any later version"；`COPYING` 是 GPLv2 全文。
- fdk-aac 被钉入依赖并编成静态库：`android/deps-android/versions.sh:35`（`fdk-aac) echo 2.0.3`）、`:58`（下载 URL）、`android/deps-android/defs.sh:141-155`（`build_fdk_aac`，含自制 `log/log.h` 垫片）；产物 `libfdk-aac.a`。
- 构建全程开启：`android/compile-check.sh:113` `-DENABLE_LIBFDK=ON`；`plugins/CMakeLists.txt:150-158` `add_subdirectory(obs-libfdk)`；`plugins/obs-libfdk/CMakeLists.txt`（`git ls-files` 可见）通过 `Libfdk::Libfdk` 静态链接。
- 随 APK 分发：`.github/release-notes.md:15` 明写"音频编码：AAC（FFmpeg / FDK-AAC）"。
- 上游参照：`git --no-pager grep -n 'ENABLE_LIBFDK' 65c320b -- CMakePresets.json build-aux` → `65c320b:build-aux/com.obsproject.Studio.json:145: "-DENABLE_LIBFDK=ON"`。即上游 Flatpak 构建也开了它，移植不是自创；但 APK 是把 `libfdk-aac.a` **静态链进随包分发的 `.so`**，合并分发的法律风险高于动态链接系统库。

**影响**：Fraunhofer FDK AAC 使用自定义许可，FSF 与 FFmpeg 社区长期认定为**非自由且与 GPL 不兼容**（FFmpeg 官方因此拒绝发布带 `--enable-libfdk-aac` 的 GPL 构建）。把 GPLv2+ 的 OBS 与 fdk-aac 静态链接成单一 APK 对外分发，属于合并作品，GPL 要求整部作品可按 GPL 条款再分发，而 fdk-aac 的条款不满足该条件 → **分发的合法性存疑**。这是公开发 GitHub Release 前的实质阻塞，不是代码 bug。

**改进建议（推荐 A）**
- **A（零技术成本、推荐）**：发版构建关闭 `-DENABLE_LIBFDK=ON`（`android/compile-check.sh:113`），并从 `plugins/CMakeLists.txt:158` 移除 `add_subdirectory(obs-libfdk)`；AAC 用 FFmpeg 原生编码器（`obs-ffmpeg` 已在白名单 `plugins/CMakeLists.txt:149`，`PORTING.md:122-128` 说明 `aac_encoder_info` 由它注册）。**代价**：少一个高音质 AAC 编码器选项；用户侧几乎无感，OBS 桌面版默认也是 FFmpeg AAC。
- **B**：保留但改为运行时 `dlopen` 系统 `libfdk-aac.so`，不随包分发。**代价**：Android 系统不提供该库，等于实际不可用；且 GPL 对"是否构成合并作品"的判定仍不因动态链接而自动豁免，法务仍需确认。
- **C**：取得 Fraunhofer 的书面 GPL 兼容授权 / 换用 GPL 兼容 AAC 实现后再发。**代价**：周期长、不确定。
- 无论选哪个，发版前应由法务出具书面意见；同时在 `.github/release-notes.md` 增加"第三方组件许可证清单"章节（见 P2-9）。

### P0-3 依赖下载无校验和，且 x264 / rnnoise 钉在 `master` 分支，缓存 key 又不感知内容

**证据**
- `android/deps-android/versions.sh:25` `x264) echo master ;;`，`:31` `rnnoise) echo master ;;`，`:48` / `:54` 对应 URL 是 `.../archive/refs/heads/master.tar.bz2` / `.../archive/refs/heads/master.tar.gz`（分支快照，非不可变 tag）。
- `android/deps-android/fetch.sh:54-58`：`curl -fL --retry 3 --retry-delay 2 -C - -sS $prox -o "$out.part" "$url"`，**全程无 sha256/md5/gpg/签名校验**；对 `android/deps-android/*.sh` 跑 `Select-String -Pattern 'sha256|sha1|md5|checksum|gpg|verify'` 输出为空，确认无校验逻辑。
- `android-compile.yml:129` deps 缓存 key = `deps-${{ env.ABI }}-ndk${{ env.NDK_VERSION }}-${{ hashFiles('android/deps-android/versions.sh','android/deps-android/defs.sh') }}`，且 `:124-126` 注释明确"故意不加 restore-keys"。`master` 内容变化时 `versions.sh` 与 `defs.sh` 都不变 → key 不变 → **缓存永远复用同一份旧 x264/rnnoise 产物，也永远拉不到"新 master"**；除非手工删缓存。

**影响**：供应链攻击面（`-L` 跟随重定向到任意主机、`-C -` 断点续传配合无校验可写入截断/污染包）、构建不可重现（`PORTING.md:764-765` 自认）、且与缓存耦合后问题被"钉死"在缓存里。公开发布一个哈希不可复现、且含未校验分支快照的 APK，无法满足基本的可重现构建/供应链要求。

**改进建议**
- 把 `x264` / `rnnoise` 钉到**不可变 commit SHA 或签名 tag**（`versions.sh:25,31,48,54`），例如 `x264) echo 31e19f92f00c7003fa115047ce50978bc98c4da609f5`，URL 改 `/archive/<sha>/x264-<sha>.tar.bz2`。
- 新增 `android/deps-android/SHA256SUMS`（15 行），`fetch.sh:54-58` 下载后 `sha256sum -c` 校验通过才 `mv "$out.part" "$out"`；校验和文件加入缓存 key 的 `hashFiles(...)` 列表（`android-compile.yml:129,157`）。
- `curl` 增加 `--proto '=https' --tlsv1.2 --fail`，去掉 `-C -` 或对 `.part` 先校验再续传。
- **代价**：每次升级依赖要重新计算 15 个 hash；首次接入需人工核对上游发布页/签名，约半天工作量。换来的是可重现性与供应链完整性，值得。
- 替代方案（更重、不推荐）：把 15 个 tarball 全部 vendor 进 Git LFS。**代价**：仓库体积膨胀数百 MB，`PORTING.md:82` 描述的 `/*` 忽略机制还要再放行，且与 `.gitignore` 现状冲突。

## P1 明显缺陷

### P1-1 `always()` 保存依赖缓存会把"半成品"写进缓存并毒化后续所有构建

**证据**
- `android-compile.yml:150-157`：`- name: 保存依赖缓存`，`if: always() && steps.deps-cache.outputs.cache-hit != 'true'`，`uses: actions/cache/save@v4`，path 含 `android/deps-android/prebuilt` 与 `~/deps-work/state`。
- `android-compile.yml:135` / `:141`：`下载依赖源码` 与 `交叉编译依赖` 都是 `if: steps.deps-cache.outputs.cache-hit != 'true'` —— 命中即整体跳过。
- `android-compile.yml:343`：apk job `fail-on-cache-miss: true`，`needs: compile`（`:296`）。
- `android/deps-android/build.sh:146-181`：每个依赖成功才 `touch "$STATE/$dep.done"`；失败则 `FAILED` 累积、最后 `exit 1`（`:181`）。
- `PORTING.md:762` 自认："`always()` 保存半成品 prebuilt 可能毒化缓存"。

**影响**：若 `build.sh` 在第 N 个依赖失败（磁盘满、上游 tarball 变更、网络抖动），`always()` 仍把已完成的 prebuilt + state 存成正式 key。下一轮：cache-hit=true → `fetch.sh`/`build.sh` 全跳过 → `compile-check.sh` 因缺头文件/缺 `.a` 失败 → `compile` job 失败 → `apk` job 被 `needs` 跳过。且 key 不含 `build.sh`、不含失败状态、无 `restore-keys`，**缓存不会自愈**，只能人工去 Actions → Caches 删除。`PORTING.md:762` 把它列为"待办"，但当前代码仍未修。

**改进建议**
- **A（推荐）**：把保存条件改为 `if: success() && steps.deps-cache.outputs.cache-hit != 'true'`。代价：deps 构建失败时这一轮不写缓存，下次重跑要重编 30 分钟依赖（`android-compile.yml:147-149` 注释正是为省这 30 分钟才用 `always()`）。可用折中：只对"构建成功但后续编译失败"保存——即把 `build.sh` 的退出码写成 step output，保存条件 `if: steps.build-deps.outputs.ok == 'true' && ...`。
- **B**：给"未完成"的缓存加 `-partial-${{ github.run_id }}` 后缀，成功后再存正式 key。代价：多一份缓存、逻辑稍复杂，但无需人工干预。
- **C**：在 `build.sh` 最后生成 `prebuilt/<abi>/.complete` 标记，`compile-check.sh:38-42` 与 workflow 的 cache-hit 判断都检查它，缺标记就强制重跑 fetch/build。代价：要改 3 个文件，但最稳，且能同时兜住 P1-2 的"假成功"。

### P1-2 `build.sh:162` 的 `set -e` 在 `if` 条件里不生效，header-only 依赖可能"假成功"并打上永久 stamp

**证据**
- `android/deps-android/build.sh:162`：`if ( set -e; "$fn" ); then` —— 子 shell 是 `if` 的条件。
- 实测（Git Bash，`& 'C:\Program Files\Git\bin\bash.exe' -c 'set -uo pipefail; if ( set -e; false; echo "SUBSHELL-CONTINUED-AFTER-FALSE" ); then echo if-true; else echo if-false; fi; echo outer'`）输出：
  ```
  SUBSHELL-CONTINUED-AFTER-FALSE
  if-true
  outer
  ```
  即 `false` 后仍继续执行，且子 shell 因最后的 `echo` 成功而返回 0 → `if` 判真。bash 手册：复合命令在 `-e` 被忽略的上下文（如 `if` 条件）中执行时，其内部所有命令都不受 `-e` 影响，即使内部再 `set -e`。
- `android/deps-android/defs.sh:25-31` `build_simde` 与 `:33-38` `build_uthash` 是 header-only：`cp -r` / `cp` **没有任何 `|| return 1`**；函数最后一句是 `echo`（必成功）。
- `build.sh:169` `touch "$stamp"` 在 `if` 为真分支里执行；`:154` 后续轮次 `[ -f "$stamp" ]` 直接 `echo "已完成，跳过"`。

**影响**：`cp` 失败（源码包解压不全、权限、磁盘）时函数仍返回 0 → 打 stamp → 该依赖被永久标记完成，且 `~/deps-work/state` 会进缓存（P1-1）→ 后续所有构建都缺 `simde/`、`uthash.h` 却"跳过"该依赖，报错点被推到很远的编译阶段。这是"静默成功"类缺陷，最难排查。`_run_cmake`（`defs.sh:41-66`）、`_autoconf_dep`（`:127-138`）、`build_x264`（`:163-184`）、`build_ffmpeg`（`:187-219`）都用 `|| return 1` / `|| exit 1` 显式串联，故仅这两个 header-only 函数暴露。

**改进建议**：`build.sh:162` 改为在普通上下文里执行并显式取返回码：
```bash
( set -e; "$fn" ); rc=$?
if [ "$rc" -eq 0 ]; then ...
```
或给 `defs.sh:25-38` 的每个 `cp` 补 `|| return 1`。**代价**：前者改 1 行、语义最清晰；后者若将来新增 header-only 依赖容易再漏，建议两者都做。

### P1-3 所有脚本只有 `set -uo pipefail`，缺 `-e`；`build-apk.sh` 最终归档未检查，可在无产物时 exit 0

**证据**
- 五个脚本首部：`android/build-apk.sh:23`、`android/compile-check.sh:25`、`android/deps-android/fetch.sh:8`、`android/deps-android/build.sh:11`、`android/deps-android/defs.sh:9` 均为 `set -uo pipefail`（无 `-e`）。
- `android/build-apk.sh:200` `cp -f "$SIGNED" "$OUT"` 无 `|| die`；其后 `:204` `ls -la "$OUT"`、`:205-209` aapt2、`:212` unzip、`:223-233` 自检都不检查 `OUT` 是否存在，最后 `:235` `echo "APK: $OUT"` 且脚本自然退出 0。
- `android/build-apk.sh:119` `cp -f "$PLUG_SRC"/*.so "$PKG_ASSETS"/` 无检查；`:128` 只校验 `count -gt 0`，**部分拷贝成功（如 10 个只拷了 5 个）不会报错**。
- `android/build-apk.sh:131` `( cd "$PKG_ASSETS" && ls -1 ./*.so | sed ... ) > "$PKG_ASSETS/MANIFEST"` 无检查，MANIFEST 可能为空而不自知（后果见 P1-6）。
- `android-compile.yml:461` 上传时才用 `if-no-files-found: error` 兜底；本地用户拿不到这层保护。

**影响**：磁盘满、权限错误、路径含空格导致 `cp` 部分失败时，脚本打印一个不存在的 `APK: /.../OBS-Android-arm64-v8a-1105-1200.apk` 并以 0 退出，本地构建者误判成功；CI 里则要等到 artifact 步骤才炸，浪费一整轮 6 分钟构建。

**改进建议**：所有脚本统一 `set -euo pipefail`，并把确实允许失败的命令显式写成 `cmd || true` / `cmd || die`。重点补 `build-apk.sh:200`：
```bash
cp -f "$SIGNED" "$OUT" || die "归档 APK 失败：$OUT"
[ -s "$OUT" ] || die "APK 为空或不存在：$OUT"
```
以及 `:119` 之后校验"拷入数量 == 源数量"（`PORTING.md:331-333` 已强调不做文件名过滤，正好可在这里加数量断言）。**代价**：`set -e` 会让若干目前"靠后续检查兜住"的命令提前失败，需要逐条过一遍（约 10 处），但换来的是失败即刻暴露。对照组：`compile-check.sh:101-127` 与 `build.sh:162-176` 已经用显式 `|| exit`/`RC=$?` 手工做了这件事，说明作者意识到问题但没贯彻。

### P1-4 CI 的 `paths` 白名单漏掉本方向的核心构建文件，改动这些文件不会触发任何构建

**证据**
- `android-compile.yml:30-41`（push）与 `:43-50`（pull_request）白名单只有：`cmake/android/**`、`libobs/**`、`libobs-opengl/**`、`plugins/**`、`frontend/**`、`android/**`、`.github/workflows/android-compile.yml`。
- 本方向下列**已修改/新增**文件均不在其中：根 `CMakeLists.txt:7`、`cmake/common/osconfig.cmake:5-13`、`.gitignore:6,31-32,56-58`、`deps/glad-gles/CMakeLists.txt`（整个 `deps/**` 未列）、`.github/workflows/android-release.yml`、`.github/release-notes.md`、`CMakePresets.json`、`build-aux/**`。
- `PORTING.md:761` 自认"paths 漏了 `deps/**`"，但仍未修；`android-compile.yml:35-38` 自述曾因白名单漏 `OBSApp.cpp` 导致"静默不构建"。

**影响**：改 `osconfig.cmake`（Android 识别逻辑，一旦写错直接整条 Android 构建失败）、改根 `CMakeLists.txt`、改 `deps/glad-gles/CMakeLists.txt`（GLES 集成）、改 `android-release.yml` 或 `release-notes.md` 都不会跑 CI；改动会一直"绿"到下次有人碰白名单内文件才暴露。`PORTING.md:38` 说"静默不构建比多跑几次更坑"，现状正是这句话描述的风险。

**改进建议**
- **A（推荐）**：把白名单改成上游 `push.yaml` 的 `paths-ignore` 黑名单风格（`git show 65c320b:.github/workflows/push.yaml` 第 5-7 行 `paths-ignore: '**.md'`），即 `paths-ignore: ['**.md', 'docs/**']`，保证任何代码/构建文件改动都触发。**代价**：文档改动也会跑一次约 10 分钟的 CI（`PORTING.md:38` 估算），但可用 `concurrency.cancel-in-progress: true`（`:52-54` 已有）抵消。
- **B**：补齐白名单，至少加 `'CMakeLists.txt'`、`'cmake/**'`、`'deps/**'`、`'.gitignore'`、`'.github/workflows/**'`、`'CMakePresets.json'`、`'build-aux/**'`。**代价**：仍会漏未来新增的顶层文件（`PORTING.md:78-89` 描述的 `/*` 问题同源），需要持续维护；不推荐但比现状好。
- 二选一都建议把 `PORTING.md:761` 的待办同步销项。

### P1-5 `.gitignore` 未忽略 `deps-android/{src,prebuilt}` 与 `frontend/cmake/android/libs`，`git add -A` 会吞入数百 MB 构建产物

**证据**
- `.gitignore:2` 是 `/*`（忽略根下一切），`:6` `!/android` 放行整个 `android/` 目录；`git ls-files android` 只列出 6 个脚本，无 `prebuilt/`、`src/`。
- `.gitignore` 全文（`:1-58`）只有 `:58` 一条与 Android 产物相关的忽略规则 `/frontend/cmake/android/assets/`；`PORTING.md:763` 自认"`.gitignore` 对 `android/deps-android/{prebuilt,src}` 无规则"。
- 实测 `git check-ignore -v --no-index android/deps-android/prebuilt/arm64-v8a/lib/libx264.a android/deps-android/src/foo.tar.gz frontend/cmake/android/libs/foo.so` → **无任何输出（none of these are ignored）**，即三条路径都不被忽略。
- `android/deps-android/fetch.sh:17` `SRC="$HERE/src"`（下载 15 个 tarball，含 FFmpeg/x264/Qt 依赖，合计数百 MB）；`build.sh:71` `EXPORT="$HERE/prebuilt/$ABI"`；`android/build-apk.sh:136` 会 `rm -rf "$ROOT/frontend/cmake/android/libs"`（说明该目录构建期也会出现）。

**影响**：任何开发者按 `PORTING.md:51-54` 跑完 `fetch.sh`/`build.sh` 后执行 `git add -A`，就会把几百 MB 二进制与源码 tarball 提交进仓库；且 `PORTING.md:78-89` 专门警告过"新增顶层文件会被 `/*` 静默吞掉"，这里却是反过来的"该忽略的没忽略"，属于同一套机制的两面。`frontend/cmake/android/assets/` 已被忽略，唯独同级的 `libs/` 没有。

**改进建议**：在 `.gitignore:58` 后补：
```
/android/deps-android/src/
/android/deps-android/prebuilt/
/frontend/cmake/android/libs/
/OBS-Android-*.apk
/build-android-*/
```
（最后两条顺带忽略 `build-apk.sh:199` 的产物与 `compile-check.sh:32` 的构建目录）。**代价**：无；唯一要注意 `!/android` 放行目录后，子目录规则需以 `/` 开头精确匹配，避免误伤 `android/deps-android/defs.sh` 等已跟踪文件（已跟踪文件不受 .gitignore 影响，风险低）。

### P1-6 「打两遍」打包方案脆弱：依赖 Gradle 中间产物删除清单与 `find | head -1`，且无"插件确实进包"的验收

**证据**
- `android/build-apk.sh:138-168`：第二遍只删 `:152-156` 的 `obs-studio.apk`、`build/outputs/apk`、`intermediates/{merged_jni_libs,merged_native_libs,stripped_native_libs}`，`:157` `find "$AB/build/outputs" -name '*-signed*.apk' -delete`，然后重跑 `androiddeployqt --release`。
- `PORTING.md:233-234` 自述："删掉 `build/outputs/apk` 和 Gradle 的 native 中间件，逼 Gradle 重新执行 `packageRelease`。其余 Gradle 任务仍走缓存，所以第二遍不是全量。"——**正确性依赖"删了这几个目录就一定会重新 merge assets"这一隐含假设**；`mergeReleaseAssets` 之类的任务输出并未删除，若 Gradle 认为其 up-to-date（例如两次 androiddeployqt 之间 assets 文件 mtime 未变、`android-build/assets/` 是 androiddeployqt 自己拷贝的副本），第二遍仍可能打进**空的/旧的**插件目录。
- `android/build-apk.sh:171` `APK="$(find "$AB/build/outputs/apk" -name '*.apk' ! -name '*-signed*' 2>/dev/null | head -1)"`：第一遍（Qt 自动那次）可能留下 debug/unsigned APK，`:152` 只删了 `$AB/obs-studio.apk` 与 `build/outputs/apk`，`head -1` 取到哪个是未定义的。
- `android/build-apk.sh:131` 生成 MANIFEST 但 `:107-114` 的断言只检查 `libobs_*`/`libQt*`，**没有反向校验"MANIFEST 里的每个 .so 都确实出现在最终 APK 的 assets/ 里"**；`:211-233` 的自检只查 `lib/<abi>/` 与应用库的 `main` 符号，完全不查 `assets/obs-plugins/`。

**影响**：这是 `PORTING.md:729-733` 号称"已解决"的发版阻塞项（非 debuggable 包插件全加载失败）的回归通道——一旦第二遍没把新 assets 合进去，症状与最初完全一样（插件一个都加载不了），而 CI「不覆盖任何运行时行为」（`PORTING.md:43`、`android-compile.yml:12`）**100% 测不出来**。此外 `PORTING.md:495-503` 说第二遍约 6s，也说明作者清楚这是在赌 Gradle 增量行为。

**改进建议（按推荐度）**
- **A（根治，推荐）**：让插件在 **configure 阶段就登记为 assets**，从而只需 androiddeployqt 跑一遍。具体：给 `frontend/cmake/os-android.cmake` 的 `qt_add_executable` 设置 `QT_ANDROID_PACKAGE_SOURCE_DIR` 指向一个由 CMake 生成的目录，在该目录的 `assets/obs-plugins/` 下用 `add_custom_command`/`configure_file` 登记插件；再用 `add_dependencies(obs-studio <所有插件目标>)` 保证 `androiddeployqt` 目标排在插件产出之后（`cmake/android/helpers.cmake:43-51` 已经在给 `obs-studio` 加 `add_dependencies(obs_executables obs_modules)`，把 assets 暂存也做成 `obs-studio` 的 PRE_BUILD/依赖即可）。**代价**：需要摸清 Qt 6.11.1 `qt_finalize_executable` 的 target 顺序（`PORTING.md:323-328` 已踩过 `_qt_is_android_executable` 失效的坑），首次改造约 1-2 人日；换来的是删掉整个"打两遍"、删掉 `:152-157` 的脆弱清理、缩短一轮 Gradle、消除回归通道。
- **B（次选）**：保留两遍，但用 Gradle 原生的方式合并 assets —— 把 `frontend/cmake/android/assets/` 作为 `sourceSets.main.assets.srcDirs` 写进 Qt 生成的 `build.gradle`（需 patch 模板），第二遍只 `gradle packageRelease` 而不重跑 androiddeployqt。**代价**：同样要接管 Qt 生成物，且 Qt 升级时模板易漂移（`PORTING.md:167-169` 已警告）。
- **C（最小兜底，无论选 A/B 都该做）**：在 `:171` 之后、`:190` 之前加**验收**：
  ```bash
  unzip -Z1 "$APK" | grep -q '^assets/obs-plugins/MANIFEST$' || die "APK 缺插件 MANIFEST"
  missing=0
  while read -r m; do unzip -Z1 "$APK" | grep -qx "assets/obs-plugins/$m" || { echo "缺 $m"; missing=1; }; done < "$PKG_ASSETS/MANIFEST"
  [ "$missing" = 0 ] || die "APK 的 assets 与 MANIFEST 不一致"
  ```
  并把 `:171` 的 `find | head -1` 改成精确路径（`build/outputs/apk/release/*-unsigned.apk`）或用 `find ... -printf '%T@ %p\n' | sort -n | tail -1` 取最新。**代价**：每轮多约 1 秒解压索引，零风险，立即可落地。

### P1-7 deps / ccache 缓存 key 组成不完整：`API` 不在 key 里、`build.sh`/`fetch.sh` 不参与 hash

**证据**
- `android-compile.yml:129`、`:157`、`:342` deps key：`deps-${{ env.ABI }}-ndk${{ env.NDK_VERSION }}-${{ hashFiles('android/deps-android/versions.sh','android/deps-android/defs.sh') }}`。
- `android-compile.yml:181`、`:394`、`:453` ccache key：`ccache-${{ env.ABI }}-ndk${{ env.NDK_VERSION }}-qt${{ env.QT_VERSION }}-${{ github.sha }}`。
- `android-compile.yml:59` `API: '29'`，`android/compile-check.sh:29` `API="${API:-29}"`、`:104` `-DANDROID_PLATFORM="android-$API"`；`android/deps-android/build.sh:18` `API="${API:-29}"`。
- `android/deps-android/build.sh`、`fetch.sh` 均未出现在任何 `hashFiles` 里。
- `PORTING.md:761` 自认"缓存 key 已含 `NDK_VERSION`…但仍不含 `API`"。

**影响**：把 workflow 的 `API` 从 29 改成 26/30（或改 `build.sh` 的编译器/flag 逻辑、改 `fetch.sh` 的下载策略），key 不变 → deps 缓存命中 → `build.sh` 因 stamp 直接跳过（`build.sh:154-157`）→ 用旧 API 编出的 `.a` 配新 API 的 libobs，运行时才炸；ccache 因 `github.sha` 每轮必变，影响较小但 `API` 变化不体现在 key 的语义里也不利于排查。`x264`/`rnnoise` 的 `master` 内容变化同样不触发（已归入 P0-3）。

**改进建议**：deps key 加 `api${{ env.API }}` 并把 `build.sh`、`fetch.sh`、`versions.sh` 一起纳入 `hashFiles`（`:129`、`:157`、`:342` 三处必须逐字一致，否则 `:343` 的 `fail-on-cache-miss` 会失效）；ccache key 加 `api${{ env.API }}`（`:181`、`:394`、`:453` 三处同步）。**代价**：改 `API` 或构建脚本会让整份 deps/ccache 缓存失效、重建约 30 分钟；这正是期望行为，且 `PORTING.md:531-538` 已论证 ccache "改 flags 自动失效，只会变慢不会变错"的设计哲学，此处应保持一致。替代方案（不推荐）：在 `build.sh` 里把 `API`/`CC` 写进 stamp 内容做自失效——代价是要在每个依赖的 stamp 里编码输入，比改 key 复杂且容易漏。

## P2 可维护性

### P2-1 `osconfig.cmake` 的 Android 判定依赖工具链文件名，且注释与代码不符

**证据**：`cmake/common/osconfig.cmake:5` `if(CMAKE_SYSTEM_NAME STREQUAL "Android" OR CMAKE_TOOLCHAIN_FILE MATCHES "[\\/]android[.]toolchain[.]cmake$")`；`:8` 注释"故同时用 NDK 工具链文件路径 / **ANDROID_ABI 缓存项兜底识别**"，但条件里**没有 `ANDROID_ABI`**。`cmake/common/bootstrap.cmake:62` 在 `project()`（`CMakeLists.txt:5`）之前 include osconfig，`CMAKE_SYSTEM_NAME` 此时确实未设置（`:7` 注释自述）；`-DCMAKE_TOOLCHAIN_FILE` 是唯一实际生效的判据（`android/compile-check.sh:102`）。

**影响**：用自定义工具链文件（例如自己写 `my-android.cmake` 里 `include()` NDK 工具链）、或 CMake 未来改变工具链文件名时，判定失效 → 落到 `osconfig.cmake:23` 的 Linux 分支 → `cmake/linux` 模块 + `OS_LINUX TRUE` → 整条 Android 构建以 Linux 方式失败，报错点远离根因。注释里那个不存在的 `ANDROID_ABI` 兜底会误导排查者。

**改进建议**：条件改为 `if(CMAKE_SYSTEM_NAME STREQUAL "Android" OR ANDROID_ABI OR CMAKE_TOOLCHAIN_FILE MATCHES ...)`（`ANDROID_ABI` 由 `compile-check.sh:103` 显式传入，稳定），并删掉/修正 `:8` 的注释。**代价**：极小；唯一风险是某个非 Android 构建误设了 `ANDROID_ABI` 缓存变量（几乎不会发生）。更稳的替代是显式传 `-DOBS_TARGET_PLATFORM=Android` 并只认这个变量——代价是 `compile-check.sh` 与所有文档调用处都要改，收益是彻底摆脱启发式。

### P2-2 `--clean` 不清理 in-source 构建产物

**证据**：`android/deps-android/build.sh:90-102` `_extract` 在源目录已存在且非空时 `return 0`（不重解压）；`:76-86` `--clean` 只影响 `:154` 的 stamp 判断，不清源目录；`defs.sh:127-138`（`_autoconf_dep`）、`:163-184`（`build_x264`）、`:187-219`（`build_ffmpeg`）都在源目录内 `./configure && make`（in-source），只有 `_run_cmake`（`:46`）做了 `rm -rf "$s/build"`。

**影响**：`./build.sh arm64-v8a core --clean`（`build.sh:8` 明写的用法）不会清掉 autotools 系依赖的陈旧 `.o`，换编译选项/换 NDK 后可能静默复用旧目标文件；`_audit_arch`（`build.sh:132-144`）只能查出跨 ABI 的架构错配，查不出同 ABI 的陈旧对象。`PORTING.md:130-131` 的注释正好描述这个坑，但只解决了跨 ABI 那一半。

**改进建议**：`--clean` 时对每个 dep 执行 `rm -rf "$(src_dir "$dep")"` 再 `_extract`，或对 autotools 依赖在 `./configure` 前 `make distclean 2>/dev/null || true`。**代价**：`--clean` 变慢（要重新解压 15 个包，约 1-2 分钟），但语义才符合"强制重做"。折中：只清 in-source 的 7 个 autotools 依赖。

### P2-3 `_relocate_cmake_package` 用未转义的路径做 `grep`/`sed` 正则

**证据**：`android/deps-android/build.sh:121-127`：`grep -rl -- "$PREFIX" ...`、`sed -i "s|$PREFIX|\${_IMPORT_PREFIX}|g" "$f"`。`PREFIX="$WORK/prefix/$ABI"`（`:69`），`WORK="${WORK:-$HOME/deps-work}"`（`:20`），典型值 `/home/bbpcs/deps-work/prefix/arm64-v8a`——含 `.`（`deps-work` 无点，但 `$HOME` 或自定义 `WORK` 可能含点，如 `/home/user.name/...`），而 `grep`/`sed` 未加 `-F`/未转义，`.` 会被当通配符；路径里若含 `|` 还会破坏 sed 分隔符。

**影响**：极端情况下误替换无关文件、或替换不到目标，导致 Windows 侧 configure 报 `missing and no known rule to make it`（`build.sh:112-117` 注释描述的正是这个故障）。

**改进建议**：`grep -rlF -- "$PREFIX"`，`sed` 用 `|` 之外的分隔符并对 `$PREFIX` 做正则转义（`printf '%s' "$PREFIX" | sed 's/[.[\*^$(){}?+|/]/\\&/g'`），或改用 `cmake -E` / Python 做纯字符串替换。**代价**：多几行代码，零风险。

### P2-4 `build-apk.sh` 若干未检查命令与"就地 apksigner"

**证据**：`android/build-apk.sh:119` `cp` 无检查（见 P1-3）；`:171` `find | head -1` 取任意 APK（见 P1-6）；`:191-193` `cp -f "$APK" "$SIGNED"` 后把**同一个路径**同时作为 `apksigner sign` 的输入与 `--out`：
```bash
SIGNED="${APK%.apk}-signed.apk"
cp -f "$APK" "$SIGNED"
"$APKSIGNER" sign ... --out "$SIGNED" "$SIGNED"
```
apksigner 对"输入 == 输出"没有明确支持保证（各 build-tools 版本行为可能不同），且 `:186`/`:202` 用 `ls "$SDK"/build-tools/*/apksigner | sort -V | tail -1` 取"最新"未钉版本（`PORTING.md:765` 自认）。

**影响**：换 build-tools 版本后可能签名失败或写出损坏 APK；`cp` 若失败，`apksigner` 会因输入不存在而 `die`（这处反而被兜住），但 `apksigner` 成功写出后 `:195` `verify` 才验证，中间无原子性。

**改进建议**：`apksigner sign --out "$OUT_SIGNED" "$APK"`（输入用未签名的 `$APK`，输出用独立临时文件），成功后 `mv -f` 到 `$SIGNED`；`APKSIGNER`/`AAPT2` 钉到 `build-tools/<版本>` 常量（runner 镜像里取 `ls` 后写死或由 env `BUILD_TOOLS_VERSION` 传入）。**代价**：极小；代价主要在"换 runner 镜像时要同步改版本号"，但 `PORTING.md:19` 已把"未钉版本"列为已知状态，钉死反而更可控。

### P2-5 版本号散落 4 处，`PORTING.md` 只说"两处一起改"

**证据**：`PORTING.md:643-653` 说"两处一起改"，只列 `frontend/cmake/os-android.cmake` 与 `frontend/cmake/android/AndroidManifest.xml`。实际还有：
- `android/compile-check.sh:63` `OBS_VERSION_OVERRIDE="${OBS_VERSION_OVERRIDE:-33.0.1}"`
- `android-compile.yml:63` `OBS_VERSION_OVERRIDE: '33.0.1'`
- `frontend/cmake/os-android.cmake:189-190` `QT_ANDROID_VERSION_NAME "33.0.1"` / `QT_ANDROID_VERSION_CODE 330001`（实测 `Select-String` 输出）
- `frontend/cmake/android/AndroidManifest.xml:54-55` `android:versionCode="330001"` / `android:versionName="33.0.1"`

**影响**：升级上游基线时漏改任一处，会出现 `versionCode` 不单调（`PORTING.md:651-653` 警告 `INSTALL_FAILED_VERSION_DOWNGRADE`）或 `OBS_VERSION_OVERRIDE` 与实际 APK 版本不一致。

**改进建议**：在 `android/deps-android/versions.sh` 或新建 `android/version.sh` 里定义单一 `OBS_VERSION=33.0.1` / `OBS_VERSION_CODE=330001`，由 `compile-check.sh`/workflow/`os-android.cmake` 读取；`AndroidManifest.xml` 的硬编码至少加一条 CI 断言（`grep -q "versionCode=\"$CODE\""`）验证与 `os-android.cmake` 一致。**代价**：CMake 读 shell 变量较别扭，可用 `configure_file` 从 `.in` 模板生成 manifest——改造量约半天；最小可行方案是先在 `android-compile.yml` 加一个校验 step（约 5 行）。

### P2-6 `build-apk.sh:38` 注释与实际 `QT_ANDROID_COMPILE_SDK_VERSION` 矛盾

**证据**：`android/build-apk.sh:38` "（我们的 `QT_ANDROID_COMPILE_SDK_VERSION 35` 就写在那里）"；实际 `frontend/cmake/os-android.cmake:203` 是 `QT_ANDROID_COMPILE_SDK_VERSION 36`（`Select-String` 输出），`build-apk.sh:44-46` 与 `android-compile.yml:327-329` 也都说 36。

**影响**：误导维护者以为 compileSdk 是 35，可能据此回退 `:47` 的 `ANDROID_PLATFORM` 默认值，触发 `:checkReleaseAarMetadata` 失败（`PORTING.md:179-185`）。

**改进建议**：把 `:38` 的 `35` 改成 `36` 并注明"以 `frontend/cmake/os-android.cmake:203` 为准"。**代价**：一行。

### P2-7 `deps/glad-gles` 的许可证与可再生产性缺口

**证据**
- `deps/glad-gles/src/gles2.c:1-3` 与 `include/glad/gles2.h:1-4` 带 `SPDX-License-Identifier: (WTFPL OR CC0-1.0) AND Apache-2.0`，**生成代码带许可证头，这一半合规**（Apache-2.0 与 OBS 的 GPLv2-or-later 在 GPLv3 选项下兼容）。
- 但 `deps/glad-gles/include/glad/glad.h`（24 行，手写 shim，**非 glad 生成物**，开头是中文注释 `/* Android(GLES) 后端的 glad 入口…` 直到 `:24` 的 `#endif`）**无 SPDX / 无版权 / 无许可证声明**。
- `deps/glad-gles/CMakeLists.txt:3` 与 `libobs-opengl/CMakeLists.txt:6` 都引用 `.qoder/gen-glad-gles.sh` 作为生成脚本；实测 `git ls-files .qoder` 为空、`Test-Path .qoder` 为 `False`（目录不在仓库中），`git ls-files deps/glad-gles` 只有 4 个文件（CMakeLists + 3 个源码/头）。

**影响**：新增的手写头文件权属/许可不明；生成脚本未入库 → 无法从仓库再生成 `gles2.c`（2122 行）/ `gles2.h`，升级 glad2 或改扩展集时只能靠某台机器上的 `.qoder/`，构建产物不可重现（`PORTING.md:764` 同类问题）。

**改进建议**：给 `glad.h` 加文件头（版权 + `SPDX-License-Identifier: GPL-2.0-or-later`，与仓库一致）；把 `gen-glad-gles.sh` 移入 `deps/glad-gles/` 并纳入版本库，或在 `CMakeLists.txt:3` 注明"生成命令 + glad2 版本 + 完整 `glad` CLI 参数"使可手动复现。**代价**：几乎为零；`gles2.h:25` 已含完整 glad URL 参数（实测 `Select-String` 输出 `http://glad.sh/#api=gles2%3D3.1&extensions=...`），补齐一条命令即可。

### P2-8 CI 供应链与最小权限：action 未按 SHA 钉、release 授予编译 job 写权限

**证据**
- `android-compile.yml` 全部用版本标签：`actions/checkout@v4`（`:78`、`:303`）、`actions/cache/restore@v4`（`:119`、`:178`、`:337`、`:391`）、`actions/cache@v4`（`:347`、`:374`）、`actions/cache/save@v4`（`:152`、`:450`）、`actions/upload-artifact@v4`（`:244`、`:258`、`:457`、`:488`）；`android-release.yml:29,31` 用 `actions/checkout@v4`/`download-artifact@v4`。
- 上游对照：`git show 65c320b:.github/workflows/push.yaml` 里是 `actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd # v6.0.2`（SHA 钉 + 注释），且每个 `uses:` job 都带 job 级 `permissions: contents: read`。
- `android-release.yml:16-17` 顶层 `permissions: contents: write`；`:22` `uses: ./.github/workflows/android-compile.yml` 未声明 job 级 `permissions`，被调用 workflow 会继承调用方的 `contents: write`。而 `android-compile.yml` 自身**没有 `permissions:` 块**。

**影响**：编译 job（会执行 `sdkmanager`、`pip install aqtinstall`、`curl` 下载第三方 tarball、`bash build.sh`）持有 `contents: write`，一旦任一第三方依赖被投毒，攻击者可直接改仓库；action 用可变 tag 而非 SHA 也扩大了供应链面。

**改进建议**：`android-release.yml:22` 的 `build` job 加 `permissions: contents: read`，只给 `:24` 的 `release` job 保留 `contents: write`（`gh release create` 需要）；所有 `actions/*` 逐步改为 `<sha> # vX.Y.Z` 形式（可配合 Dependabot）。**代价**：SHA 钉需要一次性核对约 10 个 action 的官方 release SHA，之后由 Dependabot 自动提 PR；无功能风险。

### P2-9 `.github/release-notes.md:31` 的 "Android 8.0（API 29）" 事实错误

**证据**：`.github/release-notes.md:31` "**arm64-v8a**，Android 8.0（API 29）及以上"；实际 **API 29 = Android 10**，Android 8.0 是 API 26/27。仓库内一致证据：`frontend/cmake/os-android.cmake:216` `QT_ANDROID_MIN_SDK_VERSION 29`、`libobs/cmake/os-android.cmake:30-31` "以 android-29 为目标（minSdk 29）"、`PORTING.md:20` "targetSdk / minSdk | **35** / **29**"、`frontend/cmake/android/res/drawable/obs_keepalive.xml:5` "minSdk 29（≥21 原生支持 vector）"。

**影响**：面向用户的安装说明给出错误的最低系统版本，Android 8/9 用户装上后会失败；也会让"pre-release 已知问题"清单的可信度打折。

**改进建议**：改为 "**Android 10（API 29）及以上**"。**代价**：一行。

### P2-10 `PORTING.md` 中 `plugins/CMakeLists.txt` 的行号已过时

**证据**：`PORTING.md:107` "Android 白名单在 `:158` 就 `return()`，而上游在文件末尾（`:257`）才调用 `set_obs_core_modules()`"。实测：
- 当前 `plugins/CMakeLists.txt`：`return()` 在 **`:180`**，`:158` 是 `add_subdirectory(obs-libfdk)`；`set_obs_core_modules()` 在 **`:279`**（上游末尾）与 **`:179`**（Android 分支补偿调用）。
- 上游 `git show 65c320b:plugins/CMakeLists.txt` 共 **120 行**，`set_obs_core_modules()` 在 `:18` 与 `:120`（实测 `total lines = 120`、`line 18`、`line 120`）——**不存在 `:257`**。

**影响**：`PORTING.md:76-118` 是"改代码前必读"，行号错误会让排查者在当前文件里找不到对应位置，削弱文档价值。机制描述本身正确、修法也已落地（`:179`），只是行号停留在早期版本。

**改进建议**：把 `:158`/`:257` 更新为 `:180`/`:279`，并注明"行号随白名单增删会漂移，请以 `return()` / `set_obs_core_modules()` 符号定位"。**代价**：两处文字。

### P2-11 `PORTING.md:636-639` 对"为什么 release 单独一个 workflow"的机制解释不准确

**证据**：`PORTING.md:636-639` 归因于 "paths 对 tag push 的匹配规则容易出意外 —— tag 指向的提交若没改到白名单里的路径，整条流水线根本不会启动"。但 `android-compile.yml:23-41` 的 `on.push` **同时**定义了 `branches: [main, 'test/**']`（`:27-29`）与 `paths`（`:30-41`）。tag ref（`refs/tags/v*`）**永远不匹配 `branches` 过滤器**，这才是 tag push 不会启动该 workflow 的直接原因；且据 [GitHub Actions 工作流语法文档](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax)，path filters 对 tag push 本就不评估。`android-release.yml:6-9` 自己也只说"paths 过滤…容易出意外"。

**影响**：结论（需要独立 release workflow）正确，且 `android-release.yml:11-14` 用 `on.push.tags: ['v*']` 确实绕开了；但机制解释错误，未来若有人给 `android-compile.yml` 的 `on.push` 加了 `tags:` 而忘记删 `branches:`，会再次踩坑。

**改进建议**：把 `PORTING.md:636-639` 与 `android-release.yml:6-9` 的措辞改为"因为 `on.push.branches` 不匹配 tag ref，且 `paths` 对 tag push 不评估"。**代价**：两处文字。

### P2-12 发布流程的健壮性细节

**证据**
- `android-release.yml` 无 `concurrency` 块；`:48` `gh release create "${GITHUB_REF_NAME}"` 无 `--verify-tag`、无"release 已存在"处理，重跑（`workflow_dispatch` 不支持，但 tag 删了重推会）会失败。
- `:41` `sha256sum ./*.apk | tee SHA256SUMS.txt` 只校验文件名不校验来源（artifact 来自 `actions/download-artifact`，本身可信）。
- `android/build-apk.sh:198-199` `STAMP="$(date '+%m%d-%H%M')"`、`OUT="$ROOT/OBS-Android-$ABI-$STAMP.apk"`：无年份、无秒、无 git SHA；同一分钟两次构建互相覆盖，产物无法与提交对应。`android-compile.yml:460` 用 `OBS-Android-*.apk` 通配上传，本地残留的旧 APK 会被一起带上。
- `.github/release-notes.md` 是静态文案（`:1-39`），与 `PORTING.md:725-780` 的"待办/已知缺陷"没有同步机制（例如 `:38-39` 列了 2 条已知问题，而 `PORTING.md:734-747` 列了 6+ 条）。

**改进建议**：文件名加 `${GITHUB_SHA::8}` 与秒，上传前先 `rm -f OBS-Android-*.apk` 或精确指定；release job 加 `concurrency: group: release-${{ github.ref }}` 与 `gh release view ... || gh release create` 幂等处理；把 release notes 的生成改为从 `PORTING.md` 的"已知缺陷"章节抽取（或至少加一条 CI 检查提示人工同步）。**代价**：约半天脚本工作；收益是产物可追溯、重跑安全。

---

# 实现亮点

1. **CMake 分层严格对齐上游**：`cmake/android/{compilerconfig,defaults,helpers}.cmake` 与 `cmake/linux/` 三件套同名同职责（`git ls-tree --name-only 65c320b:cmake/linux` 对照），靠 `osconfig.cmake:11` 追加 `CMAKE_MODULE_PATH` 让根 `CMakeLists.txt:14-16` 的 `include()` 自动落到 Android 版，**零改动接入**。同理 `libobs/cmake/os-android.cmake`、`frontend/cmake/os-android.cmake` 沿用上游 `os-*.cmake` 命名，`libobs/CMakeLists.txt:268-269` 用标准 `elseif(OS_ANDROID)` 接入。这是整个移植里最"上游友好"的部分。

2. **glad-gles 的双目录同名 target 技巧**：`deps/glad-gles/CMakeLists.txt:5-9,16-17` 与上游 `deps/glad/CMakeLists.txt` 保持 `obsglad` / `OBS::glad` 一致，`libobs-opengl/CMakeLists.txt` 只用一层 `if(OS_ANDROID)` 切换 `add_subdirectory`，下游 `target_link_libraries(... OBS::glad)` 完全不用加分支（`:6-9` 注释说清了理由）。生成文件带 SPDX（`gles2.c:1-3`、`gles2.h:1-4`）。

3. **helpers.cmake 的目标身份兜底 + build-apk.sh 的二次断言**：`cmake/android/helpers.cmake:40-51` 在 Qt 6.11.1 的 `_qt_is_android_executable` 失效后，补了 `OR target STREQUAL obs-studio` 这个不随 Qt 升级漂移的判据；`android/build-apk.sh:107-114` 又在打包前断言 `lib/obs-plugins/` 里没有 `libobs_*`/`libQt*`，把"静默 APK 白胖 99 MB"变成响亮失败。两层防御 + 详尽根因注释，`PORTING.md:323-333` 有完整复盘。

4. **`build.sh` 的架构审计与 CMake 包重定位**：`:132-144` `_audit_arch` 对每个 `.a` 跑 `readelf -h` 比对 `ARCH_EXPECT`，直接堵住"别的 ABI 的陈旧 .o 混进包"；`:104-128` `_relocate_cmake_package` 把 `*Targets.cmake` 里 WSL 绝对路径换成 `${_IMPORT_PREFIX}`，解决了 Windows 侧 configure 的 `missing and no known rule to make it`（`:112-117` 注释有实测证据）。这两处是真正踩过坑才写得出来的工程细节。

5. **ccache 设计与文档化**：`android-compile.yml:177-201,390-453` 用 `restore`/`save` 分离、`key` 带 `github.sha` + `restore-keys` 前缀、4 项关键配置（`COMPILERCHECK=content`/`BASEDIR`/`NOHASHDIR`/`COMPRESS`/`MAXSIZE`），并针对"两个 job 同 key 抢保存"做了**单一写入方（apk job）**的设计；`:212-217`、`:439-444` 用 `ccache -s` 把命中率打出来，防止"配错但不报错只是变慢"。`PORTING.md:493-616` 把"试过并放弃 build 目录缓存""试过回退 Release"的取舍、实测数据、失败日志都写全了，取舍论证质量很高。

6. **`OBS_VERSION_OVERRIDE` 的绕过**：`android/compile-check.sh:53-63` 精确对应 `cmake/common/versionconfig.cmake:9` 的条件（`git show 65c320b:...` 与当前 `versionconfig.cmake:9` 逐字一致），配合 `android-compile.yml:63` 的 workflow env，解决浅克隆 `git describe` 返回裸 SHA 导致 `list(GET _obs_version_canonical 1 ...)` 越界（`versionconfig.cmake:48-50`）的问题。

7. **失败诊断信息充分**：`android-compile.yml:255-265`、`:464-499` 在失败时上传 `compile.log`/`apk.log`、CMake 日志、deployment settings、`build.gradle`、`outputs/logs/*.txt`，并打印 SDK platforms / `compileSdk|targetSdk|minSdk` grep 结果；`:80-84`、`:159-165`、`:267-269` 保留 `df -h` 观测。对无人值守 CI 很实用。

8. **Release workflow 复用干净**：`android-release.yml:22` 用 `workflow_call` 复用 `android-compile.yml`，构建步骤只写一份（`:21` 注释明确），`:36-41` 生成 `SHA256SUMS.txt`，`:43-53` 用 `GH_TOKEN` + `--prerelease` + `--notes-file`，无重复逻辑。

9. **打包不变式写进代码**：`build-apk.sh:90-97`（先清空再拷）、`:116-127`（补 `llvm-strip`，因为 assets 不经 Gradle strip）、`:145-151`（不删 `android-build`）、`:99-114`（不做文件名过滤、只做不变式断言）——`PORTING.md:236-251`、`:329-333` 的解释与代码一一对应，都是难以从零想到的细节。

10. **`compile-check.sh:121-127`**：`-- -k 0` 一次报完所有编译错误 + 显式 `RC=$?` / `exit $RC`，在 CI 里比默认快速失败更有用；`:101-119` 的 CMake 参数集中且都带注释。

---

# PORTING.md 声明核验表

> 判定：**属实** / **存疑**（静态仓库无法独立核实或表述部分成立）/ **有误**。只列与本方向相关的声明。

| # | PORTING.md 位置与声明 | 判定 | 证据 |
|---|---|---|---|
| 1 | `:17` NDK 30.0.16248370，"ubuntu-24.04 runner 自带（`ANDROID_NDK_LATEST_HOME`），无需安装" | **存疑 / 部分有误** | workflow 显式 `sdkmanager --install "ndk;$NDK_VERSION"`（`android-compile.yml:86-115,308-320`），只是"已装就跳过"（`:99-100`、`:314`）；`ANDROID_NDK_LATEST_HOME` 全仓库未出现（`grep` 无匹配）。准确说法是"runner 若自带则复用，否则安装"。 |
| 2 | `:18` FFmpeg 8.0.3，OBS 33.0 要求 ≥8.0 | **属实** | `versions.sh:26` `ffmpeg) echo 8.0.3`；`frontend/CMakeLists.txt:12` `find_package(FFmpeg 8.0 REQUIRED ...)`（上游原版，`git show 65c320b:frontend/CMakeLists.txt` 一致）。 |
| 3 | `:19` build-tools 用 runner 最新版、未钉 | **属实** | `build-apk.sh:186,202` `ls "$SDK"/build-tools/*/... \| sort -V \| tail -1`。 |
| 4 | `:20` targetSdk / minSdk = 35 / 29 | **属实** | `frontend/cmake/os-android.cmake:203`（compile 36）、`:215`（target 35）、`:216`（min 29）；`libobs/cmake/os-android.cmake:30-31`。 |
| 5 | `:22` Qt 6.11.1，"与上游一致" | **存疑** | workflow 确用 6.11.1（`android-compile.yml:75,299`）；但"obs-deps 批次 2026-10-23 → Qt 6.11.1"的映射来自外部仓库，本仓库无法独立核实（`git diff -- build-aux CMakePresets.json` 为空）。 |
| 6 | `:28-35` CI 两个 job 的覆盖范围 | **属实** | `android-compile.yml:66-253`（compile）、`:291-503`（apk）；`:10` 注释 `-DENABLE_FRONTEND=OFF` 实际由 `compile-check.sh:73` 默认值实现（未显式传 `-D`），结果一致。 |
| 7 | `:32` 15 个第三方依赖 | **属实** | `versions.sh:77-82`：core 6 + usb 3 + filters 3 + output 3 = 15。 |
| 8 | `:34` `android-native-libs` artifact 含 `.so` 清单 + `libobs.so` 的 `main` 符号 | **属实** | `android-compile.yml:228-239`（生成清单 + `llvm-readelf --dyn-syms ... \| grep -w main`）、`:242-253`（上传）。 |
| 9 | `:39` Qt 存 cache，key 含版本号 | **属实** | `android-compile.yml:345-350` `key: qt-${{ env.QT_VERSION }}-android-arm64`。 |
| 10 | `:41` 产出 `OBS-Android-apk` artifact | **属实** | `android-compile.yml:455-462`。 |
| 11 | `:43` 不覆盖任何运行时行为 | **属实** | 两个 workflow 全文无 emulator / instrumentation test / 运行时断言；`android-compile.yml:12` 明确声明。 |
| 12 | `:45` 缓存命中时 `fetch.sh` 与 `build.sh` 整体跳过 | **属实** | `android-compile.yml:135`、`:141` 均为 `if: steps.deps-cache.outputs.cache-hit != 'true'`。 |
| 13 | `:78-89` `.gitignore` 第 2 行 `/*` 会静默吞新文件 | **属实** | `.gitignore:2` = `/*`；`git show 65c320b:.gitignore` 一致（上游原规则）；`:6` `!/android`、`:32` `!/PORTING.md` 靠显式放行。**但反向问题（该忽略的没忽略）见 P1-5。** |
| 14 | `:91-103` `versionconfig.cmake:9` 条件、`OBS_VERSION_OVERRIDE` 修法 | **属实（一处小出入）** | `cmake/common/versionconfig.cmake:9` 与引用逐字一致；`git describe --always --tags`（`:11`）浅克隆返回裸 SHA → `:24` 正则不匹配 → `:49` `list(GET ... 1 ...)` 越界。失败点实际在 `versionconfig.cmake:49` 的 `list(GET)`，**早于** `project()`（`CMakeLists.txt:5`），文档说"`project()` 收到非法 VERSION 直接失败"略不准。`android-compile.yml:63`、`:99-101`、`compile-check.sh:63` 均设了 override。 |
| 15 | `:105-115` plugins 提前 `return()` 跳过 `set_obs_core_modules()`，修法已补 | **机制属实，行号有误** | 当前 `plugins/CMakeLists.txt:180` `return()`、`:179` 已补 `set_obs_core_modules()`、`:279` 是上游末尾调用；上游 `git show 65c320b:plugins/CMakeLists.txt` 共 120 行，`set_obs_core_modules()` 在 `:18`/`:120`，**无 `:257`**；`:158` 现为 `add_subdirectory(obs-libfdk)`。见 P2-10。 |
| 16 | `:206-211` `build-apk.sh` 给 androiddeployqt 传 `--android-platform android-36` | **属实** | `build-apk.sh:47` `ANDROID_PLATFORM="${ANDROID_PLATFORM:-android-36}"`、`:167` `--android-platform "$ANDROID_PLATFORM"`；`android-compile.yml:322-329` 同述。**但 `:38` 注释仍写 35，与 `:203` 的 36 矛盾（P2-6）。** |
| 17 | `:217-234` APK 是"打两遍"、第一遍不含插件、第二遍删产物强制重打包 | **属实（但方案脆弱）** | `build-apk.sh:70-79`（第一遍）、`:84-136`（插件暂存）、`:138-168`（清产物 + 第二遍）；`PORTING.md:222-227` 的 4 步与代码一一对应。风险见 P1-6。 |
| 18 | `:238-244` assets 必须"先清空再拷" | **属实** | `build-apk.sh:96-97` `rm -rf "$PKG_ASSETS"` + `mkdir -p`。 |
| 19 | `:246-250` 绝不能删 `android-build` 目录 | **属实** | `build-apk.sh:145-151` 注释 + `:152-157` 只删产物不删 `android-build`。 |
| 20 | `:329-330` assets 不经 Gradle strip，补 `llvm-strip` | **属实** | `build-apk.sh:116-127`（`llvm-strip --strip-unneeded`）。 |
| 21 | `:331-333` 不做文件名过滤、改用断言 | **属实** | `build-apk.sh:99-114`（只断言 `libobs_*`/`libQt*`，不过滤）。 |
| 22 | `:495-503`、`:520-525`、`:574-579` 构建耗时/命中率/体积实测表 | **存疑** | 静态仓库无法核验；需 CI run 日志与 `ccache -s` 截图。文中"6.2 分钟 / 2.6 分钟 / 48.67% / 44.1 MB"均属运行时数据。`android-compile.yml:212-217,439-444` 确实会打印命中率，可据此复核。 |
| 23 | `:509-511` 缓存 `~/.gradle/wrapper` + `~/.gradle/caches`，`gradle-9.3.1-bin.zip` | **部分属实 / 自相矛盾** | 缓存路径属实（`android-compile.yml:373-381`）；但 `android-compile.yml:425` 注释写 "Gradle **8.14** 不支持 JDK 21+"，与 9.3.1 不一致，二者至少其一过时。 |
| 24 | `:513-538` ccache 四项配置 + `ccache -s` | **属实** | `android-compile.yml:187-201,402-416`（`COMPILERCHECK=content`/`BASEDIR`/`NOHASHDIR`/`COMPRESS`/`MAXSIZE`）、`:212-217,439-444`（`ccache -s`）。 |
| 25 | `:540-541` ccache 用 `restore`/`save` 分开、key 带 `github.sha` | **属实（限 ccache）** | `android-compile.yml:177-183,390-396`（restore）、`:448-453`（save）。Qt 与 Gradle 缓存仍用单件 `actions/cache@v4`（`:347`、`:374`），表述未区分，易误读。 |
| 26 | `:590-616` 两个 job 共用 ccache、**只有 apk job 保存** | **属实** | `android-compile.yml:177-183`（compile 只 restore）、`:210-211` 注释"故意不保存"、`:448-453`（apk 唯一 save，`if: always()`）。 |
| 27 | `:631-635` `workflow_call` 复用 + `SHA256SUMS.txt` + `gh release create --prerelease` | **属实** | `android-release.yml:22`、`:36-41`、`:43-53`。 |
| 28 | `:636-639` release 独立文件是因为 "paths 对 tag push 匹配规则容易出意外" | **有误（结论对、机制错）** | `android-compile.yml:27-29` 的 `on.push.branches: [main,'test/**']` 才是 tag push 不触发的直接原因；paths 对 tag push 不评估（[GitHub 工作流语法文档](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax)）。见 P2-11。 |
| 29 | `:643-653` 版本号"两处一起改" | **不完整** | 实测另有 `compile-check.sh:63` 与 `android-compile.yml:63` 的 `OBS_VERSION_OVERRIDE`，共 4 处；`PORTING.md:103` 自己也要求同步改。见 P2-5。 |
| 30 | `:655-662` 每次构建新生成 debug keystore，正式版必须换固定密钥 | **基本属实** | CI runner 全新 → 每次新建（`build-apk.sh:174-183`、`android-compile.yml:303`）；本地同一 `$BUILD` 目录会复用（`:177` `[ ! -f "$KS" ]`），且 `KEYSTORE` 可覆盖（`:174`）。**但 `KEYSTORE` 逃生舱被硬编码别名/密码废掉（P0-1），文档低估了修复难度。** |
| 31 | `:664-668` debuggable 必须关，默认 false | **属实** | `frontend/cmake/android/AndroidManifest.xml` 全文无 `android:debuggable`（`Select-String` 无匹配）；`PORTING.md:745-746` 记录已在 `58cc365b` 去掉。 |
| 32 | `:672-691` 代码风格硬性规则（tab/8/120/2 空格/CMake） | **属实（与本方向 CMake 一致）** | `.editorconfig:9-14`（tab/8）、`:21-31`（CMake 2 空格）、`.clang-format:46`（`ColumnLimit: 120`）、`:103,106`（TabWidth 8 / UseTab）；`cmake/android/*.cmake` 与 `deps/glad-gles/CMakeLists.txt` 实测为 2 空格缩进，符合。CODESTYLE `:410` "Always use double quotes for strings" 在 `cmake/android/helpers.cmake`（如 `:25` `set_property(TARGET ${target} ...)` 未引号）有少量未遵守，属其他方向风格范畴。 |
| 33 | `:761-765` 工程债清单（paths 漏 `deps/**`、key 不含 `API`、`always()` 毒化缓存、`.gitignore` 无规则、`master` 无 checksum、每次新建 keystore、`sort -V`） | **属实，且全部仍未修** | 逐条复核：`android-compile.yml:30-41`（漏 `deps/**`）、`:129,181,394`（无 `API`）、`:150-151`（`always()`）、`.gitignore:1-58` + `git check-ignore` 输出为空、`versions.sh:25,31` + `fetch.sh:54-58`（无校验）、`build-apk.sh:174-183`、`:186,202`（`sort -V`）。作者自认的 7 条工程债与本报告 P0-3 / P1-1 / P1-4 / P1-5 / P1-7 / P2-4 一一对应。 |

---

# 跨范围观察（不属于本方向，但值得 lead 注意）

1. **`frontend/CMakeLists.txt` 的 Android 段引用了 `:105` 与 `:159` 等行号**，`PORTING.md:327-328` 说 `os-android.cmake` 在 `frontend/CMakeLists.txt:105` 读 `OBS_MODULES_ENABLED` 生成头文件、`obs-studio` 在 `:159` 才进 helpers。`git diff` 显示该文件有 4 个 hunk（`@@ -28,8 +28,29 @@`、`@@ -80,6 +101,8 @@`、`@@ -100,6 +123,14 @@`、`@@ -113,4 +144,16 @@`），行号已整体后移约 +21~+31，文档中的 `:105`/`:159` 很可能同样过时（同 P2-10）。建议 frontend 审查员复核。

2. **`plugins/obs-libfdk/CMakeLists.txt` 的 `LINKER_LANGUAGE CXX`**（`if(OS_ANDROID) set_target_properties(... LINKER_LANGUAGE CXX)`）是为静态链 `libfdk-aac.a`（C++ 实现）打的补丁，`plugins/CMakeLists.txt:155-157` 有解释。该 CMakeLists 通过 pwsh 读取时中文注释出现 mojibake（`Android锛歠dk-aac...`）——**可能只是 PowerShell 控制台 GBK/UTF-8 显示问题，但建议 frontend/plugins 审查员用 `read` 工具确认文件本身是 UTF-8**，因为 `.editorconfig:12` 要求 `charset = utf-8`。

3. **`frontend/cmake/os-android.cmake:189-190` 的 `QT_ANDROID_VERSION_NAME/CODE` 与 `AndroidManifest.xml:54-55` 的 `versionName/versionCode` 目前一致（33.0.1 / 330001）**，但 `PORTING.md:651-653` 强调 `versionCode` 必须单调递增。若 P0-1 的签名问题不修，`versionCode` 再单调也没用（签名不匹配仍拒绝安装）——两个方向的修复应一并排期。

4. **P0-2 的 fdk-aac 合规问题需要 lead 决策**：它横跨"依赖"（`versions.sh`/`defs.sh`）、"构建开关"（`compile-check.sh:113`）、"插件白名单"（`plugins/CMakeLists.txt:158`，plugins-android 主审）、"发布文案"（`release-notes.md:15`）四个范围。最小改动是关掉 `ENABLE_LIBFDK` 并移除 `add_subdirectory(obs-libfdk)`，后者会落到 plugins-android 的写范围，需要 lead 协调，避免我的报告与 plugins-android 的建议冲突。

5. **`PORTING.md:729-733` 声称"非 debuggable 包插件全加载失败"已解决**，但如 P1-6 所述，CI 完全不验证 APK 内容（`PORTING.md:43`），"打两遍"方案没有任何自动化验收。建议 lead 把 P1-6 的"APK assets 验收"（方案 C，约 10 行 shell）列为发版前必做项——它是唯一能在 CI 里低成本拦住该阻塞项回归的手段。

6. **`PORTING.md:761` 的 `paths 漏了 deps/**`** 与 `deps/glad-gles/`（本方向 CMake 集成）直接相关：改 `deps/glad-gles/CMakeLists.txt` 不会触发 CI。这与 graphics-gles 审查员的改动范围重叠，建议由 lead 统一决定 P1-4 选 A（黑名单）还是 B（补白名单），避免两个方向各自提出冲突的 workflow 修改。

7. **`actions/checkout@v4` 默认 `fetch-depth: 1`** 是 `versionconfig.cmake:9` 问题的根因（`PORTING.md:99-101`）。上游 `build-project.yaml` 用 `fetch-depth: 0`（`git show 65c320b:.github/workflows/build-project.yaml` 前 25 行可见）。若将来想去掉 `OBS_VERSION_OVERRIDE` 这个"钉死的 33.0.1"，给两个 job 的 checkout 加 `fetch-depth: 0` + `fetch-tags: true` 是更根本的做法——但会让 `PORTING.md:14` 的版本矩阵与 `OBS_VERSION_OVERRIDE` 的价值重估，属跨方向的构建策略决策。

8. **许可清单缺失**：`COPYING` 只有 GPLv2 全文，仓库无第三方许可证汇总（`NOTICE` / `LICENSES/`）。APK 实际随包分发 FFmpeg(LGPL)、x264(GPLv2+)、mbedtls(Apache-2.0)、curl、freetype、libjpeg-turbo、libusb/libuvc(LGPL)、glad(WTFPL/CC0 AND Apache-2.0)、Qt(LGPLv3/GPL)、fdk-aac(自定义，见 P0-2)。无论 P0-2 如何处理，都建议产出一个随 Release 附带的 `THIRD-PARTY-LICENSES.md`，否则 LGPL/Apache 的"保留声明"义务难以满足。这需要 graphics-gles / plugins-android / frontend 各方向提供各自引入的组件清单，由 lead 汇总。

---

（报告完。所有引用均为只读检视所得；未修改任何源码、构建脚本或 workflow 文件。）
