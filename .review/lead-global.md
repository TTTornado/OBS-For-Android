# 全局量化与横向观察（Lead 自审部分）

> 证据口径：所有 `git diff` 均基于 `upstream/master...HEAD`（merge-base `65c320b` = tag `33.0.1`）。
> 生成时间：2026-11-05。此文件是最终总报告的素材，不含各领域 teammate 的详细结论。

## 1. 基线与规模

- 上游基线确认为 tag **33.0.1**（`65c320b`），fork 保留了完整上游历史（该 commit 可达 331 个祖先提交），因此可以持续 merge/rebase 上游。
- 移植提交 **58 个**（2026-10-29 ~ 2026-11-05，单一作者 TTTornado），其中只有 **1 个 merge**（`b46637fa7 merge: 同步上游 33.0.1`）——即"导入上游快照 + 一次性叠加移植"，不是长期伴随上游演进。
- 与上游差异：**137 文件，+21932 / -148 行**；其中 **A=61、M=76**。删除仅 148 行 ⇒ 几乎没有重写/删除上游逻辑，全部是"新增文件 + 插入代码块"。
- 按顶层目录分布（插入行数）：

  | 目录 | +行 | -行 |
  |---|---:|---:|
  | frontend | 7389 | 76 |
  | plugins | 6568 | 9 |
  | deps（glad-gles 生成代码） | 3258 | 0 |
  | libobs | 1163 | 45 |
  | libobs-opengl | 981 | 16 |
  | android（构建脚本/依赖） | 925 | 0 |
  | (root) | 788 | 1 |
  | .github | 595 | 0 |
  | cmake | 265 | 1 |

  **重心在 frontend 与 plugins 两层**，libobs 核心层改动最小。

## 2. 侵入性量化

