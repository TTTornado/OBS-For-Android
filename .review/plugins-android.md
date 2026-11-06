# 审查范围

只读审查。基线 `65c320b`（33.0.1），命令 `git diff upstream/master...HEAD -- plugins/`：`23 files changed, 6568 insertions(+), 9 deletions(-)`。
对象：4 个新增插件（`android-audio/aaudio-input.c` 752 行、`android-camera/camera2-input.c` 1738 行、
`android-capture/uvc-input.c` 2267 行、`android-screen/screen-capture.c` 857 行）及其 CMakeLists；
上游插件的 Android 适配（`plugins/CMakeLists.txt`、`obs-ffmpeg`、`obs-libfdk`、`obs-outputs`、`rtmp-services`、
`text-freetype2`）。对照上游简单源插件：`git show 65c320b:plugins/image-source/image-source.c`（`obs_module_text`、
`OBS_MODULE_USE_DEFAULT_LOCALE`、`obs_module_description`）与 `linux-capture/CMakeLists.txt`。
已读 `PORTING.md`(780 行)、`CODESTYLE.md`(425 行)、`.clang-format`(213 行)、`cmake/android/helpers.cmake`、
`cmake/common/helpers_common.cmake`、`libobs/obs-android.{h,c}`、`frontend/widgets/OBSAndroidPermissions.cpp`、
`frontend/cmake/os-android.cmake`、`frontend/cmake/android/java/com/obsproject/studio/{ObsProjectionHost,ObsPermissionHost}.java`。

# 做了什么

- **android-audio**：AAudio 输入（`aaudio-input.c:302-446` 开流、`:468-535` 采集线程、`obs_source_output_audio`）。
  设备清单走 libobs 音频总线 `obs_android_audio_enum_devices`（`:273-300`、`:657-689`）；API 36 的 MMAP 接口用
  `dlsym` 动态解析（`:56-82`）；断流自愈重开（`:454-466`、`:478-511`）。
- **android-camera**：Camera2 NDK 纯原生取流（`:1037-1281` 开设备/会话、`:710-853` YUV_420_888→I420/NV12）。
  尺寸/FPS 档位取自设备能力表（`:434-529`）；权限/前后台走相机总线（`:1055-1059`、`:1316-1373`）；
  `obs_source_set_async_rotation` 处理方向（`:1252-1269`）。
- **android-capture**：自写最小 UVC-over-libusb（无 root）。fd 由 Java `UsbManager` 经 USB 总线借入
  （`:1240-1262`）；`libusb_wrap_sys_device` + `NO_DEVICE_DISCOVERY`；32 个 64KB 异步 bulk 传输流水线
  （`:1109-1149`、`:1396-1419`）；MJPEG/YUYV/NV12 三种载荷（`:861-954`）；纯字节扫描帧重组状态机（`:1590-1916`）。
- **android-screen**：MediaProjection + AImageReader。总线取尺寸/接面/收面（`:551-608`）；
  `obs_source_set_async_unbuffered` 控内存（`:710`）；一路一份的系统内录泵，带 `obs_source_get_ref`
  扇出表与双锁（`:279-301`、`:303-362`、`:459-494`）；令牌按 `live_sources` 计数交还（`:712`、`:729-732`）。
- **上游适配**：Android 白名单与 core/module 分类（`plugins/CMakeLists.txt`）；关联网更新 + stub；
  关 ffmpeg-mux 伴生进程 + 补静态 FFmpeg 链接；RTMPS CA 路径；字体发现按平台换源；FDK-AAC 编码器启用。

# 4 个新插件实现质量（逐个）

共同点：`OBS_DECLARE_MODULE()` + `obs_module_load()` 里用 `obs_get_source_output_flags()` 回读确认注册
（`aaudio-input.c:745-748`、`camera2-input.c:1716-1720`、`uvc-input.c:2256-2260`、`screen-capture.c:848-852`），
比上游更防御。`obs_source_info` 字段（`OBS_SOURCE_ASYNC_VIDEO`/`AUDIO`/`DO_NOT_DUPLICATE`、
`activate`/`deactivate`/`update`/`get_defaults`/`get_properties`/`icon_type`）符合异步采集源惯例
（对照 `git show 65c320b:plugins/linux-v4l2/v4l2-input.c` 的 `v4l2_input`）。时间戳：
AAudio/内录泵用 `os_gettime_ns()` 起头再按帧数自增避免抖动（`aaudio-input.c:471,519-521`、
`screen-capture.c:306,325-327`），视频帧用 `os_gettime_ns()`（`:197`、`:811`、`:870`）——正确。
`video_format_get_parameters_for_format` 填齐 color_matrix/range（`:816-820`、`:872`、`:951`），
避开了上游 `{0}` 帧导致纯黑的坑。include 块没有被宏包裹，符合 CODESTYLE.md:286。

**android-audio**（较稳）：资源释放与 5 次重开退避完整（`:238-248`、`:454-466`、`:537-556`）。
问题：① `dlopen("libaaudio.so")` 从不 `dlclose`（`:62`）；② `AAudio_setMMapPolicy` 是进程级开关，
`set→open→还原` 在多源并发时互相踩（`:327-354`）；③ 硬编码中文串、无 locale（见下）。

**android-camera**（最完整，风险集中在生命周期）：session→device→reader 拆除顺序按 NDK 头注释写
（`:962-1025`），设备/会话回调 `context=NULL` 避免 UAF（`:887-931`、`dev_cb`/`sess_cb` `:1088-1094`）——亮点。
问题：① 旋转公式硬编码"本 App 锁死横屏 sensorLandscape"，只用 `SENSOR_ORIENTATION`，不查运行时
display rotation（`:1252-1269`），`PORTING.md:751` 自认"相机方向未做真机验证"；
② `close_capture_locked` 在视频线程上等回调最多 2000 ms（`:983-999`），超时置 `cb_thread_maybe_live`
并故意泄漏 reader，`shutting_down` 到下次 `open_capture_locked` 才清（`:1062`）——此期间若重开会与
泄漏的旧回调共用 `context=&c`，双回调同时写 `frames_out`/`in_callback`（数据竞争）。