- 变更文件里含 `__ANDROID__` 的有 **60 个**，共 **163 次**提及、**126 条**条件编译指令；**`Q_OS_ANDROID` 出现 0 次**（Qt 前端也一律用编译器宏）。
- 76 个被修改的上游文件中，**29 个是无条件改动**（不含任何 `__ANDROID__` 守卫）。逐条分类（完整 diff 见 `.review/_diff_unguarded.txt`）：

  1. **纯上游 bug 修复，对所有平台生效（方向正确，但属于"借移植之名改上游"）**
     - `frontend/OBSStudioAPI.cpp:150,161,172,183,194`：5 处 `if (!main->outputHandler) return nullptr;`，修空指针解引用。
     - `frontend/wizards/AutoConfigTestPage.cpp:444-448`：`res_count` / `maxFPS` 初始化，避免 service 无效时读到栈垃圾。
     - `libobs/obs-service.c:417,426`：两个 getter 补 `obs_service_valid()` 守卫。
     - `frontend/widgets/OBSBasic_{Streaming,ReplayBuffer,VirtualCam}.cpp`：把 `outputHandler->XxxActive()` 改为 `XxxActive()`。**注意（frontend-intrusion 核实更正）**：这 4 个包装函数**不是移植新增**，上游基线 `65c320b` 已存在且已带空指针检查（`git show 65c320b:frontend/widgets/OBSBasic_Streaming.cpp:473` 等），移植只改了 6 个调用点；属"用上游已有的更安全封装替换裸成员访问"，桌面正常路径等价，null 时由崩溃变 `false`。
     > 更优雅做法：拆成独立 bugfix 提交 / 直接提给上游，而不是混在 Android 移植里无条件生效。
  2. **为 Android 服务但无条件生效（需重点确认桌面回归）**
     - `AudioMixer.cpp:871`、`OBSBasic_Preview.cpp:242,260`、`OBSBasic_Scenes.cpp:633`、`OBSBasic_StudioMode.cpp:380-414`：`QCursor::pos()` → `OBSMenuPos(...)`。核实 `AndroidMenuPos.hpp:34-48`：非 Android 分支恒 `Q_UNUSED` 后 `return QCursor::pos()`，**桌面行为不变**，但每个桌面构建都无条件 include 了一份 Android 专有头（且该头在非 Android 下仍有 `OBSMenuPosBelow/Center` 的无用计算）。
     - `frontend/dialogs/OBSBasicFilters.hpp:125-128` + `OBSBasicFilters.cpp:633-645`：**无条件**新增 `showEvent`，在 QShowEvent 里 `resize()` 收高度。而同文件 `:68-78` 的 `#ifdef __ANDROID__` 构造函数里已经调用了 `androidFitTopLevelToScreen(this)`。PORTING.md「前端尺寸处理」明确说"必须在 `show()` 之前调用……等 QShowEvent 到达时窗口已画出，再 resize 会看到『先大后小』闪一下"——**实现与文档自相矛盾**，且注释声称"桌面行为不变"实际只是"通常不触发"（桌面小屏/高分屏仍会触发 resize）。
     - `plugins/rtmp-services/rtmp-services-main.c:1060-1063`：把 `load_twitch_data()` / `load_amazon_ivs_data()` 从 `#ifdef ENABLE_SERVICE_UPDATES` 里挪出，桌面 `ENABLE_SERVICE_UPDATES=OFF` 时也会执行 ⇒ 行为变化。
     - `libobs/data/deinterlace_base.effect`、`format_conversion.effect`：**无条件**把 `/2`→`/2.0`、`float3` 返回值→`float4(rgb,1.)`、`image.Sample(...,0)` 去掉第三参数。这些 effect 是 D3D11/HLSL、桌面 GL、Metal **共用**的着色器源，为 GLES ES 3.0 编译通过而改，却未加平台守卫，需要逐后端验证（详见 graphics-gles 报告）。
  3. **共享头/签名的无条件扩展**
     - `frontend/utility/CrashHandler.hpp:240`：`PlatformType` 枚举加 `Android`，无害。
     - `libobs-opengl/gl-helpers.h:251-253`：`gl_init_face()` **全局**加 `bool swap_rb` 参数；`gl-subsystem.h:203-223` 提供两个版本的 `gs_format_needs_rb_swap()`（Android 真判断 / 桌面恒 `false` 且 `UNUSED_PARAMETER`），于是 `gl-texture2d.c:34-36`、`gl-texturecube.c:39-41` 的调用点**无需任何 #ifdef**。这是全项目最优雅的一处设计：用"编译期恒假的 helper + 统一签名"把平台差异收进一个头文件，桌面侧分支可被编译器消掉。
  4. **无意义残留（应删除以降低侵入性）**
     - `libobs/obs-display.c:259-261`：把 `if (render_display_begin(...))` 拆成 `const bool began = ...; if (began) {`，全文件 `began` 之后再无使用（已 grep 确认）。纯粹多改了一行上游代码。
  5. **CMake 平台分支（符合上游惯例）**
     - `cmake/common/osconfig.cmake`、`CMakeLists.txt`、`frontend/CMakeLists.txt`、`libobs/CMakeLists.txt`、`libobs-opengl/CMakeLists.txt`、`plugins/*/CMakeLists.txt`、`frontend/cmake/feature-plugin-manager.cmake`：均为 `if(OS_ANDROID)/elseif(...)` 或 `$<$<PLATFORM_ID:Android>:...>` 生成器表达式，与上游 `os-*.cmake`、`gl-*.c`、`find-font-*.c` 的组织方式一致。
     - `osconfig.cmake:52-59` 在 `project()` 之前用"`CMAKE_SYSTEM_NAME` + NDK toolchain 文件路径 + ANDROID_ABI"三重嗅探识别 Android：能工作但脆弱（工具链文件命名/路径变化即失效），属于必要妥协，注释已说明。