**android-capture**（体量最大、风险最高）：UAF 防护做得很细（`out:` 取消/回收/释放顺序 `:1921-1976`），
但见 P0/P1。问题：① 只接受 **bulk IN** 端点，等时设备明确不支持（`:485-500`），大量 UVC 摄像头不兼容；
拓扑硬编码 `VC=N / VS=N+1`（`:28-32`）；② 机型怪癖表硬编码单台 `{0x2b89,0x5348}`（`:540-544`），
默认 1920x1080@60 MJPEG（`:2059-2062`）；③ `find_eoi_tail` 等边界检查齐全，但 `dump_diag` 读
`st->frame_buf[i]` 前已判 `fn`（`:1034-1041`）——OK。

**android-screen**（Java 契约基本对得上，但模块级并发有洞）：`screenStartDisplay` 用
`ANativeWindow_toSurface`（`OBSAndroidPermissions.cpp:317`）与 Java `startCapture(Context,Surface,int,int,int)`
签名一致（`ObsProjectionHost.java:241`）；`captureSize()` 返回 4 元数组、native 取前 3
（`OBSAndroidPermissions.cpp:263-295`）；`startAudio()`→`int[2]`、`drainAudio(short[],int)`→int
（`:403-424`、`:480-518`）与 `ObsProjectionHost.java:353-464,547-565` 一致。
问题：① `display_size` 合同说"start_display 不回填尺寸"（`libobs/obs-android.h:236-237`），实现却又
`ANativeWindow_getWidth/Height` 又再跑一趟 `screenDisplaySize` 取 dpi（`OBSAndroidPermissions.cpp:327-330`），
与"一次往返"的设计意图矛盾；② 音频泵/令牌/bus 都是模块级全局，见 P0。

## 共同的上游惯例偏离（P2）

4 个插件全部 `get_name()` 返回**硬编码中文**，无 `obs_module_text()`、无 `OBS_MODULE_USE_DEFAULT_LOCALE`、
无 `obs_module_description()`、无 `data/locale/*.ini`（`aaudio-input.c:575-579,738-752`、
`camera2-input.c:1378-1382,1708`、`uvc-input.c:2050-2054,2247`、`screen-capture.c:686-690,840`；
`git ls-files plugins/android-*` 只有 8 个文件，无 `data/`）。对照上游 `image-source.c` 的
`OBS_DECLARE_MODULE()+OBS_MODULE_USE_DEFAULT_LOCALE()+obs_module_description()`：这 4 个是
`PLUGIN` 类型、会出现在插件管理器里（`PORTING.md:296`），但描述为空、无法本地化。
另：`CODESTYLE.md:13-21` 明确禁止"同一行声明多个变量"，而 `screen-capture.c:145`、
`camera2-input.c:713-716`、`uvc-input.c:145` 均 `int32_t fw = 0, fh = 0, ...;`；
`uvc-input.c:823-830` 单行 case 也违反 `.clang-format` `AllowShortCaseLabelsOnASingleLine: false`。
`CMakeLists.txt` 用 `FOLDER plugins/android-*`（上游是 `FOLDER plugins`），但 `set_target_properties_obs()`
已按 `MODULE_LIBRARY` 处理 install/拷贝/注册（`cmake/android/helpers.cmake:101-118`），安装链是对的。

# 上游插件适配的侵入性（量化）

| 文件 | 改动 | 守卫 | 桌面行为变化 |
|---|---|---|---|
| `plugins/CMakeLists.txt` | +159：Android 白名单 + 提前 `return()` + `list(REMOVE_ITEM)` + 补 `set_obs_core_modules()` | `if(OS_ANDROID)` 整块 | 无 |
| `obs-ffmpeg/CMakeLists.txt` | +55/-?：`set(ENABLE_NEW_MPEGTS_OUTPUT OFF)`、`if(NOT OS_ANDROID) add_subdirectory(ffmpeg-mux)`、`log/android/mediandk/ZLIB/camera2ndk` + `--exclude-libs,ALL` | `if(OS_ANDROID)` / `if(NOT OS_ANDROID)` | 无 |
| `obs-ffmpeg/obs-ffmpeg.c` | +6/-1：VAAPI 门禁加 `!defined(__ANDROID__)` | 平台宏 | 无 |
| `obs-libfdk/CMakeLists.txt` | +7：`LINKER_LANGUAGE CXX` | `if(OS_ANDROID)` | 无 |
| `obs-outputs/librtmp/rtmp.c` | +20：`#elif defined(__ANDROID__)` 插在 `__linux__` **之前**，指向 `/apex/com.android.conscrypt/cacerts`、`/system/etc/security/cacerts` | 平台 `#elif` | 无（且必要：clang 在 Android 也定义 `__linux__`，否则命中不存在的 `/etc/ssl/certs`，RTMPS 全挂） |
| `obs-outputs/rtmp-stream.c` | +1/-1：仅错误文案的 `#if` 加 `&& !defined(__ANDROID__)` | 平台宏 | 无（仅字符串） |
| `rtmp-services/CMakeLists.txt` | +40：`ENABLE_SERVICE_UPDATES=OFF`、跳过 `OBS::file-updater`、加 stub 源 + include 路径、自找 `CURL` 并链 `CURL::libcurl` | `if(OS_ANDROID)` / `else()` | 无（桌面仍链 `OBS::file-updater`） |
| `rtmp-services/file-updater-android-stub.c` | +56，新文件 | 整文件 Android-only | 无 |
| `rtmp-services/rtmp-common.c` | +44/-1 | **两处 `#ifdef __ANDROID__` 诊断块是守卫的**（`:330-349`、`:416-438`）；**但 `:373-379` 把 `blog(LOG_DEBUG)` 改成 `blog(LOG_WARNING)` 是全平台无守卫改动** | **有**：桌面 format_version 不符时日志级别变 WARNING |
| `rtmp-services/rtmp-services-main.c` | +8/-3 | **无守卫**：`load_twitch_data()/load_amazon_ivs_data()` 从 `#if defined(ENABLE_SERVICE_UPDATES)` 内挪到外面（`:122-125`） | **有**：`git show 65c320b:plugins/rtmp-services/rtmp-services-main.c` 证实原在门禁内；桌面 `ENABLE_SERVICE_UPDATES=OFF` 构建现在也会执行，往 ingest 表 push 内置默认项并读缓存 |
| `rtmp-services/service-specific/{dacast,service-ingest}.c` | +6 / +14 | `#ifndef __ANDROID__` / `#ifdef __ANDROID__` 分支 | 无 |
| `text-freetype2/CMakeLists.txt` | +8：`$<$<PLATFORM_ID:Android>:find-font-android.c>` + `log` | 生成器表达式 / `if(OS_ANDROID)` | 无 |
| `text-freetype2/text-freetype2.c` | +6：`#elif defined(__ANDROID__) DEFAULT_FACE "Noto Sans CJK SC"` | 平台宏 | 无 |
| `text-freetype2/find-font-android.c` | +424，新文件 | 整文件 Android-only | 无 |

**结论**：绝大部分改动是 `OS_ANDROID` / `__ANDROID__` 守卫的，桌面构建不受影响。
**仅 2 处无条件改动**：`rtmp-services-main.c:122-125`（行为变化，应加
`#if defined(ENABLE_SERVICE_UPDATES) || defined(__ANDROID__)` 或拆成独立上游修复提交）与
`rtmp-common.c:373-379`（日志级别，可回退或加守卫）。另 `obs-ffmpeg/CMakeLists.txt:8-16` 把
`set()` 放在 `option()` **之前**，靠 CMP0077 NEW 才生效（`cmake_minimum_required(VERSION 3.28...3.30)` 满足），
但 `rtmp-services/CMakeLists.txt:5-10` 用的是 `option()` 后再 `set()` 的更稳写法——两处风格不一致，建议统一。

**stub 是否完整**：`shared/file-updater/file-updater/file-updater.h:17,20,22` 只声明 3 个函数
（`update_info_create` / `update_info_create_single` / `update_info_destroy`），
`file-updater-android-stub.c:28,42,53` 全部实现 → 无未定义符号。
**busy-wait**：`service_ingests_refresh` 在 Android 直接置 `ingests_refreshed=true`
（`service-ingest.c:128-138`），`dacast_ingests_load_data` 的等待被 `#ifndef __ANDROID__` 去掉
（`dacast.c:157-170`）→ 不会空等。**服务定义不会静默缺失**：`services.json` 随包（127825 字节、
format_version=5、83 个服务，`file-updater-android-stub.c:20-21`），且 `obs-outputs` 已在白名单里
（`plugins/CMakeLists.txt` 的 `add_subdirectory(obs-outputs)` 注释），协议闸门可通过。
**但确实存在静默硬编码回退**：`dacast_ingest()` 无数据时返回
`dacast_invalid_ingest = {"rtmp://dabcast", "", "", "fake_key"}`（`dacast.c:23,122-128`），
`rtmp_common_url()` 原样返回 `rtmp://dacast`（`rtmp-common.c:913-919`）——用户选 Dacast 会拿到假 URL
且无告警；Twitch/Amazon IVS "auto" 回退到编译期内置默认 ingest（`twitch.c:43-48`、
`service-ingest.c:160-183`）。`showroom.c:111`/`nimotv.c:76` 仍无条件调 `curl_easy_*`，stub 下无 CA 链。

**obs-ffmpeg「注册但不可用」是缺陷**：`obs-ffmpeg.c:351-355` 无条件注册 `ffmpeg_output`、
`ffmpeg_muxer`、`ffmpeg_mpegts_muxer`、`ffmpeg_hls_muxer`、`replay_buffer`；而 `ffmpeg-mux`
伴生可执行文件在 Android 不构建（`obs-ffmpeg/CMakeLists.txt:24-33`，注释自认"注册但一启动就报错"）。
至少 4 个输出在 Android 必然启动失败，设置里仍列出 mkv/flv/mov/mpegts/hls 等格式。
`PORTING.md` 的 CMake 注释把这项归到"P-6 前端清单"待办，即未修。更优：在 `obs_module_load()` 用
`#ifndef __ANDROID__` 门掉这些 `obs_register_output()`，让格式列表自然不出现。