- **符合上游多平台风格的正面证据**：`libobs/cmake/os-android.cmake:14-26` 通过 `target_sources()` 只列入 `obs-android.c`、`util/platform-android.c`、`obs-core-modules-loader-android.c`，其余 `platform-nix.c`、`threading-posix.c`、`obs-nix-platform.c` 原样复用 Linux 文件（文件头注释明确："bionic 就是一个 POSIX libm/pthread 实现……只有两处必须替换"）。同理 `frontend/cmake/os-android.cmake:11-32` 按平台列入 `platform-android.cpp` / `system-info-android.cpp` / `CrashHandler_Android.cpp` / `OBSAndroid*.cpp`；`libobs-opengl/CMakeLists.txt:33` 按 `$<PLATFORM_ID:Android>` 选 `gl-android.c`；`plugins/text-freetype2/CMakeLists.txt:28` 选 `find-font-android.c`；`frontend/cmake/feature-plugin-manager.cmake:209-213` 选 `PluginModuleLoader_Android.cpp`。**底层完全沿用上游"平台差异收敛到独立源文件、由 CMake 选择"的分层**。
- **不符合/高风险的部分**：frontend 层 60 个上游活跃文件（尤其 `OBSApp.cpp` +629 行 / 10 处 `#ifdef`、`OBSQTDisplay.cpp` +251 行 / 10 处、`OBSBasic.cpp` +228 行 / 7 处）散落条件编译。`OBSApp.cpp` 里约 500 行的 `AndroidInputFilter` 承担 5 件事（PORTING.md 自列为工程债）。`Q_OS_ANDROID` 0 次而全用 `__ANDROID__`，与 Qt 前端惯例有差异（需对照上游 C++ 用宏习惯裁定）。
- **同步成本**：76 个上游文件被修改，其中 `OBSApp.cpp`、`OBSBasic*.cpp`、`OBSQTDisplay.cpp` 都是上游高频改动文件；每次 rebase/merge 上游都可能冲突。PORTING.md 已记录多个"上游新增初始化被提前 `return()` 跳过"之类事故，印证这一成本。

## 3. 工程化、文档与提交规范

- **亮点**：`PORTING.md` 780 行，含版本矩阵、CI 覆盖边界、构建流程、"试过并放弃"的负面结论（build 目录缓存、Release 构建）、真机实测数据、逐项待办。工程记录质量远超一般移植项目。
- **文档滞后（自称"只讲当前状态"但不符）**：待办里
  - `ObsForegroundService.java 缺 microphone 前台类型` —— **已实现**（`ObsForegroundService.java:267` 按权限条件 `type |= FOREGROUND_SERVICE_TYPE_MICROPHONE`，commit `19381a803` 已修，manifest `:76,112` 也已声明），仍列未修；
  - `探针日志 63 处……建议加 OBS_ANDROID_PROBE 运行时开关（默认关）` —— **已实现**（`OBSAndroidDisplay.hpp:67-80` 的 `OBS_PROBE_LOG` 走 `debug.obs.probe` 系统属性，默认关，commit `e7f4ebfb8`），仍列未修。
  - `AndroidManifest.xml:9` 注释引用的 `frontend/CMakeLists.txt:143` 行号已漂移（现为 `:156`）。
  > 待办清单会误导维护者做重复工作，建议每次改动同步更新文档。
- **CI**：`.github/workflows/android-compile.yml` 两个 job（compile 不带 Qt 验证原生层、apk 带 Qt 打包），有 ccache、Gradle 缓存、artifact、release 复用 `workflow_call`；`PORTING.md` 坦承"不覆盖任何运行时行为"。实测 **CI 不运行任何测试/静态检查**（grep `ctest|test|check` 无命中）。4 个新采集插件（AAudio/Camera2/UVC/MediaProjection）零自动化测试，只靠真机人工 ⇒ 质量保障缺口。
- **提交规范**（对照 `CONTRIBUTING.md:44-63` 的 50/72 与 "prefix 按模块名，CI 用大写 `CI`、构建系统用 `cmake`"）：
  - 10 个提交标题超过 50 字符（最长 80）；
  - prefix 混用 `ci`(小写)/`build`/`qt`/`apk`/`frontend`/`docs`/`fix`/`plugins`/`android`/`feat`/`deps`/`libobs`/`merge`，不是上游定义的模块前缀体系；
  - 4 个"一次性导入"式大提交：`022525b39` 16 文件 +7292、`3e51aed82` 25 文件 +4211、`739dbdafe` 51 文件 +3782、`ca5e40d33` 8 文件 +2672，违反上游"每个提交是独立逻辑单元"的规范。