**FDK-AAC 合规（P0 发版阻塞）**：`android/compile-check.sh:113` 显式 `-DENABLE_LIBFDK=ON`，
`android/deps-android/versions.sh:35,58` 取 `mstorsjo/fdk-aac v2.0.3`，
`defs.sh:141-155` 编成静态 `libfdk-aac.a`（并用垫片替换 `log/log.h`），
`plugins/obs-libfdk/CMakeLists.txt` 强制 `LINKER_LANGUAGE CXX` 静态链进插件；Android 白名单把它列为
**core**（`plugins/CMakeLists.txt` 的 `add_subdirectory(obs-libfdk)` 注释），
`.github/release-notes.md:15` 对外宣称"音频编码：AAC（FFmpeg / FDK-AAC）"。
上游默认 **OFF**（`git show 65c320b:plugins/obs-libfdk/CMakeLists.txt:3`
`option(ENABLE_LIBFDK "Enable FDK AAC support" OFF)`）——正是因许可证：
Fraunhofer FDK AAC 许可证被 Debian 归入 **non-free**，其版权文件明写
"*It is incompatible with any version of the GNU GPL*"，且第 3 节 **不授予任何专利许可**
（[Debian fdk-aac copyright](https://metadata.ftp-master.debian.org/changelogs/non-free/f/fdk-aac/testing_copyright)；
[GNU 许可证列表](https://www.gnu.org/licenses/license-list.html.en#fdk)）。
OBS Studio 是 GPL-2.0-or-later（如 `find-font-android.c:1-16` 的 GPLv2 头），把静态链接的
FDK-AAC 打进同一 APK 分发属于典型的 GPL 不兼容 + AAC 专利风险；
对照 [Flathub OBS issue #48](https://github.com/flathub/com.obsproject.Studio/issues/48)
（"built with --enable-nonfree and as such can't be redistributed as a binary"）。
`obs-ffmpeg.c:356` 已无条件注册原生 `aac_encoder_info`（`plugins/CMakeLists.txt` 的 P-6 注释亦说明"没有它
选不到 aac"是指 FFmpeg AAC），**关掉 libfdk 不丢 AAC 编码能力**。建议发版前 `ENABLE_LIBFDK=OFF`
（或至少做法律评估 + 专利授权 + 随包许可证文本），并将其从 core 名单剔除。

# 是否符合上游多平台风格（逐项 verdict）

- 插件目录命名 `android-*`：✅ 合上游 `linux-capture`/`win-dshow`/`mac-*`/`obs-*` 的按平台命名惯例。
- CMakeLists 结构 `add_library(MODULE)` + `OBS::` 别名 + `target_sources` + `target_link_libraries` +
  `set_target_properties_obs(... PREFIX "")`：✅ 与 `git show 65c320b:plugins/image-source/CMakeLists.txt` 同构；
  `PREFIX ""` 去掉 `lib` 前缀是 OBS 插件惯例（`PORTING.md:257`）。`FOLDER plugins/android-*` 是小偏差。
- 按 OS 选源：✅ **text-freetype2 完全照抄上游惯例**——上游已有
  `$<$<PLATFORM_ID:Linux,FreeBSD,OpenBSD>:find-font-unix.c>` / `$<PLATFORM_ID:Windows,Darwin>:find-font.c>`，
  新增 `$<PLATFORM_ID:Android>:find-font-android.c>`（`text-freetype2/CMakeLists.txt:25-28`）正是同一机制。
  `find-font.h` 声明的 6 个函数 `find-font-android.c` 全实现（`:54,61,107,214,334,366`），
  `load_cached_os_font_list()` 返回 false 表示无缓存（`:61-64`），完整。
- 用 stub 顶替 `OBS::file-updater`：**⚠️ 不是上游惯例**。上游没有按 OS 替换 file-updater 的机制，
  它是单一 INTERFACE 目标；这里靠改消费者 CMake + 新增同名 .c + 手动补 include 路径 + 手动补 `CURL`
  （`rtmp-services/CMakeLists.txt:19-88`）绕开。能工作但侵入性中等，且把"平台差异"摊到消费者里。
- 插件数据分发：⚠️ 4 个新插件都没有 `data/` 目录（`git ls-files` 验证），因此不上 locale/effect；
  与上游每个源插件带 `data/locale/*.ini` 不同，也不受益于 `target_install_resources()`。
- core/plugin 分类（`list(REMOVE_ITEM)`）：❌ **不够上游**。`plugins/CMakeLists.txt:175-179` 在
  `add_subdirectory()` 后把 4 个从全局属性 `OBS_MODULES_ENABLED` 里删掉，再 `set_obs_core_modules()`。
  上游提供的是声明式的 `add_core_module(...)` / `add_obs_plugin(... PLATFORMS ...)`（见
  `git show 65c320b:plugins/CMakeLists.txt` 末尾 `add_core_module(oss-audio PLATFORMS FREEBSD OPENBSD)`，
  定义 `cmake/common/helpers_common.cmake:460`）。见「更优实现」。
- 提前 `return()`：❌ 上游单路径结构被劈成两段，`PORTING.md:105-118` 也承认它曾漏掉 33.0 新增的
  `set_obs_core_modules()`（现在 `:179` 已补）；这是 rebase 脆弱点，属已知工程债。

# 问题清单

## P0

1. **FDK-AAC 许可证不兼容，发版合规阻塞**。`android/compile-check.sh:113`、
   `android/deps-android/versions.sh:35,58`、`defs.sh:141-155`、`plugins/obs-libfdk/CMakeLists.txt`、
   `plugins/CMakeLists.txt`(白名单把 obs-libfdk 列 core)、`.github/release-notes.md:15`。
   影响：GPLv2 主程序静态链接 GPL 不兼容、无专利授权的编解码库并随 APK 分发；Debian 归 non-free，
   GNU 明确列为 GPL-incompatible。建议：发版构建 `ENABLE_LIBFDK=OFF`（`obs-ffmpeg.c:356` 的原生 AAC 已足够），
   或完成法律/专利评估并把完整许可证文本与源码可得性随包提供。

2. **android-screen 模块级音频泵的 acquire/release 竞态可导致"泵已死但源以为在跑"**。
   `screen-capture.c:459-494`（`audio_pump_release` 在 `audio_mx` 外调 `audio_pump_stop()`）
   与 `:367-456`（`audio_pump_start_impl` 以 `audio_thread_started` 判幂等）。两颗源 A/B 交叉时：
   A release 算出 `need_stop=true` 但尚未执行 stop，B acquire→start 看到 `audio_thread_started=true`
   直接返回，随后 A 的 `stop` 发信号/join/销毁 `audio_stop`/`bfree(audio_buf)`/
   `obs_android_screen_stop_audio()`（`:425-448`）。B 的音频静默丢失，且 `audio_state_line` 仍可能误报。
   影响：多源/重建场景下内录静默失败。建议：把 `audio_users` 的增减与泵的启停放进同一把
   `audio_life_mx`（或引入显式引用计数 + 单一 owner 的启停状态机），start 在 `audio_thread_started`
   为真时也要校验 `audio_stop` 未被消费；并对泵侧 `audio_buf` 生命周期加锁。

## P1

0. **【P1 首条｜功能缺失】`android-audio` 的"选择具体输入设备"能力是死的：音频总线零注册点**。
   全仓库 `grep obs_android_set_(usb|audio|camera|screen)_host` 只有 `libobs/obs-android.h:60,120,167,273`
   的声明与 `libobs/obs-android.c:275,352,409,464` 的定义；调用点只有
   `frontend/widgets/OBSBasic.cpp:250-259` 的 `obsAndroidPermissionsRegisterCameraBus()` /
   `obsAndroidPermissionsRegisterScreenBus()` / `obsAndroidUsbRegisterBus()` 三条，分别落到
   `OBSAndroidPermissions.cpp:191`（camera）、`:541`（screen）、`OBSAndroidUsb.cpp:147`（USB）——
   **`obs_android_set_audio_host()` 没有任何调用点**。
   后果：`obs_android_audio_ready()` 恒 false、`obs_android_audio_enum_devices()` 恒返回 -1、
   `obs_android_audio_has_permission()` 恒 false（`libobs/obs-android.c:363-398`）。
   而 `aaudio-input.c:273-300`（`aaudio_resolve_device` 靠 `enum_devices` 核对 id）、
   `:660-688`（`aaudio_properties` 靠 `ready()`/`enum_devices()`/`has_permission()` 生成下拉框）
   完全依赖这三条。`:665` 的 UI 文案"系统默认输入（音频总线未接通：宿主 App 没注册
   `obs_android_set_audio_host`）"正是这个未实现状态的自认降级提示；`:687-688` 还会在
   `has_permission()` 恒 false 时打出"RECORD_AUDIO 未授予"的**误报告警**。
   注释里提到的 `ObsAudioHost.MAX_ENTRIES` / `obs_audio_host.cpp`（`aaudio-input.c:150`）在全树
   `grep` 无对应文件（`ObsPermissionHost.java:5` 只提到历史上"查"的口）——即 Java 侧音频宿主也没实现。
   定级 P1 而非 P0：采集本身可走 `AAUDIO_UNSPECIFIED` 交给 AAudio 选默认设备
   （`aaudio-input.c:265-271,324-325`），`device_id` 默认 0（`:635`），不会崩、也不会完全静音；
   死掉的是"选哪台麦克风/耳机麦"这一整个 A4 功能与权限提示的准确性。
   建议：补 `OBSAndroidAudio.cpp` + `ObsAudioHost.java`（照 `OBSAndroidUsb.cpp` /
   `ObsUsbHost.java` 的形状），在 `OBSBasic.cpp:250-259` 注册；或在属性页明确禁用设备下拉并
   去掉那句误导性权限告警。

1. **android-capture 拆除路径的 5 秒阻塞 + 正常关闭也可能泄漏传输缓冲**。
   `uvc-input.c:1929-1941` 的 `out:` drain 循环 `for(k=0;k<250;k++) libusb_handle_events_timeout(20ms)`
   最长 **5 秒**，且循环内**不检查 `shutting_down`**；`uvc_stop_thread()` 在
   deactivate/destroy 线程上 `pthread_join`（`:2037-2046`，注释 `:2042-2043` 却写"最坏堵一个 bulk 超时 1 s"，
   与代码不符）→ UI/视频线程 ANR。同时 `libusb_cancel_transfer` 对**已完成并挂在 `done[]` 队列里**的传输
   返回 `NOT_FOUND`、不产生回调（`:1130-1149`），而回收判据 `drained >= submitted`（`:1933,1943-1944`）
   要求收齐全部 `submitted` 个回调，于是正常退出（循环顶部 break 时 `done_head < done_tail` 很常见）会走
   `:1951-1954` 的"故意泄漏"分支，最多漏 32×64KB + 传输对象。
   建议：drain 循环每轮判 `shutting_down` 并缩短上限；回收判据改为"待取消数 = 仍未收到回调的传输数"
   （统计 `done[]`/`err[]` 中已有的完成项），或改为逐个 `libusb_free_transfer` 前确认该 transfer 已完成/已取消。

4. **rtmp-services 两处无守卫改动污染桌面**。`rtmp-services-main.c:122-125` 把
   `load_twitch_data()/load_amazon_ivs_data()` 移出 `#if defined(ENABLE_SERVICE_UPDATES)`
   （`git show 65c320b:...` 证实原在门禁内），桌面 `-DENABLE_SERVICE_UPDATES=OFF` 也会执行，
   会 push 内置默认 ingest 并读本地缓存文件（`service-ingest.c:160-183`）——行为变化；
   `rtmp-common.c:373-379` 把 `LOG_DEBUG` 改 `LOG_WARNING` 亦为全平台。
   建议：前者加 `#if defined(ENABLE_SERVICE_UPDATES) || defined(__ANDROID__)`，或把"默认 ingest 应始终加载"
   作为独立上游修复提交；后者恢复 `LOG_DEBUG` 或加 `__ANDROID__` 分支。

5. **Dacast 静默回退到硬编码假 URL**。`dacast.c:23`（`{"rtmp://dacast","","","fake_key"}`）、
   `:122-128`、`rtmp-common.c:913-919`（`rtmp_common_url` 直接返回）、`:936-941`（key 返回 `"fake_key"`）。
   stub 下 `update_info_create_single` 恒 NULL，等待又被 `#ifndef __ANDROID__` 去掉，用户选 Dacast
   会得到连不上的地址且无任何告警。建议：Android 档在 `rtmp_common_url()` 里对 `dacast_invalid_ingest`
   显式 `blog(LOG_WARNING)` 并在属性页禁用 Dacast 条目，或提供内置默认 ingest。

6. **obs-ffmpeg 4 个「注册但必然失败」的输出**。`obs-ffmpeg.c:351-355` 无条件注册，
   `obs-ffmpeg/CMakeLists.txt:24-33` 不构建伴生 `obs-ffmpeg-mux`，注释自认"一启动就报错"。
   影响：设置里可选但必失败的格式。建议：`obs_module_load()` 内用 `#ifndef __ANDROID__` 门掉
   `ffmpeg_muxer`/`ffmpeg_hls_muxer`/`replay_buffer`/`ffmpeg_mpegts_muxer` 的注册。

7. **JNI 从插件原生线程高频调用 + Surface 局部引用处置**。`screen-capture.c:303-362` 的泵每 10 ms 在
   自己 `pthread_create` 的线程上经 `obs_android_screen_read_audio` 跑到
   `OBSAndroidPermissions.cpp:480-518` 的 `QJniObject::callStaticMethod`/`QJniEnvironment`；
   该文件 `:495-499` 自认"这条线程不在 JVM 的线程表里"。Qt 每次 attach/detach 的开销与
   `ANativeWindow_toSurface` 返回的 local ref（`:317,341-346`）在高频路径上有风险。
   另外 `ObsProjectionHost.java:149,292` 的 `captureSurface` 静态字段在 `stopCapture` 只置 null、
   未 `Surface.release()`。建议：在前端把音频读取改为一次性长连接 JNI（或 Java 侧回调），
   用全局引用缓存 `Surface` 并在收面时 `release()`。

8. **camera2/screen 的 `cb_thread_maybe_live` 超时路径与重开共用同一 context**。
   `camera2-input.c:983-999,1062,1460-1467` 与 `screen-capture.c:528-543,548,734-741`：
   等 2 s 回调未退出就置 `cb_thread_maybe_live`、泄漏 reader、把 `reader=NULL`，随后
   `screen-capture.c:548` 还把 `shutting_down` 复位为 0；下一次 `try_start`→`open_capture` 会用
   同一个 `c` 再挂一个 listener，泄漏的旧回调（context 仍是 `&c`）也恢复执行，双线程同时写
   `frames_out`/`in_callback`、同时 `obs_source_output_video`。建议：超时后把该源置为永久失能
   （不再 open_capture），或把回调 context 改为独立堆对象 + 引用计数。

## P2

9. 硬编码中文 UI 串、无 locale/无 `obs_module_description()`（4 个插件 `get_name`/`get_properties`；
   对照 `image-source.c`）。插件管理器里这 4 个是 PLUGIN（`PORTING.md:296`），描述为空。
10. `camera2-input.c:1252-1269` 旋转硬编码横屏假设、不查运行时 display rotation；
    `PORTING.md:751` 自认未验证。建议把 `captureSize()` 已有的 `rot`（`ObsProjectionHost.java:219`）
    经总线透传到插件。
11. `uvc-input.c:485-500` 只支持 bulk 端点、`:28-32` 硬编码接口拓扑、`:540-544` 单机型怪癖表、
    `:2059-2062` 硬编码 1920x1080@60；`camera2-input.c:77-78` 硬编码自动档 1920x1080 上限。
12. `text-freetype2.c:529-533` `DEFAULT_FACE "Noto Sans CJK SC"` 绑定特定 OEM 字体家族；
    `find-font-android.c:47-52` 兜底表里的 `"NotoSansCJK-Regular"` 是**文件名不是 family name**，
    `match_font_path()`（`:276-332`）永远匹配不到，该兜底是死代码；`:35` 用 `__android_log_print`
    而非 `blog()`，日志不进 OBS 日志。
13. `aaudio-input.c:62` `dlopen` 不 `dlclose`；`:327-354` 进程级 MMAP 策略多源并发互相覆盖。
14. 代码风格：`screen-capture.c:145`/`camera2-input.c:713-716`/`uvc-input.c:145` 违反
    `CODESTYLE.md:13-21` 单行多变量；`uvc-input.c:823-830` 单行 case 违反 `.clang-format`；
    include 块未按 `CODESTYLE.md:285` 字母序（`PORTING.md:712-716` 已自认 18 个文件）。
15. `obs-ffmpeg/CMakeLists.txt:8-16` 依赖 CMP0077 NEW 的 `set()`-before-`option()` 写法脆弱，
    与 `rtmp-services/CMakeLists.txt:5-10` 的写法不一致。
16. `screen-capture.c:822-838` 的 `obs_source_info` 缺 `.icon_type`（另 3 个都有）。

# 更优实现方案

1. **core/plugin 分类改用上游声明式机制（收益最大）**。上游已有
   `add_core_module()` / `add_obs_plugin(... PLATFORMS ...)`（`cmake/common/helpers_common.cmake:460`）
   与 `$<PLATFORM_ID:...>`。做法：给上游 helper 增加 `PLATFORMS ANDROID` 支持，把 4 个插件写成
   `add_obs_plugin(android-camera PLATFORMS Android)`（默认 PLUGIN 类型），其余写成
   `add_core_module(...)`；删掉 `plugins/CMakeLists.txt` 的 159 行白名单、提前 `return()`、
   手动 `list(REMOVE_ITEM)` 和重复的 `set_obs_core_modules()`（`:175-180`）。
   代价：要改 `cmake/common/helpers_common.cmake` 与 13 个模块的调用点，需回归桌面构建；
   收益：不再有"提前 return 跳过上游新增初始化"（`PORTING.md:105-118`）这类 rebase 地雷，
   分类语义与桌面一致，且 `OBS_MODULES_ENABLED` 不再被二次篡改（现在它还被
   `frontend/cmake/os-android.cmake:127` 与 `cmake/android/helpers.cmake:137` 消费，见「跨范围观察」）。
   保留现有 `REMOVE_ITEM` 方案也可，但至少应把它收敛成一个 `add_android_plugin()` 函数并加注释说明
   它同时影响构建/打包清单/core 清单三个消费者。

2. **把平台 stub 收敛为编译期接口**。给 `shared/file-updater` 增加
   `$<$<PLATFORM_ID:Android>:file-updater-android.c>`（与 text-freetype2 的按 OS 选源同构），
   由 INTERFACE 目标自身决定实现，消费者 CMake 无需 `if(OS_ANDROID)` 分支、无需手动
   `target_include_directories` 与 `find_package(CURL)`（`rtmp-services/CMakeLists.txt:19-88`）。
   代价：动共享目标，需桌面回归；收益：消除 stub 与真身 API 漂移风险、去掉 3 处手工补丁。
   更彻底的做法是在 libobs 定义 `struct obs_platform_services` 函数表（file-updater / font / permission），
   Android 注册 no-op 实现，插件与上游 .c 里就不需要任何 `#ifdef`——代价是接口设计工作量大，
   对当前规模可能过度。

3. **抽共享静态库 `libobs-android-support`**。4 个插件重复了：总线包装调用、
   `shutting_down`/`in_callback`/`cb_thread_maybe_live` 的 AImageReader 拆除协议、
   `os_event`+`pthread` 采集线程启停、限频日志、`set_error`/`status` 快照。
   抽成 `plugins/android-common/`（静态库，纯 C、只链 libobs），能让 P1-8 的拆除竞态只修一处。
   代价：新增一个构建目标与打包项；收益：~600 行样板去重、生命周期协议唯一。

4. **重划 Java/NDK 边界**。camera2/screen 已经证明纯 NDK 取流可行；把 screen 的
   `AudioPlaybackCapture` 也改成"Java 侧只负责令牌与 AudioRecord，PCM 由 native 泵取"（现状已是），
   但应把 `drainAudio` 换成一次 `RegisterNatives` 的回调或固定全局引用缓冲，避免每 10 ms 一次
   JNI attach（`OBSAndroidPermissions.cpp:480-518`）；`ANativeWindow_toSurface` 的 local/global
   判定（`:339-346`）应落成确定策略而非"先看真机给什么"的探针日志。

5. **输出/服务的可见性对齐**。Android 上不注册不可用的 ffmpeg muxer 系列；在
   `rtmp-common.c` 的服务列表里对依赖联网更新的服务（Dacast/Showroom/Nimotv）显式标注或禁用，
   而不是让 stub 静默返回假 ingest。

# 实现亮点

- 4 个插件都在 `obs_module_load()` 回读 `obs_get_source_output_flags()` 确认注册成功
  （`aaudio-input.c:745`、`camera2-input.c:1716`、`uvc-input.c:2256`、`screen-capture.c:848`），
  比上游"注册返回 void、失败只 blog"更严谨。
- `camera2-input.c:887-931` 设备/会话回调一律 `context=NULL`，从设计上消掉"回调跑在相机框架线程、
  `data` 已 bfree"的 UAF；`:962-1025` 严格按 NDK 头 `session.close()`（异步）→`device.close()`（同步）
  → 再释放 output/container/target/request 的顺序。
- `screen-capture.c:710` `obs_source_set_async_unbuffered(true)` 把整屏 RGBA 稳态内存从"最多 30 帧"
  降到 1~2 帧，注释把 `obs-source.c:3540/4132` 的依据写清楚；`:279-301` 用 `audio_mx`/`audio_life_mx`
  两把锁显式修掉"持表锁 join 泵线程"的死锁（注释 `:284-287`）。
- `uvc-input.c:1109-1149` 用 `cancelled` 单调计数判断取消是否收齐，替代固定泵次数的猜测；
  `:1921-1976` 明确"宁可泄漏也不在回调在场时 delete"——思路正确（判据有缺陷，见 P1-3）。
- `obs-outputs/librtmp/rtmp.c:339-361` 把 `__ANDROID__` 分支放在 `__linux__` **之前**，
  并给出"clang 在 Android 也定义 `__linux__`"的具体理由 + 两个系统信任锚路径的实测读数，
  是全组最干净的一处平台适配。
- `text-freetype2` 用 `$<PLATFORM_ID:Android>` 加源（`:28`），与上游按 OS 选源惯例完全一致；
  `find-font-android.c` 自带 UTF-16BE→UTF-8 转换（`:366-424`），不依赖 bionic 缺失的 iconv。
- 4 个新插件的 include 块都没有被 `#ifdef` 包裹，符合 `CODESTYLE.md:286`（`PORTING.md:702-708`
  自认的那 24 处违规在别处）。

# PORTING.md 声明核验表

| 声明（行号） | 判定 | 证据 |
|---|---|---|
| `:107-118` Android 分支提前 `return()` 会跳过上游末尾 `set_obs_core_modules()`，需在 return 前补一次 | ✅ 属实 | `git diff` 显示 `plugins/CMakeLists.txt` Android 块末尾加了 `set_obs_core_modules()`（`:179`）再 `return()`；`git show 65c320b:plugins/CMakeLists.txt` 末尾确有 `set_obs_core_modules()` |
| `:284-296` 8 个上游插件 + obs-libfdk = core，4 个 Android 插件 = PLUGIN，插件管理器可显示/禁用 | ✅ 基本属实 | `plugins/CMakeLists.txt:175-177` `list(REMOVE_ITEM ... android-audio android-camera android-capture android-screen)`；`cmake/common/helpers_common.cmake:528` `set_obs_core_modules()` 读的正是该属性。core 数 = 13 个 `add_subdirectory` − 4 = 9，与"8+1"一致 |
| `:302-309` 实现在 `add_subdirectory()` **之后**、`set_obs_core_modules()` **之前** REMOVE_ITEM | ✅ 属实 | `plugins/CMakeLists.txt:175-179`；`cmake/android/helpers.cmake:118,137-144` 证实 `target_install_resources()` 在 `set_target_properties_obs()` 期间（即 add_subdirectory 内）读该属性决定 `share/obs/obs-plugins/<模块>` |
| `**:307-309` "先剔掉会让数据落到错的位置"对这 4 个成立** | ⚠️ 存疑 | 4 个插件都无 `data/` 目录（`git ls-files plugins/android-*`），`target_install_resources()` 对它们是空操作；真正受影响的是**后续消费者** `frontend/cmake/os-android.cmake:127`（见下） |
| `:733` 非 debuggable 包实测 13 个插件、0 失败 | ✅ 数字自洽 | 9 core + 4 plugin = 13；具体装载属 libobs/前端范围，未在本方向核验 |
| `:280-282` "装载器不再用编译期白名单：就是 `obs_load_plugins()` + `os_glob($ROOT/lib/*.so)`" | ⚠️ **存疑 / 内部矛盾** | `frontend/cmake/os-android.cmake:150-156` 仍在用 `OBS_MODULES_ENABLED` 生成 `android-plugin-modules.h`，注释写"OBSApp::loadAppModules() 的 Android 分支按这张表逐个裸名 dlopen"；而 `frontend/plugin-manager/PluginModuleLoader_Android.cpp:44-46` 说编译期白名单是"以前……被迫改成"、解到私有目录后已废除，`:38-42` 又说内置插件由 `obs-core-modules-loader-android.c` 按 `obs_core_modules[]` 装载。两处叙述冲突，需核实 `frontend/OBSApp.cpp:2696` `loadAppModules()` 是否仍 include 该头 |
| `:38-42`（PluginModuleLoader）"obs_core_modules[] 里那 14 个" | ❌ 数字对不上 | Android 白名单 `add_subdirectory` 共 13 个（含 4 个被 REMOVE_ITEM 的），core 实际 9 个；`PORTING.md:295` 也说 8+1。"14"疑为旧版本残留 |
| `:43` CI 不覆盖任何运行时行为 | ✅ 属实 | `.github/workflows/android-compile.yml` 两个 job 只做编译/打包；4 个插件源码里大量"真机实测/b1xx"注释均为人工验证，无自动化覆盖 |
| `:295` core 免疫 disabled 配置、PluginManager 跳过 core | ✅ 机制属实（未逐行核验 `obs-module.c:502`/`PluginManager.cpp:42`） | 与 `helpers_common.cmake:528` 把 `OBS_MODULES_ENABLED` 写进 `obs-core-modules.c` 一致 |
| `:751` "相机方向（渲染期旋转=N°）未做真机功能验证" | ✅ 属实且与代码一致 | `camera2-input.c:1252-1269` 的旋转公式是静态推算、注释自认"真机上若方向仍不对，改这里"；与源码头注释"六条腿都在"的乐观语气存在张力 |
| `:672-674` / `:689-691` 代码风格自认（缺大括号、include 块宏包裹、字母序、常量命名） | ✅ 属实 | 本次核验 4 个 .c 无宏包裹 include、C 风格单语句无括号合法；但 `CODESTYLE.md:13-21` 的"单行多变量"违规在 `screen-capture.c:145` 等处存在，`PORTING.md` 未列此项 |
| `:295` 把 obs-libfdk 列 core（不可禁用） | ✅ 属实，**但叠加 P0 合规问题** | 见问题清单 P0-1：core 意味着必然随 APK 分发，无法由用户关闭，放大许可证风险 |
| `:296` 4 个 Android 插件"显示、可禁用"，且 A4 声称 `android-audio` 支持"选哪台输入设备" | ⚠️ **存疑 / 部分不实** | 显示/禁用机制成立（REMOVE_ITEM → PLUGIN 装载）；但"选设备"能力因 `obs_android_set_audio_host()` 零调用点而完全失效（见问题清单 P1-0）。`PORTING.md` 未记录这一缺口，`待办`(`:727-780`) 也未列出 |

# 跨范围观察（交给 lead 分派）

- **core 清单与打包装载清单是同一份 `OBS_MODULES_ENABLED`，REMOVE_ITEM 会同时影响两者**。
  顶层 `CMakeLists.txt` 顺序是 `add_subdirectory(plugins)` → `add_subdirectory(frontend)`
  （`git show 65c320b:CMakeLists.txt`），而 `frontend/cmake/os-android.cmake:127` 在 frontend 阶段
  读同一属性来生成 `android-plugin-modules.h`（`:150-156`）和 `obsplugindata` 资源列表（`:130-146`）。
  `plugins/CMakeLists.txt:175-179` 已经把 4 个 Android 插件删掉，因此它们**不会**出现在这两个清单里。
  由于 4 个插件无 `data/`，资源清单无影响；但装载清单是否仍被 `OBSApp::loadAppModules()` 使用
  （`frontend/OBSApp.cpp:2696`、`:2706 EnsureAndroidPluginBinaries()`）需要 frontend 方向确认——
  这直接关系到 `PORTING.md:280` 的声明是否成立，以及 4 个 PLUGIN 是否真能被 `os_glob` 装到。
- `PORTING.md:744` 待办"ObsForegroundService.java 缺 microphone 前台类型（Android 14+ 后台录音静默断）"
  直接影响本方向的两个音频采集源（AAudio 与 AudioPlaybackCapture）：退后台后可能静默拿到全零 PCM，
  与 `screen-capture.c:297-301` `audio_peak_all` 探针观测到的"峰值恒 0"是同一族现象，建议联合排查。
- `PORTING.md:758-759` 待办"探针日志 63 处……建议加 `OBS_ANDROID_PROBE` 开关"：本方向
  `camera2-input.c:834-851`（每 300 帧全帧三平面扫描）、`uvc-input.c:881-920`（首帧全帧像素统计）、
  `aaudio-input.c:527-531`（每 100 推）、`screen-capture.c:353-359`（每 200 发）都是 LOG_INFO 常驻日志，
  应纳入该收敛。
- `PORTING.md:43` 与源码注释中大量"实测/b1xx"结论共存：CI 无法回归这些行为，P0/P1 中的
  JNI 线程、UVC drain、音频泵竞态都缺乏自动化覆盖，建议至少补编译期静态检查或单元级线程模型测试。