- **代码本地化**：变更相关代码文件共 75128 行，其中 **4381 行含中文字符（5.8%）**；中文注释密度最高的是 4 个新插件（`camera2-input.c` 530、`uvc-input.c` 502、`screen-capture.c` 264、`aaudio-input.c` 132）、Java 层、CI 与 CMake。上游代码全英文 ⇒ 这决定了项目只能定位为**独立 fork**，若要贡献上游需全量翻译注释。
- **版权头**：新增的 `.c/.cpp/.h/.hpp/.java` 共 37 个，仅 **12 个**带版权/许可证头（h 1/3、c 5/11、cpp 5/10、hpp 1/8、java **0/5**）；上游同类 C/C++ 文件普遍带 `Copyright (C) ... by ...`（抽查 `obs-display.c`、`OBSBasic.cpp`、`text-freetype2.c`、`gl-texture2d.c`、`platform-windows.cpp` 均带头，`obs-libfdk.c` 例外）。带头的 12 个里多数写上游作者（Lain Bailey 等）或 `OBS for Android port contributors`，GLAD 生成文件带 SPDX。
- **来源/署名风险**：`PORTING.md:6` 称"原移植工作由 cunminF/OBS_for_Android 完成"，但 58 个提交全部署名 TTTornado，无 `Co-authored-by`，4 个大提交一次性导入上万行且无原始版权声明。若代码源自他人 GPL 仓库，GPLv2 要求保留版权声明与署名 ⇒ 建议核实来源、补齐原始版权头/署名，避免法律瑕疵。

## 4. 合规与安全（发版阻塞候选）

- **【P0 候选】FDK-AAC 静态链接进 APK**：`android/deps-android/versions.sh:35,58,80` 定义 `fdk-aac 2.0.3`（来自 mstorsjo/fdk-aac，属 `output` 批次），`defs.sh:141-154` 的 `build_fdk_aac()` 交叉编译成静态库 `libfdk-aac.a`，`plugins/obs-libfdk/CMakeLists.txt:19-21` 强制 `LINKER_LANGUAGE CXX` 把它链进插件，随 APK 分发。Fraunhofer FDK AAC 使用与 GPLv2 不兼容的自定义许可（FFmpeg 官方因此不分发含 libfdk_aac 的二进制；OBS 上游把 obs-libfdk 做成可选、不随官方构建分发也正是这个原因）。**以 GPLv2+ 分发的 OBS APK 静态链接 FDK-AAC，存在许可证冲突**。建议：改用 Android MediaCodec AAC（NDK AMediaCodec）或 FFmpeg 原生 AAC 编码器替代，或发布前取得法律意见；至少应让该插件默认不启用。
- **供应链 / 可重现性**：`versions.sh:25,37` 的 `x264`/`rnnoise` 钉 `master` 分支而非 tag/commit；`fetch.sh` 无 checksum/gpg 校验（grep `sha|checksum|md5|verify|gpg` 无命中）；每次构建现生成 debug keystore（PORTING.md 已承认，用户升级必须卸载）。PORTING.md 待办已列前两项，但状态仍是"未做"。
- **AndroidManifest**（`frontend/cmake/android/AndroidManifest.xml`）：
  - `android:allowBackup="true"`（`:100`）⇒ 私有数据（含推流密钥/场景配置）可被 adb backup 导出，建议 `false` + `dataExtractionRules`；
  - `requestLegacyExternalStorage="true"` + `WRITE_EXTERNAL_STORAGE`（`:57,99`）在 targetSdk 35/Android 11+ 分区存储下已无效，应改用 MediaStore/SAF；
  - 包名 `com.obsproject.studio` 使用上游组织名，若上架应用商店会有冲突/商标问题，建议改自有反向域名；
  - 正面：`<service>` 与代理 Activity 均 `exported=false`，FileProvider `grantUriPermissions` + `exported=false`，权限与 `foregroundServiceType` 成套（specialUse|mediaProjection|microphone），`uses-feature` 成对处理了 CAMERA 权限隐含 required 的问题，注释详尽并记录真机证据。
- **前台服务**：`specialUse` 类型带 `PROPERTY_SPECIAL_USE_FGS_SUBTYPE`，Google Play 对 `specialUse` 有严格审核要求（需声明用途且可能被拒），上架前需评估；PORTING.md 未提及这一点。

## 5. 实现亮点（供总报告引用）

1. 底层平台适配沿用上游"按平台选源文件"分层，libobs 复用几乎全部 Linux/POSIX 实现（只换 2 个文件），把 ABI/API 侵入压到最小（libobs 仅 +1163/-45）。
2. `gl_init_face(... bool swap_rb)` + 桌面恒 `false` 的 `gs_format_needs_rb_swap()`：调用点零 `#ifdef`，是把平台差异收敛进单点的最佳范例（建议把这一模式推广到前端）。
3. `AndroidMenuPos.hpp` 头文件自守卫、非 Android 恒等，把"触摸不更新光标缓存"这一 Qt/Android 缺陷的修复集中在一个头里。
4. 插件分发：`assets/obs-plugins/*.so` + `MANIFEST` → 启动时解到私有目录 → `obs_load_plugins()` 扫目录；用 `.managed` 清单与 `.bundle-stamp`(MANIFEST SHA1) 支持用户按需增减插件且不误删用户文件，绕开 Android 只把 `lib*.so` 解到 nativeLibraryDir 的限制。
5. `PORTING.md` 记录负面结论与实测数据（"打两遍包"、"为什么不能用 Release"、ccache 四个必需变量），工程决策可追溯。
6. CI 的 ccache 配置（`CCACHE_COMPILERCHECK=content` / `BASEDIR` / `NOHASHDIR` / 压缩 / 上限）与"两个 job 只由 apk job 保存缓存"的单写入方设计，是经过实测的正确性优先取舍。

## 6. 定位与改进方向（供总报告结论）

- **目标 A：长期维护的独立 fork**（当前事实定位）——现状基本成立：改动以新增文件与条件编译为主、上游历史完整、文档与 CI 齐备。优先补：① FDK-AAC 合规；② 自动化测试/静态检查（哪怕先加 host 侧编译与纯逻辑单测）；③ 文档待办同步；④ 把 frontend 的 126 条 `#ifdef` 逐步收敛为"平台委托 + 少量钩子"；⑤ 补版权头、明确原始移植署名；⑥ keystore 固定与 release 签名。
- **目标 B：贡献回上游**——按现状几乎不可能：需拆成数十个逻辑提交、英文化全部注释、移除 FDK、把前端散落的 `#ifdef` 重构为上游式平台后端、补版权头与测试、处理 `specialUse` 前台服务等政策问题。成本高，且 OBS 上游目前没有 Android 支持的既定计划。
- **"更好的实现"总体方向**：把分散在上游活跃 `.cpp` 里的 Android 逻辑，按上游既有惯例收敛到 `frontend/utility/platform-android.cpp` / 独立编译单元 + 少量虚/回调钩子（例如 `AndroidPlatformDelegate`），并统一用 `Q_OS_ANDROID` 或编译期策略对象；对 `OBSApp.cpp` 的 500 行事件过滤器按职责拆成 4 个单责过滤器（PORTING.md 已提出）。libobs-opengl 的 `convert_gs_upload_format` 那套"统一签名 + 编译期恒值 helper"是可直接复用的模板。
