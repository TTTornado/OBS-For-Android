# OBS Android 移植代码审查：Qt 前端新增的 Android 专有实现

审查员：`frontend-android`（只读审查，未修改任何源码）
基线：`upstream/master` = `65c320b46`（"libobs: Update version to 33.0.1"），`HEAD` = `dc6e156d6`
范围判定依据：`git diff --name-status upstream/master...HEAD -- frontend/` 中被标为 `A` 的文件。
所有结论都带 `文件:行号`；命令输出用 `>` 前缀标出。

---

# 范围

`git diff --name-status upstream/master...HEAD -- frontend/` 中 `A` 且属于本次分工的文件共 **28 个**：

| 组 | 文件 |
|---|---|
| CMake / 模板 | `frontend/cmake/os-android.cmake`、`frontend/cmake/templates/android-plugin-modules.h.in` |
| 清单 / 资源 | `frontend/cmake/android/AndroidManifest.xml`、`frontend/cmake/android/res/drawable/obs_keepalive.xml`、`frontend/cmake/android/res/xml/qtprovider_paths.xml` |
| Java 桥 | `frontend/cmake/android/java/com/obsproject/studio/` 下 `ObsDisplayHost.java`、`ObsForegroundService.java`、`ObsPermissionHost.java`、`ObsProjectionHost.java`、`ObsUsbHost.java` |
| 插件管理 | `frontend/plugin-manager/PluginModuleLoader_Android.cpp` |
| utility | `AndroidMenuPos.hpp`、`AndroidRuntimeBootstrap.cpp/.hpp`、`CrashHandler_Android.cpp`、`android-window-fit.hpp`、`platform-android.cpp`、`system-info-android.cpp` |
| widgets | `OBSAndroid{Display,KeepAlive,Path,Permissions,Usb}.{cpp,hpp}`（10 个） |

> 命令输出：
> ```
> A	frontend/cmake/android/AndroidManifest.xml
> A	frontend/cmake/os-android.cmake
> A	frontend/cmake/templates/android-plugin-modules.h.in
> A	frontend/plugin-manager/PluginModuleLoader_Android.cpp
> A	frontend/utility/AndroidMenuPos.hpp
> A	frontend/utility/AndroidRuntimeBootstrap.cpp
> ... (共 28 个 A 文件，M 文件由 frontend-intrusion 负责)
> ```

**未纳入**（属 M 文件 / 其它范围，仅在"跨范围观察"里作为证据引用）：`frontend/CMakeLists.txt`、`OBSApp.cpp`、`obs-main.cpp`、`CrashHandler.hpp`、`feature-plugin-manager.cmake`、`OBSQTDisplay.*`、`OBSBasic_*`、`OBSProjector.cpp`、`Multiview.cpp`、`SurfaceEventFilter.hpp`、`android/build-apk.sh`、`libobs/obs-android.h`、`libobs/obs-module.c`。

---

# 做了什么（按子系统）

整体思路：桌面 OBS 的"平台相关实现"用 CMake 按 `OS_*` 选文件（`git show upstream/master:frontend/cmake/os-linux.cmake` 里 `utility/platform-x11.cpp`、`utility/CrashHandler_Linux.cpp`；`frontend/cmake/feature-plugin-manager.cmake` 里 `PluginModuleLoader_Linux.cpp` 等）。移植照抄了这个套路，新增一整套 `*_Android.cpp` / `platform-android.cpp` / `os-android.cmake`，并在 `frontend/CMakeLists.txt` 里加 `elseif(OS_ANDROID) include(cmake/os-android.cmake)`。

1. **运行时路径引导**：`obs-main.cpp:892`（M）在 `main()` 最前面调 `PrepareAndroidRuntimePaths()`（`AndroidRuntimeBootstrap.cpp:257`）：拿应用私有目录 → 把 Qt 资源 `:/obsdata`、`:/obsfrontenddata`、`:/obsplugindata` 解到 `$ROOT/share/...`，`setenv("OBS_ROOT_PATH", root, 1)`（`:280`），并打一组探针。`AndroidRuntimeBootstrap.hpp:29-37` 声明两个入口。
2. **插件 assets 解压与加载**：`EnsureAndroidPluginBinaries()`（`:238`）在 `OBSApp::loadAppModules()`（`OBSApp.cpp:2706`，M）里、`obs_load_core_modules()` 之前调；按 `assets:/obs-plugins/MANIFEST` 把 `.so` 逐个写 `.part` 再 rename 到 `$ROOT/lib`（`:145-234`），用 `.managed` + `.bundle-stamp`(SHA1) 做增量（`:120-121`, `:171-187`）。`PluginModuleLoader_Android.cpp:64-93` 用 `obs_load_plugins()` + `os_glob($ROOT/lib/*.so)` 扫目录，替换上游桌面 loader。
3. **JNI / Java 桥接**：5 个 `OBSAndroid*.cpp` 全走 `QJniObject::callStaticMethod/callStaticObjectMethod` 调 `com/obsproject/studio/Obs*Host` 的 `public static` 方法；`Context`/`Activity` 从 `org/qtproject/qt/android/QtNative` 的 `activity()` / `getContext()` 取（`OBSAndroidDisplay.cpp:30-34`、`OBSAndroidPermissions.cpp:214-218`、`OBSAndroidUsb.cpp:32-43`）。仅 `OBSAndroidDisplay.cpp:221-252` 用 `RegisterNatives` 开了两条 Java→native 回调（`nativeSurfaceEvent`、`nativeTouch`）。
4. **权限**：`ObsPermissionHost.java` 首启批量申请 RECORD_AUDIO / POST_NOTIFICATIONS(API33+) / CAMERA（`:155-164`），结果不接回调、靠 `checkSelfPermission` 现读（`:85-91`）；`OBSAndroidPermissions.cpp:149-194` 把 `obs_android_camera_host` 三条腿注册进 libobs。
5. **前台服务 / 保活**：`AndroidManifest.xml:109-116` 声明 `specialUse|mediaProjection|microphone` 单服务；`ObsForegroundService.java` 起停 + `PARTIAL_WAKE_LOCK`（`:402-414`）+ 通知；`OBSAndroidKeepAlive.cpp:36-92` 把"有没有输出在跑"翻译成起停并去重。
6. **USB**：`ObsUsbHost.java` 枚举/`requestPermission`/`openFd`/热插拔 receiver；`OBSAndroidUsb.cpp:108-168` 把四条腿注册成 `obs_android_usb_host`。
7. **投影 / 显示宿主**：`ObsProjectionHost.java` 是收 `onActivityResult` 的透明 Activity，管 MediaProjection token、`VirtualDisplay`、`AudioPlaybackCapture` 环形缓冲（`:353-565`）；`OBSAndroidPermissions.cpp:521-579` 把 9 条腿注册成 `obs_android_screen_host`。`ObsDisplayHost.java` + `OBSAndroidDisplay.cpp` 把预览控件映射为 `SurfaceView → ANativeWindow → obs_display`，并用 `surfaceSerial` 状态机（B-8）驱动重建。
8. **崩溃处理**：`CrashHandler_Android.cpp` 实现 `getPlatformType()/findLastCrashLog()/getCrashLogDirectory()` 三个平台钩子。
9. **窗口尺寸适配**：`android-window-fit.hpp` 的 `androidFitTopLevelToScreen()` / `androidPreviewMinHeight()`。
10. **菜单定位**：`AndroidMenuPos.hpp` 的 `OBSMenuPos/ OBSMenuPosBelow / OBSMenuPosCenter`，绕开 Android 上 `QCursor::pos()` 不跟触摸更新的问题。
11. **系统信息**：`system-info-android.cpp` 读 `ro.*` 系统属性 + `/sys/.../cpuinfo_max_freq` + `uname()` 填 `GoLiveApi::Capabilities`。
12. **杂项**：`platform-android.cpp` 的 `GetDataFilePath/GetDefaultVideoSavePath/GetPreferredLocales/IsAlwaysOnTop/...`；`OBSAndroidPath.cpp` 把桌面 `QDesktopServices::openUrl(本地路径)` 换成"显示路径 + 复制剪贴板"。

---

# 架构与实现质量（分子系统）

## 3.1 JNI 边界

**线程亲和处理总体正确**：Java 侧 `ObsDisplayHost.attach/detach/alignToRequest` 用 `postToUi()`（`ObsDisplayHost.java:237-254`）把 `addContentView` / `getLocationOnScreen` 投递到 Android UI 线程并 `CountDownLatch.await`；native 回调 `jniSurfaceEvent`/`jniTouch` 在 Android UI 线程被调后只用 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 投递回 Qt 线程立即返回（`OBSAndroidDisplay.cpp:131-151`, `:153-156`, `:162-219`），并显式注明"绝不能同步等 Qt 主线程……同步等必死锁"（`:119-121`）。这是本移植里最扎实的一处并发设计。`OBSAndroidPermissions.cpp:108-112` 也用 `std::atomic<bool>` 缓存 `applicationState`，避免视频线程读 GUI 线程状态。

**JNIEnv 获取**：绝大多数用 `QJniObject`（内部自行 attach）；直接取 `JNIEnv` 的地方都先判 `env.isValid()`：`OBSAndroidDisplay.cpp:420-425`、`:230-234`；`OBSAndroidPermissions.cpp:500-508` 甚至为"插件 pthread_create 出来的原生线程"专门判空并注明"少这一格判空是段错误"。但 **`OBSAndroidPermissions.cpp:271-274`、`:413-416`、`:389-398`、`:315-317` 的 `QJniEnvironment env;` 未判 `isValid()` 就 `env->GetArrayLength` / `env->DeleteGlobalRef` / `ANativeWindow_toSurface`**，与同文件 `:500-508` 的标准不一致（见 P1-6）。

**局部/全局引用**：
- `ANativeWindow_fromSurface()` 的引用在 `obsAndroidDisplayRelease()` 里 `ANativeWindow_release()` 归还（`OBSAndroidDisplay.cpp:416-418, :453-457`），注释正确。
- `ANativeWindow_toSurface()` 返回的 `jobject` 用 `GetObjectRefType` 现问再决定 `DeleteLocalRef`（`OBSAndroidPermissions.cpp:336-346`），并诚实注明"这一轮先看真机给什么，之后再按它收拾"——即**已确认存在未收拾的分支**（见 P2）。
- `g_audioScratch` 使用 `NewGlobalRef` + `DeleteGlobalRef`（`:385-398, :447-448`），因为要在插件音频线程反复用，理由正确；`GetIntArrayElements/GetShortArrayElements` 一律 `JNI_ABORT` 只读不回写（`:283, :424, :517`）。
- `QJniObject::fromString(...).object<jstring>()` 作为临时量传参（`OBSAndroidUsb.cpp:89, :96`）：临时对象活到完整表达式结束，JNI 调用期间有效，安全。

**异常检查**：`ensureSurfaceNativesRegistered()` 在 `FindClass` 失败、`RegisterNatives` 返回非 `JNI_OK` 时都调了 `ExceptionClear()`（`OBSAndroidDisplay.cpp:235-248`），是全范围唯一显式处理 JNI 异常的地方。其余所有 `QJniObject::callStaticMethod*` 调用**没有一处**检查/清除 JNI 异常；Qt 内部会清掉异常并返回默认值，因此 Java 抛异常时 native 拿到 `false/0/空串` 却无法区分"没这个类"与"方法返回 false"（例如 `OBSAndroidUsb.cpp:137-145` 的 `install()` 失败会被当成"该设备没有 USB Host 服务"）。见 P2。

**FindClass / GetMethodID 缓存**：`kSurfaceNatives` 用 `std::call_once` 一次性 `RegisterNatives`（`OBSAndroidDisplay.cpp:226-252`），好。但方法 ID 完全没有缓存：每个 `callInt/callString`（`:43-51`）和每秒一次的 `drainAudio`（`OBSAndroidPermissions.cpp:490`）、`captureSize`（`:263`）、`snapshot`（`OBSAndroidUsb.cpp:61`）都重复 `GetStaticMethodID`。Qt 的 `QJniObject` 内部有缓存，属可接受但非最优。

**方法签名正确性**：逐条核对 Java 声明与 C++ 签名后**基本一致**：`attach(Landroid/app/Activity;IIII)Z`（`OBSAndroidDisplay.cpp:371-373` ↔ `ObsDisplayHost.java:315`）、`nativeSurfaceEvent(I)V`/`nativeTouch(IFF)V`（`OBSAndroidDisplay.cpp:221-224` ↔ `ObsDisplayHost.java:195,199`）、`startCapture(Landroid/content/Context;Landroid/view/Surface;III)Z`（`OBSAndroidPermissions.cpp:332-334` ↔ `ObsProjectionHost.java:241`）、`captureSize(Landroid/content/Context;)[I`（`:263-264` ↔ `:232`）、`drainAudio([SI)I`（`:490` ↔ `:547`）、`start(Landroid/content/Context;Ljava/lang/String;)Z`（`OBSAndroidKeepAlive.cpp:74-76` ↔ `ObsForegroundService.java:85`）。libobs 三条总线的**字段顺序也逐项对得上** `libobs/obs-android.h:43-57, 145-164, 206-270` 与 `kUsbHost`/`kCameraHost`/`kScreenHost`（`OBSAndroidUsb.cpp:108-114`、`OBSAndroidPermissions.cpp:149-158, :521-532`），没有错位。
**唯一确定的签名错误**：`OBSAndroidUsb.cpp:35-38` 用 `"()Landroid/content/Context;"` 去查 `QtNative.activity()`，而 `activity()` 的真实返回类型是 `Landroid/app/Activity;`（同文件 `OBSAndroidPermissions.cpp:31-34` 用的是正确签名）。见 P1-5。

**Context / Activity 生命周期**：
- `ObsForegroundService.java:91-92`、`ObsUsbHost.java:71`、`ObsProjectionHost.java:608-609` 都立刻 `getApplicationContext()`，不把 Activity 留在静态字段——正确。
- 但 `ObsDisplayHost.java:88` 的 `private static Activity activity` **持有 Activity 强引用**，只在 `detach()`（`:629-631`）清空。`AndroidManifest.xml:137` 的 `configChanges` 虽然覆盖了大部分配置变化，仍有 Activity 被销毁重建的路径（开发者选项"不保留活动"、权限弹窗导致的 pause/resume、部分 OEM 行为）。重建后 `addContentView` / `runOnUiThread` 会打在已销毁的 Activity 上（`WindowLeaked` 甚至崩溃），且静态引用泄漏整个 Activity。见 P1-3。

## 3.2 插件加载链路

**时序属实**：`PrepareAndroidRuntimePaths()` 在 `main()` 最前、`QApplication` 之前（`obs-main.cpp:892`，M），`EnsureAndroidPluginBinaries()` 在 `OBSApp::loadAppModules()` 最前（`OBSApp.cpp:2706`，M），`loadAllPlugins()` 在 `:2713`；`PluginModuleLoader_Android.cpp:72-74` 明确"解压不在这里"。`AndroidRuntimeBootstrap.cpp:276-278` 解的是 Qt 内置资源 `:/...`（不依赖 `assets:` 引擎），`assets:/obs-plugins/MANIFEST` 只在 `EnsureAndroidPluginBinaries()`（QApplication 之后）打开（`:147`）——**PORTING.md 关于"`assets:` 必须晚于 QApplication"的核心论断成立**。

`os_glob($ROOT/lib/*.so)` 这条链也成立：`libobs/obs-module.c:796-827` 的 `find_modules_in_path()` 在 `bin` 不含 `%module%` 时拼 `*` + `get_module_extension()` 并 glob，`PluginModuleLoader_Android.cpp:79-86` 喂的正是 `$ROOT/lib` + `share/obs/obs-plugins/%module%`；`.part`/`.managed`/`.bundle-stamp` 因不匹配 `*.so` 天然被排除。

**幂等/并发**：`EnsureAndroidPluginBinaries()` 用 `static bool done`（`:243`）且"只有成功才置位"（`:253-254`），`extractPluginBinaries()` 用 SHA1 标记 + 逐文件存在检查跳过（`:171-187`），`.managed` 只删"上版登记过、本版没有"的（`:189-196`），用户自放 `.so` 不会被碰——设计正确。但 `static bool done` 的读-改-写没有任何同步（`std::call_once` / `std::atomic` 都没用），而同文件 `OBSAndroidDisplay.cpp:228` 却用了 `std::call_once`，风格不一致；若两个线程同时进（例如保活看门狗 + 装载路径）会重复解压、交错写同一批文件。见 P2。

**路径安全**：`:158` 的 `!name.contains('/')` 挡住了 `../` 形式的 MANIFEST 项，`build-apk.sh:131` 的 MANIFEST 由 `ls -1 ./*.so | sed` 生成，文件名可信。但 `:190-195` 的 `.managed` 回读循环**没有对 `m` 做任何校验**，而 `$ROOT/lib` 是 PORTING.md:310-319 明示"用户能写的地方"——`.managed` 可被用户/其它插件篡改，写入 `../../somefile` 就会让 `QFile::remove(dstRoot + '/' + m)` 删掉私有目录外的文件。见 P1-2。另外名字未限制前导 `.`，理论上可覆盖 `.managed`/`.bundle-stamp` 本身。

**部分写入 / 磁盘不足 / 升级残留**：`.part` + `rename` 保证单文件原子（`:208-226`），`wrote != data.size()` 有检查（`:217-221`）。但**集合级没有原子性**：若第 3 个插件写失败直接 `return false`（`:203, :213, :220, :225`），前两个已是新版本、其余还是旧版本，而 `.managed`/`.bundle-stamp` 只在全部成功后才写（`:230-231`）。当前进程随后照样 `loadAllPlugins()`（`OBSApp.cpp:2713`）→ 新旧 ABI 混装可能直接崩；下一次启动虽会重解，但**本轮已经晚了**。磁盘不足时 `.part` 残留也不会被清理。见 P1-1。

**失败降级**：`unpack()` 把 `extractTree()` 的 `bool ok` 只用于打日志、直接丢弃（`AndroidRuntimeBootstrap.cpp:99-104`），`PrepareAndroidRuntimePaths()` 之后无条件 `setenv` 并继续；`EnsureAndroidPluginBinaries()` 返回 `void`，调用方无法知道解压失败。也就是**数据/插件解包失败完全没有降级或上报路径**，只能靠 logcat 里 `PKG()` 那行（`AndroidRuntimeBootstrap.cpp:37-38`）。见 P1-1。

## 3.3 权限与前台服务

**声明成套性**：`AndroidManifest.xml:62-78` 有 `FOREGROUND_SERVICE`、`FOREGROUND_SERVICE_SPECIAL_USE`、`FOREGROUND_SERVICE_MEDIA_PROJECTION`、`FOREGROUND_SERVICE_MICROPHONE`、`WAKE_LOCK`、`POST_NOTIFICATIONS`；`:109-116` 的 `foregroundServiceType="specialUse|mediaProjection|microphone"` + `PROPERTY_SPECIAL_USE_FGS_SUBTYPE` 与 `ObsForegroundService.java:253-277` 的位或一一对应，API 34+ `startForeground(id, n, type)`、API 33 以下走无类型重载（`:274-280`）。这部分做得很细致。

**麦克风类型核实（PORTING.md:744 待办）**：待办写 `ObsForegroundService.java 缺 microphone 前台类型（Android 14+ 后台录音静默断）`，但代码里 **`FOREGROUND_SERVICE_MICROPHONE` 权限已声明**（`AndroidManifest.xml:76`）、**service 类型已含 `microphone`**（`:112`）、**`canMic` 已位或 `FOREGROUND_SERVICE_TYPE_MICROPHONE`**（`ObsForegroundService.java:263-267, :271-272, :276-277`）。即该待办**已过时/已修**（详见核验表）。不过实现仍有边界缺陷：`canMic` 只在 `onStartCommand → promoteToForeground()` 那一刻算一次（`ObsForegroundService.java:181-187`），`retypeIfNeeded()` 只在投影态变化时触发（`:148-165`, `ObsProjectionHost.java:322, :657, :725`）——若用户先拒后补授 RECORD_AUDIO、或授权在服务启动后才到位，不会重算类型；`START_STICKY`（`:203`）在系统重建服务时 `onStartCommand` 会再算一次，但 Android 14+ 从后台以 `microphone`/`mediaProjection` 这类 while-in-use 类型调 `startForeground` 会抛 `SecurityException`（`:284-288` 只 catch 后 `stopSelf`），保活会静默消失。见 P1-4。

**后台录音被掐断**：`PARTIAL_WAKE_LOCK`（`:402-414`）只保 CPU，不保麦克风；真正防掐断的是 `microphone` FGS 类型，如上已实现但有时序缺口。

**权限最小化 / exported / debuggable**：
- `android:debuggable` 未出现 → 默认 false，**PORTING.md:664-668 的说法属实**。
- `QtActivity` `exported="true"`（`:141`）配 MAIN/LAUNCHER 必需且正确；`service`/`ObsProjectionHost`/`FileProvider` 都 `exported="false"`（`:111, :130, :159`），正确。
- `WRITE_EXTERNAL_STORAGE`（`:57`）在 targetSdk 35 / Android 11+ 已无效果，`requestLegacyExternalStorage="true"`（`:99`）在 API 30+ 被忽略——都是无害但过时的声明。
- **`android:allowBackup="true"`（`:100`）未配 `dataExtractionRules` / `fullBackupContent`**：应用私有目录（含 `$ROOT` 与 OBS 配置/推流密钥）会被纳入自动备份/设备迁移。PORTING.md 只讨论了 `debuggable`，没提这条。见 P1-7。
- **Google Play 政策**：`specialUse` 被当作**常驻基类型**，`ObsForegroundService.java:6-9` 明写"dataSync 有 6h/24h 上限，长推流会被系统停掉，所以不用"——即用 `specialUse` 规避 `dataSync` 时长限制。Play 对 `FOREGROUND_SERVICE_TYPE_SPECIAL_USE` 要求单独声明与审核，且明确要求"无其它类型适用时"才用；这里 `mediaProjection`/`microphone`/`mediaPlayback` 都适用，被拒风险实在。`PARTIAL_WAKE_LOCK` 长期持有也有"过度唤醒锁"政策风险。见 P1-4。

## 3.4 Surface / 显示

`OBSAndroidDisplay.cpp` 与 `ObsDisplayHost.java` 组成一条完整链路：`attach()` 前先 `ensureSurfaceNativesRegistered()`（`:358-362`），`getSurface()` → `ANativeWindow_fromSurface()`（`:408-427`），三个尺寸来源交叉校验（`:434-449`），`surfaceSerial` 状态驱动（Java `:795-797` ↔ native `:91-94`），`claimLifecycle/Preemptive/ReleaseLifecycle` 单槽 + 一层挂起（`:257-341`），`setVisible` 用 INVISIBLE 让出模态窗口（Java `:750-788`）。B-8 用"事件只当门铃、状态现读"替代"lost/gained 顺序"，是对 Android Surface 乱序的正确抽象，值得肯定。

但**显示所有权确实存在多处重复实现**：Java 侧一份 `hiddenForModal`/`view`/`holder`/`activity` 静态状态（`ObsDisplayHost.java:88-133`），native 侧一份 `g_lifecycleCtx/g_parkedCtx/g_onSurfaceEvent` 静态状态（`OBSAndroidDisplay.cpp:123-129`），`OBSQTDisplay`（M）、`OBSBasic_Preview`（M）、`OBSProjector.cpp`（M）、`Multiview.cpp`（M）各有自己的 CreateDisplay/releaseDisplay 分支——PORTING.md:760 自承"预览面所有权 5 处各自实现"属实。`ObsDisplayHost.java:27-29` 也自承"当前只做单个活动显示……多面到位时再改成按 id 建表"，即 Multiview/投影仪的显示宿主尚未接进这套桥，与 `:113-116` 注释"全应用只有一个原生窗"互为限制。见"更优实现方案"。

**并发细节**：`g_lifecycleCtx` 用 `QPointer<QObject>` 且 `postSurfaceEvent()` 在锁内取 `.data()` 后在锁外 `invokeMethod`（`:139-151`）——`QObject` 的析构不持有 `g_lifecycleMutex`，QPointer 的置空与 UI 线程的读之间仍有窄窗口；`QMetaObject::invokeMethod` 对已析构对象行为未定义。`postToUi` 的 `CountDownLatch.await(UI_POST_MS=5000)` 与 `attach()` 的 `SURFACE_WAIT_MS=8000`（Java `:66-68, :367`）、`alignToRequest` 的 8 轮 × `Thread.sleep(40)`（`:532-607`）、`detach()` 的 3 秒忙等（`:658-672`）叠加，最坏情况会把 Qt GUI 线程阻塞十几秒。`sizeLatch` 是单个静态 `volatile`（`:93`），B-8 自愈重挂时的旧 `surfaceChanged` 可能提前 countDown 新一次 attach 的闩（`:170-171`）。见 P2。

## 3.5 尺寸 / clamp / 菜单定位

`android-window-fit.hpp:35-89` 逻辑基本正确：`Popup`/`ToolTip` 跳过（`:42-45`），满屏窗用 `before == scr->geometry().size()` 精确判等而不误伤设置对话框（`:60-63`），被 `minimumSizeHint` 挡住时记 `target`/`actual` 并贴左上角避免负 y（`:75-83`），`screen()` 取的是 widget 所在屏、支持多屏坐标。但**存在硬编码**：`margin = 8`（`:52`）、`qMax(320, ...)` / `qMax(240, ...)`（`:53-54`）、`androidPreviewMinHeight()` 的 `[60,120]` 与兜底 `80`（`:96-103`）。PORTING.md:484 称"`androidFitTopLevelToScreen` / `clampTopLevelToScreen` —— 全部从 `availableGeometry()` 取"并不严格成立；在可用宽 < 320 的分屏/折叠外屏上 `maxSize.width()` 反而大于可用宽，窗口仍溢出（`:80-81` 只 move 不缩）。此外 `fit` 只在对话框构造 / 一次 `QTimer::singleShot(0)` 兜底时调（PORTING.md:377-388），**没有监听 `QScreen::geometryChanged`**，折叠屏内外屏切换、横竖屏（本 App 锁 `sensorLandscape`，`AndroidManifest.xml:140`）切换后不会重新 fit。DPI 方面用逻辑像素 `availableGeometry()`，与 PORTING.md:479 "Qt 逻辑像素 = dp" 一致，正确。见 P2 与核验表。

`AndroidMenuPos.hpp` 三条 helper 的算法本身没问题（`:34-77`：有坐标用坐标、否则退回 `QCursor::pos()`；锚下缘/中心 `mapToGlobal`）。但 `OBSMenuPos()` 只决定**顶层菜单**的 `exec()` 位置，无法影响 Qt 内部子菜单几何；PORTING.md:738-740 的"嵌套子菜单坐标错位"待办因此**不可能靠本文件修好**（详见跨范围观察）。`:21-32`、`:36-47`、`:56-59`、`:70-73` 是宏包 `#include` + 宏包代码的混合体，是全范围最典型的 CODESTYLE 违规。

## 3.6 崩溃处理

`CrashHandler_Android.cpp:22-43` 实现 `getPlatformType() → PlatformType::Android`、`findLastCrashLog()` 返回空 path、`getCrashLogDirectory()` 返回空 path。与 `CrashHandler_Linux.cpp:22-39` **逐行同构**（只把 `Linux` 换成 `Android`），且 `CrashHandler.hpp:33`（M）已把 `Android` 加进枚举：
> `git diff upstream/master...HEAD -- frontend/utility/CrashHandler.hpp` → `-enum class PlatformType { ..., FreeBSD };` / `+enum class PlatformType { ..., FreeBSD, Android };`

上游 `CrashHandler.cpp` 的 `setupSentinel()` 用 `GetAppConfigPathPtr()` 建哨兵（`git show upstream/master:frontend/utility/CrashHandler.cpp` 输出 `crashSentinelFile_ = std::filesystem::u8path(crashSentinelFilePath);`），不依赖 `getCrashLogDirectory()`，所以"上次是否正常退出"的判定在 Android 上仍工作——`CrashHandler_Android.cpp:27-30` 的注释属实。**接口契合 verdict：符合且完整**。

但功能上是**空实现**：没有注册任何信号处理器（SIGSEGV/SIGABRT/SIGBUS）、没有读取 `/data/tombstones/`、没有把唯一可见的 logcat 现场导出到可上传文件。`hasNewCrashLog()` 会走 `findLastCrashLog()` 返回空 → `NotAvailable`（上游 `CrashHandler.cpp` 的 `if (lastLocalCrashLogFile.empty() && lastCrashLogFile_.empty()) return CrashLogUpdateResult::NotAvailable;`），崩溃上报链路在 Android 上等于全程关闭。这与 Linux/FreeBSD 上游版一致（不是移植引入的缺陷），但对一个 CI 完全不覆盖运行时、已出现 SIGSEGV 待办（PORTING.md:734-737）的移植来说，可观测性缺口被放大了。Android 侧 tombstone 由 `debuggerd` 写 `/data/tombstones/`，非 root 应用确实读不到——注释判断正确，但可以更进一步（见更优实现）。

## 3.7 系统信息 / 平台杂项

`system-info-android.cpp` 与上游 `system-info-linux.cpp` 同构，用 `__system_property_get`、`/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq`、`uname()`，GPU 能力留空并说明原因（`:62-64`），`system_data.armEmulation = os_get_emulation_status()` 并注明 Houdini 上会误报 false（`:114-117`）——诚实且合理。**缺陷**：`:94` 的 `system_data.build = stoi(sdk)` 无 try/catch，`ro.build.version.sdk` 若为非数字（预览版/codename 构建）会抛 `std::invalid_argument` 未捕获 → `std::terminate`。属 P2。

`platform-android.cpp`：`GetDataFilePath()` 优先 `$OBS_ROOT_PATH/$OBS_DATA_PATH/obs-studio/`（`:59-65`），与 `AndroidRuntimeBootstrap.cpp:277` 的解压目标一致；`GetDefaultVideoSavePath()` 用 `QStandardPaths::MoviesLocation` 并在非目录时退回 `AppDataLocation`（`:84-96`）;`SetAlwaysOnTop` 只记 Qt 属性不重建窗口以免销毁 ANativeWindow（`:145-162`），理由与 S1 实测一致；`CheckIfAlreadyRunning()` 靠 `singleTop` 恒 false（`:181-186`）。这些都是合理的平台适配。`GetPreferredLocales()`（`:98-143`）是上游同款实现。

---

# 是否符合上游多平台风格（逐项 verdict）

| # | 检查项 | Verdict | 证据 |
|---|---|---|---|
| 1 | CMake 按 `OS_*` 选平台文件、`os-android.cmake` 用 `target_sources`/`target_link_libraries`/`set_target_properties` | ✅ 符合 | `os-android.cmake:11-32, :52, :73, :175-216`；上游 `git show upstream/master:frontend/cmake/os-linux.cmake:5` 是 `utility/NativeEventFilter.cpp`，`:4` 是 `utility/CrashHandler_Linux.cpp`；`frontend/CMakeLists.txt` 加 `elseif(OS_ANDROID) include(cmake/os-android.cmake)`（diff 输出） |
| 2 | `platform-android.cpp` / `system-info-android.cpp` 命名与分层 | ✅ 符合 | 对应上游 `platform-x11.cpp` / `system-info-linux.cpp`；`os-android.cmake:17, :18` 挂进 `target_sources`；`NativeEventFilter.cpp`（上游 stub，`frontend/utility/NativeEventFilter.cpp:22-26`）在 `:16` 被复用而非新写，正确 |
| 3 | `CrashHandler_Android.cpp` | ✅ 符合 | 与 `CrashHandler_Linux.cpp:22-39` 逐行同构；`CrashHandler.hpp:33`（M）加 `Android` 枚举；`os-android.cmake:15` 挂入 |
| 4 | `PluginModuleLoader_Android.cpp` | ✅ 符合 | 与 `PluginModuleLoader_Linux.cpp` / `_BSD.cpp` 同构（`git ls-tree upstream/master frontend/plugin-manager/` 列出 5 个平台 loader）；`feature-plugin-manager.cmake` 加 `if(OS_ANDROID)` 分支选它（diff 输出）；实现 `loadPlugins()`/`loadLegacyPlugins()` 两个上游约定的符号 |
| 5 | `qt_add_resources` + 模板生成头文件组织 | ⚠️ 部分符合 | `os-android.cmake:103-116, :138-142` 用 Qt 现代 target API、2 空格缩进、`_UNDERSCORE_PREFIX` 私有变量（`:87-88, :128-129`）符合 CODESTYLE:391-411；但 `:150-156` 生成的头文件是死代码（见 P1-8） |
| 6 | `frontend/widgets/OBSAndroid*.{cpp,hpp}` 的分层 | ⚠️ 不符合上游 | 上游 `frontend/widgets/` 放的是 QWidget 派生类（如 `OBSQTDisplay`），这里 10 个文件只有**自由函数**（`OBSAndroidDisplay.hpp:26-122` 全是 `bool/QString/int` 函数），却按 widget 命名并塞进 `widgets/`；`PluginModuleLoader_Android.cpp:26` 反过来 `#include "widgets/OBSAndroidDisplay.hpp"` 只为拿 `obsAndroidDisplayLog()`，形成 plugin-manager → widgets 的反向依赖 |
| 7 | `android-window-fit.hpp` 头文件自守卫 | ❌ 不算允许的例外 | 见下方专门判断 |
| 8 | `AndroidMenuPos.hpp` 用宏包 `#include` 与代码 | ❌ 明确违反 | `:21-32`（`#ifdef __ANDROID__` 包 `#include "widgets/OBSAndroidDisplay.hpp"` / `<QString>`）、`:36-47`、`:56-59`、`:70-73`；CODESTYLE.md:286 原文 "Instead of using a macro to conditionally compile code ... well, don't do that at all"，并明令 "Do not create platform specific include blocks"、要求 "Refactor the source file to be platform-specific and use CMake to add the appropriate file" |
| 9 | `.cpp` 整个文件被 `#ifdef __ANDROID__` 包裹 | ❌ 冗余、不符精神 | `OBSAndroidDisplay.cpp:3`/`:483`、`OBSAndroidKeepAlive.cpp:3`/`:104`、`OBSAndroidPermissions.cpp:3`/`:581`、`OBSAndroidUsb.cpp:3`/`:170`。这些文件已由 `os-android.cmake:22-31` 只在 Android 挂入，宏是纯噪声；CODESTYLE.md:286 要求用 CMake 选择文件而不是宏 |
| 10 | 头文件 guard | ✅ 符合 | 全部 `#pragma once`（`OBSAndroidDisplay.hpp:6`、`OBSAndroidKeepAlive.hpp:4`、`AndroidMenuPos.hpp:14`、`android-window-fit.hpp:1`、`AndroidRuntimeBootstrap.hpp:27`），CODESTYLE.md:170 明确允许 |
| 11 | 新常量 `kCamelCase` | ✅ 基本符合 | `kStampFile`/`kManagedFile`（`AndroidRuntimeBootstrap.cpp:120-121`）、`kDisplayHostClass`/`kSurfaceNatives`（`OBSAndroidDisplay.cpp:26, :221`）、`kServiceClass`（`OBSAndroidKeepAlive.cpp:14`）、`kCameraHost`/`kScreenHost`（`OBSAndroidPermissions.cpp:149, :521`）、`kUsbHost`（`OBSAndroidUsb.cpp:108`）、`kAlwaysOnTopKey`（`platform-android.cpp:152`）全部合规。唯一例外是死模板里 `OBS_ANDROID_PLUGIN_MODULES`（`android-plugin-modules.h.in:9`）为 UPPER_SNAKE |
| 12 | include 块内 case-sensitive 字母序 | ❌ 不符合 | `OBSAndroidDisplay.cpp:5-10`（`QJniEnvironment, QJniObject, QPointer, QApplication, QMouseEvent, QWidget` 未排序）、`:12-15`（`android/log.h` 应在 `native_window.h` 前）、`AndroidRuntimeBootstrap.cpp:26-27`（`<QString>` 排在 `<QStandardPaths>` 前，应为 QStandardPaths < QString）、`OBSAndroidPermissions.cpp:5-24`（Qt 头被拆成两块，`<string.h>` 混进 C++ 块）、`OBSAndroidUsb.cpp:7-9`（`QJniObject` 应在 `QByteArray` 前）、`platform-android.cpp:27-30`（`QStandardPaths` 应在 `QWidget` 前）。与 PORTING.md:712-716、:777-778 自承的"18 个新增文件"一致 |
| 13 | C++ 分支必须加大括号 | ❌ 不符合 | `android-window-fit.hpp:39-40, :44-45, :48-49, :67-73, :80-83, :100-101`；`AndroidRuntimeBootstrap.cpp:64-65, :67-68, :76-80, :87-89`；`OBSAndroidDisplay.cpp:134-136, :169-170, :176-177`；CODESTYLE.md:199 "No exceptions"、`.clang-format:110` `InsertBraces: true`（C++ 段）、`:84` `SortIncludes: false`。PORTING.md:769-773 自承约 82 处 |
| 14 | 跨目录用双引号 include | ❌ 部分违反 | `AndroidMenuPos.hpp:22` 与 `PluginModuleLoader_Android.cpp:26` 用 `"widgets/OBSAndroidDisplay.hpp"`，而文件分别在 `utility/`、`plugin-manager/`；CODESTYLE.md:288 要求非同目录用尖括号 |
| 15 | 版权头归属 | ⚠️ 存疑 | `AndroidRuntimeBootstrap.cpp:2`、`AndroidRuntimeBootstrap.hpp:2`、`platform-android.cpp:2`、`system-info-android.cpp:2` 都写 `Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>`（抄自上游 `platform-*.cpp`）；`CrashHandler_Android.cpp:2` 抄 `Patrick Heyer`。其中 `AndroidRuntimeBootstrap.*` 是全新文件、并非上游派生作，沿用上游作者头属归属错误；只有 `PluginModuleLoader_Android.cpp:2` 写了 `2026 OBS for Android port contributors` |
| 16 | Java 侧 | N/A | 上游无 Android Java 代码；包名 `com.obsproject.studio`、`Obs*Host` 与 `android-shell` 同源（`ObsUsbHost.java:2-3` 自述"忠实移植，仅改包名"）。Java 常量 `UPPER_SNAKE`（`ObsDisplayHost.java:79-86`）符合 Java 惯例，不受 C++ `kCamelCase` 约束 |

### 关于"头文件自守卫是否算 CODESTYLE 例外"的判断

`android-window-fit.hpp:1-3` 是 `#pragma once` 后紧跟 `#ifdef __ANDROID__`，`:105` 以 `#endif` 收尾——**整个函数体/结构体定义被平台宏包住，非 Android 展开为空**。PORTING.md:703-710 认为"被包含的头文件自己带守卫，无条件包含是安全的"，把它当作可以规避"平台相关 include 块"的例外。

**结论：不算 CODESTYLE 允许的例外，只是程度较轻的违规。**

1. CODESTYLE.md:170 说的 "Header guards are permitted" 指的是**防重复包含的 include guard**（`#pragma once` / `#ifndef FOO_H`），`android-window-fit.hpp` 的 `#ifdef __ANDROID__` 不是 guard，是**用宏条件编译代码**，正落在 :286 引述的 Google 原文 "Instead of using a macro to conditionally compile code ... well, don't do that at all" 射程内。
2. :286 给的正解是"Refactor the source file to be platform-specific and use CMake to add the appropriate file to the target for a specific platform"。这个仓库已经具备条件：`os-android.cmake` 只在 Android 挂文件、`platform-android.cpp` 已是平台专属实现单元。正解应是：公共头 `android-window-fit.hpp` 只放**声明**（`struct AndroidFitResult` + 函数原型，无条件、无宏），实现放进 `os-android.cmake:11-32` 已列出的 `platform-android.cpp`（或新建 `android-window-fit-android.cpp`），桌面端由其它 `os-*.cmake` 提供 no-op 实现——与 `platform.hpp` / `platform-*.cpp` 完全同构。这样连"调用点要不要 `#ifdef` 包 `#include`"这个问题都消失了。
3. 客观地说，这种"自守卫"确实**缓解**了 :286 前半句（调用点不必写 `#ifdef` 包 `#include`，避免了 PORTING.md:774 那 24 处平台 include 块），比 `AndroidMenuPos.hpp:21-32` 那种"宏包 include + 宏包代码"的混合体更接近规范。但它是把违规从调用点搬进被调文件，没有消除违规；而且 `OBSMenuPos()` 在 `#else` 分支里靠 `Q_UNUSED(androidGlobalPos)`（`:45`）维持桌面编译，说明该文件**本质上仍是全平台共用源**，却用宏藏起了 Android 实现——正是 :286 要禁的形态。

**建议**：把 `android-window-fit.hpp` 与 `AndroidMenuPos.hpp` 都改成"声明无条件 + 实现按 CMake 选文件"；至少也要让 `android-window-fit.hpp` 提供一个桌面 no-op 内联实现，使头文件自身不再是条件编译体。

---

# 问题清单

## P0（崩溃 / 数据丢失 / 发版阻塞）

**本范围内未发现确定性 P0。** 说明：
- PORTING.md:729-733 标注为已解决的原发版阻塞（非 debuggable 包插件全加载失败）在代码中确有对应方案（`build-apk.sh:84-131` 走 assets、`AndroidRuntimeBootstrap.cpp:145-234` 解压、`PluginModuleLoader_Android.cpp:79-86` 扫目录），且 `AndroidManifest.xml:95-101` 无 `android:debuggable`（默认 false），链路上自洽。
- 但存在**跨范围 P0**：`PORTING.md:655-662`、`:747` 自承"每次构建现生成新 debug keystore，用户升级必须卸载重装（丢配置）"，这是数据丢失 + 正式发版阻塞，位于 `android/build-apk.sh`（不在我的 A 范围，交给 build-ci / lead 复核）。
- 另一个跨范围 P0 候选：`PORTING.md:734-737` 浮动 dock 有源渲染时 SIGSEGV（`status=11`），其 Surface 销毁路径与我的 `ObsDisplayHost.surfaceDestroyed`（`ObsDisplayHost.java:177-188`）/`OBSAndroidDisplay.cpp:108-121` 的 B-8 状态机直接相关，但修复点在 M 文件（`OBSBasic_Docks.cpp` 等）。

## P1（明显缺陷）

**P1-1 插件解压失败无上报、无集合级原子性，可能导致新旧 .so 混装后原地崩溃**
`AndroidRuntimeBootstrap.cpp:198-233`：`names` 循环里任一环节失败即 `return false`，但前面已 `QFile::remove(dstRoot + '/' + name)` + `rename`（`:222-226`）写入的新 `.so` 不会回滚，`.managed`/`.bundle-stamp` 也不更新（`:230-231`）。当前进程随后仍执行 `OBSApp.cpp:2713` 的 `loadAllPlugins()`，旧版与新版本插件会在同一进程里被 `obs_load_core_modules()` 装载，ABI 不一致时直接崩。磁盘不足是典型触发（`:217-221` 只检查短写、不检查可用空间）。
`AndroidRuntimeBootstrap.cpp:99-104` 的 `unpack()` 把 `extractTree()` 的 `bool ok` 只用于日志、直接丢弃；`:257-280` 之后无条件 `setenv` 继续跑。
`AndroidRuntimeBootstrap.cpp:238-255` 的 `EnsureAndroidPluginBinaries()` 返回 `void`，调用方 `OBSApp.cpp:2706` 拿不到成败。
**改进**：改为"先全部解到 `$ROOT/lib/.staging-<stamp>/`，全部成功后一次性原子换目录（或写 `generation` 标记）"，并把解压结果作为 `bool` 返回给 `loadAppModules()`，失败时明确降级（仅装载 core / 弹错误对话框），`unpack()` 也要把失败冒泡到 `PrepareAndroidRuntimePaths()`。至少在循环失败时删除本轮已写入的文件并保留旧 stamp。

**P1-2 `.managed` 回读未校验，存在路径穿越删除任意私有文件的风险**
`AndroidRuntimeBootstrap.cpp:190-195` 直接对 `readLineList(dstRoot + kManagedFile)` 的每个 `m` 执行 `QFile::remove(dstRoot + '/' + m)`。`.managed` 位于 `$ROOT/lib`，而 PORTING.md:310-319 明示该目录"用户能写的地方"、`:319` 甚至鼓励用户"放一个 `.so` 就能装插件"。任何能写该目录的主体都能把 `.managed` 改成 `../../databases/xxx` 之类，让下次启动删掉应用私有目录外的文件。MANIFEST 侧有 `!name.contains('/')`（`:158`），但 `.managed` 侧完全没有对称校验。
**改进**：删除前对 `m` 用与 `:158` 相同的白名单（如正则 `^[A-Za-z0-9._+-]+\.so$`、拒绝前导 `.`），并用 `QFileInfo(dst).canonicalFilePath()` 断言结果仍在 `dstRoot` 之下。

**P1-3 `ObsDisplayHost` 静态持有 Activity，Activity 重建后可能崩溃并泄漏整个 Activity**
`ObsDisplayHost.java:88` `private static Activity activity`，`:301` 赋值，仅 `:631`（`detach()`）清空；`:305` `addContentView`、`:241-247`/`:421`/`:636`/`:716`/`:781` 的 `runOnUiThread` 都基于它。`AndroidManifest.xml:137` 的 `configChanges` 虽覆盖多数配置变化，但不覆盖"不保留活动"、OEM 触发的 Activity 重建。重建后对已销毁 Activity 调 `addContentView`/`runOnUiThread` 会抛异常或产生 `WindowLeaked`。
**改进**：改为持有 `applicationContext`，Activity 用 `QtNative.activity()` 每次现取（与 `OBSAndroidUsb.java:71`、`ObsForegroundService.java:91` 一致）；或在 `onConfigurationChanged`/`Activity.onDestroy` 时清空并在下次 `attach()` 重新取。

**P1-4 前台服务类型判定只在启动那一刻；`specialUse` 基类型 + `START_STICKY` 有政策与重启风险**
`ObsForegroundService.java:181-187` → `:234-289`：`canMic`（`:263-265`）只在 `promoteToForeground()` 调用时算一次；`retypeIfNeeded()`（`:148-165`）只被投影态变化触发（`ObsProjectionHost.java:322, :657, :725`）。用户"先拒后补授 RECORD_AUDIO"或权限在服务启动后才到位时，类型不会重算，后台录音仍会被掐断——**这正是 PORTING.md:744 待办描述的症状，只是成因从"完全没实现"变成了"条件覆盖不全"**。
`:203` `START_STICKY` 在服务被杀后由系统后台重建，`:277` 会以带 `microphone`/`mediaProjection` 的 while-in-use 类型调 `startForeground`，Android 14+ 从后台启动这类类型会抛 `SecurityException`，`:284-288` 只是 catch 后 `stopSelf()` → 保活静默消失、通知消失。
`:6-9` 明写用 `specialUse` 规避 `dataSync` 的 6h 上限；Play 对 `FOREGROUND_SERVICE_TYPE_SPECIAL_USE` 要求单独声明且限定"无其它类型适用"，而 `mediaProjection`/`microphone`/`mediaPlayback` 均适用，审核被拒风险实在。
**改进**：权限授予态变化时（复用 `applicationStateChanged` 或注册 `ACTION_*` 广播）主动 `retypeIfNeeded()`；`onStartCommand` 中对 `START_STICKY` 的空 intent 走"仅 `specialUse` 的最小档"或干脆返回 `START_NOT_STICKY`；发版前按 Play 的 FGS 声明表逐类型申报，评估把基类型换成 `mediaPlayback`/`dataSync`。

**P1-5 `OBSAndroidUsb.cpp` 用错误 JNI 签名查 `QtNative.activity()`，第二个 Context getter 必然失败**
`OBSAndroidUsb.cpp:35-41` 的 `appContext()` 依次尝试 `getContext` 和 `activity`，但两者都传 `"()Landroid/content/Context;"`。`QtNative.activity()` 的真实签名是 `()Landroid/app/Activity;`（同仓 `OBSAndroidPermissions.cpp:31-34`、`OBSAndroidDisplay.cpp:30-34` 都用对了；`:207-213` 的注释还专门 `javap` 过这两个口）。JNI 的 `GetStaticMethodID` 把返回类型算进签名，`activity` 这一发必然 `NoSuchMethodError` → `QJniObject` 无效。目前靠 `getContext()` 先命中而掩盖，一旦 `getContext()` 在某 Qt 版本失效，USB 总线整体静默装不上，而 `:130-134` 的报错会误导成"拿不到 Context"。
**改进**：`activity` 分支改用 `"()Landroid/app/Activity;"`，或干脆删掉这个不可靠的 fallback（与 `OBSAndroidPermissions.cpp:214-218` 统一只用 `getContext()`）。

**P1-6 `QJniEnvironment` 多处未判 `isValid()` 就解引用，与同文件标准不一致**
`OBSAndroidPermissions.cpp:271-274`（`screenDisplaySize`）、`:413-416`（`screenStartAudio`）、`:315-317`（`screenStartDisplay` 的 `ANativeWindow_toSurface`）、`:389-398`（`audioReleaseScratch` 的 `DeleteGlobalRef`）都直接 `env->GetArrayLength` / `env.jniEnv()` / `env->DeleteGlobalRef`，而同文件 `:500-508` 明确写了"少这一格判空是段错误，不是读不到"。这些函数按 `libobs/obs-android.h:202-205, :254-255` 的约定可能落在插件的视频线程/音频线程（外来 pthread），正是最需要判空的位置。
**改进**：抽一个 `withJniEnv()` helper，所有需要 `JNIEnv*` 的入口统一先判 `env.isValid()`。

**P1-7 `android:allowBackup="true"` 未配备份排除规则，推流密钥随私有数据被自动备份**
`AndroidManifest.xml:100` `android:allowBackup="true"`、`:101` `android:fullBackupOnly="false"`，全仓无 `android:dataExtractionRules` / `android:fullBackupContent`（grep `AndroidManifest.xml` 全文仅 166 行，无这两项）。targetSdk 35 下自动备份/设备到设备迁移会包含应用私有 `files/`（`$OBS_ROOT_PATH` 与 OBS 配置、场景、推流密钥都在这里）。PORTING.md 只讨论了 `debuggable`（`:664-668`）与签名导致卸载丢配置（`:655-662`），没覆盖这条数据暴露面。
**改进**：加 `android:dataExtractionRules` / `fullBackupContent` 把配置目录排除，或按威胁模型直接设 `allowBackup="false"`（注意这会与 PORTING.md:653 "卸载丢配置和场景" 的顾虑相冲突，需一并评估云备份方案）。

**P1-8 生成的 `android-plugin-modules.h` 是死代码，且附带的注释与新 loader 互相矛盾**
`os-android.cmake:150-156` 用 `configure_file(cmake/templates/android-plugin-modules.h.in android-plugin-modules.h ...)` 生成 `OBS_ANDROID_PLUGIN_MODULES[]`（`android-plugin-modules.h.in:9-11`）。全树 grep `android-plugin-modules|OBS_ANDROID_PLUGIN_MODULES`（32 条命中）显示**除 `os-android.cmake` 自身与模板外，没有任何 C++/CMake 引用**：
> `grep` 输出中 `android-plugin-modules.h` 仅出现在 `frontend/cmake/os-android.cmake:150,154,156`、`frontend/cmake/templates/android-plugin-modules.h.in:5,9`、`plugins/CMakeLists.txt:40`（注释）、`PORTING.md:328`（注释）

同时 `os-android.cmake:150` 的注释写「`OBSApp::loadAppModules()` 的 Android 分支按这张表逐个裸名 dlopen」、`feature-plugin-manager.cmake`（M，diff 输出）写「改为按构建期白名单从 APK 内按裸文件名 dlopen」——但 `OBSApp.cpp:2696-2714`（M）实际只调 `EnsureAndroidPluginBinaries()` + `loadAllPlugins()`，`PluginModuleLoader_Android.cpp:31-54, :64-93` 明确"装载器不再用编译期白名单、就是 `obs_load_plugins()` + `os_glob()`"。即**注释停留在被废弃的 P-1 方案**，会误导后续维护者（PORTING.md:280 本身已改成正确说法）。
**改进**：删除 `configure_file` 与模板（或确实需要时把清单接进 loader 做校验），并同步修正 `os-android.cmake:150`、`feature-plugin-manager.cmake`、`plugins/CMakeLists.txt:40` 的注释。

## P2（可维护性 / 稳健性）

- **P2-1 `static bool done` 无同步**：`AndroidRuntimeBootstrap.cpp:243-254` 的读-改-写竞态，与 `OBSAndroidDisplay.cpp:228` 的 `std::call_once` 不一致。改用 `std::once_flag` + 返回值，或 `std::atomic<bool>` + mutex。
- **P2-2 `unpack()` 吞掉失败**：`AndroidRuntimeBootstrap.cpp:99-104`（详见 P1-1）。
- **P2-3 `.part` 残留不清理**：失败路径（`:211-226`）留下 `.part` 文件；`:190-196` 的清理只处理 `.managed` 里的名字。建议启动时扫 `*.part` 删除。
- **P2-4 `.managed` 名字白名单不对称**：`:158` 挡了 `/` 但没挡前导 `.`/`..`；理论上 MANIFEST 里出现 `.managed` 会覆盖账本文件。建议统一用 `^[A-Za-z0-9._+-]+\.so$` 且拒绝以 `.` 开头。
- **P2-5 `privateFilesDir()` 硬编码 `/data/user/0`**：`AndroidRuntimeBootstrap.cpp:51-69` 的退路写死 `/data/user/0/<包名>/files`（`:68`），多用户/工作资料（user 10+）、FBE `user_de` 都会算错，且首条路径失败时 `PrepareAndroidRuntimePaths()` 只 `PKG("FAIL ...")` 后 `return`（`:260-263`），`OBS_ROOT_PATH` 不会设置，后续全链路静默失败。建议改用 `QtNative.getContext().getFilesDir()`（一次 JNI）或把失败显式上报。
- **P2-6 `stoi()` 未捕获**：`system-info-android.cpp:94` `system_data.build = stoi(sdk)`，非数字 SDK 值抛 `std::invalid_argument` 未捕获 → terminate。改 `strtol` + 范围检查。
- **P2-7 `ANativeWindow_toSurface` 的 global-ref 分支未收拾**：`OBSAndroidPermissions.cpp:336-346` 只在 `ref_type == JNILocalRefType` 时 `DeleteLocalRef`，注释自己写"这一轮先看真机给什么，之后再按它收拾"。若真机给 `JNIGlobalRefType`，每次 `startCapture` 漏一枚全局引用。建议按真机结论补 `else if (ref_type == JNIGlobalRefType) DeleteGlobalRef`，并去掉"再看"的临时状态。
- **P2-8 全范围 JNI 调用无异常检查/清除**：除 `OBSAndroidDisplay.cpp:237,246` 外没有 `ExceptionCheck`/`checkAndClearExceptions`，Java 抛异常被 Qt 静默清成默认返回值（例如 `OBSAndroidUsb.cpp:137-145` 把 `install()` 抛异常误报成"该设备没有 USB Host 服务"）。建议每个桥接入口统一 `QJniEnvironment::checkAndClearExceptions()` 并把异常写进 `detail`。
- **P2-9 `OBSAndroidKeepAlive` 的去重是纯本地状态，无法发现服务已死**：`OBSAndroidKeepAlive.cpp:44-60` 只比较 `g_running`/`g_state`，从不调 Java 侧已提供的 `ObsForegroundService.isRunning()`（`ObsForegroundService.java:133-135`）。注释 `:49` 自称"看门狗每秒复核一次"，实际复核的是自己的缓存；一旦服务被系统杀掉（P1-4 的 `START_STICKY` 路径），只要 state 不变就永远不再 `start()`，保活静默丢失。建议在 `repeat` 判定里并入 `isRunning()`。
- **P2-10 `OBSAndroidKeepAlive.cpp:101` 读 `g_running` 未持 `g_keepAliveMutex`**（其它读写都在 `:43-54` 的锁内），数据竞争。
- **P2-11 `releaseProjectionForeground` 在 `instance == null` 时会留下孤儿服务**：`ObsForegroundService.java:340-350` 只有 `wasOurs && instance != null && ctx != null` 才 `stop(ctx)`；若 `start()` 已发出但 Service 尚未 `onCreate`（`startForegroundService` 是异步的），令牌换取失败时 `instance` 仍为 null，走 `retypeIfNeeded()` 什么也不做，随后服务照常起来并带 `screen-capture` 状态常驻——正是 `:335-337` 想避免的"没人认领的常驻通知"。建议无条件 `stopService(...)`。
- **P2-12 `ObsUsbHost.java` 线程安全**：`:59-61` 的 `hotplugEvents`/`lastAction`/`lastPermissionResult` 是普通字段，却在 `:282-288` 的 `onReceive`（主线程）里无锁写、由 `:100-110` 的 `synchronized` getter 读；`:297` 的 `connections.remove(d.getDeviceName())` 也未加类锁，而 `openFd`/`closeFd`/`snapshot`（`:197`, `:237`, `:131`）都在 `synchronized` 里操作同一个 `LinkedHashMap`，存在 `ConcurrentModificationException` / 结构损坏风险。`dropDisconnected()`（`:259`）倒是 `synchronized`，不一致。建议把它们统一到 `synchronized (ObsUsbHost.class)` 或改用并发容器 + volatile。
- **P2-13 `ObsUsbHost.requestPermission` 的 `FLAG_IMMUTABLE` 会丢掉系统回填的 extras**：`:189-191` 只用 `PendingIntent.FLAG_IMMUTABLE`。`UsbManager.requestPermission()` 通过 `PendingIntent.send()` 的 fill-in Intent 回填 `EXTRA_PERMISSION_GRANTED`/`EXTRA_DEVICE`，而 `FLAG_IMMUTABLE` 的语义正是忽略 fill-in（Android 12+ 要求显式声明可变性）。结果 `:285-289` 的 `lastPermissionResult` 会恒为 `denied`、`:294-295` 的 `EXTRA_DEVICE` 取不到。功能上因 `openFd()`（`:203-206`）改用 `manager.hasPermission(d)` 复核而未受损，但诊断与任何依赖该广播结果的 UI 都会误报。建议 API 31+ 用 `FLAG_MUTABLE | FLAG_UPDATE_CURRENT`。（此项建议真机确认一次再定级。）
- **P2-14 Surface 生命周期的窄竞态与阻塞**：`OBSAndroidDisplay.cpp:139-151` 在锁内取 `g_lifecycleCtx.data()`、锁外 `invokeMethod`，`QPointer` 的置空由 QObject 析构触发且不受 `g_lifecycleMutex` 保护；`ObsDisplayHost.java:93` 单个 `sizeLatch` 可能被重挂时的旧 `surfaceChanged`（`:170-171`）提前 countDown；`:66-68` + `:367` + `:600` + `:658-672` 叠加的最坏阻塞可达十几秒。建议 `postSurfaceEventTo` 直接捕获 `QPointer`、给 latch 加 generation、把对齐/销毁等待改为异步状态机。
- **P2-15 尺寸算法硬编码 + 不响应屏幕变化**：`android-window-fit.hpp:52-54`（`margin=8`、`qMax(320,..)`/`qMax(240,..)`）、`:96-103`（`[60,120]` / 兜底 80）；可用宽 < 320 时 `:80-81` 只 move 不缩，仍溢出；全仓无 `QScreen::geometryChanged` 监听。与 PORTING.md:473-475、:484 的"全部从 availableGeometry() 取 / 比例必须有上下限"表述不完全一致。
- **P2-16 注册时的"现调探针"带副作用**：`OBSAndroidPermissions.cpp:534-579` 在 `obsAndroidPermissionsRegisterScreenBus()` 里真的调 `obs_android_screen_start_audio()`（`:560`）再立刻 `stop_audio()`（`:561`），`:550-553` 自承"万一它返回 true（不该有），后面那发 stop_audio 会当场把它收回去"。把副作用探针放进注册函数，在令牌恰好存在的热重启路径上会真的起停一次录音。建议探针只调纯查询（`ready`/`has_consent`/`display_size`）。
- **P2-17 版权头归属**：见风格表 #15（`AndroidRuntimeBootstrap.cpp:2` 等把全新文件署成上游作者 `Lain Bailey`）。
- **P2-18 CODESTYLE 批量违规**：见风格表 #7/#8/#9/#12/#13/#14；PORTING.md:693-716、:767-780 已自承，`.clang-format:110` 的 `InsertBraces: true` 与 `:84` 的 `SortIncludes: false` 意味着"跑一次 `clang-format -i` 能修大括号，但 include 排序要手动"。
- **P2-19 探针/诊断日志与业务代码高度耦合**：`OBSAndroidDisplay.cpp:57-89`（`OBS_PROBE_LOG` 宏 + `debug.obs.probe` 开关）、`AndroidMenuPos.hpp:11-13, :39-43`、各 Java 文件的 `lastAction`/`describe()`、`ObsProjectionHost.java:211-218` 的 `lastMeasured` 去重日志。设计上已有开关（`OBSAndroidDisplay.hpp:67-80`），值得保留，但 `AndroidMenuPos.hpp:11-13` 自承"属待删探针清单一员"，`OBSAndroidPath.cpp:3` 为一行日志反向依赖 `widgets/OBSAndroidDisplay.hpp`。建议把 logcat 通道收敛成独立 `utility/AndroidLog.hpp`（同时修掉风格表 #6 的反向依赖）。

---

# 更优实现方案（含代价）

| 方案 | 内容 | 收益 | 代价 |
|---|---|---|---|
| **A. JNI 桥接收敛为独立静态库 + 生成式绑定** | 把 5 个 `OBSAndroid*.cpp` 里重复的 `hostActivity()/hostContext()/jstringToQString()/callDescribe()` 抽成 `frontend/utility/android-jni/`（或 `libobs-android-bridge`）静态库；用一张表（类名/方法名/签名/返回类型）生成 `QJniObject` 包装与 `RegisterNatives` 表，签名由 `javap -s` 在构建期校验 | 一次性消灭 P1-5 这类"签名写错"、消除 5 份重复的 `QtNative` 取值代码、异常检查可集中在一处（P2-8）、方法 ID 可缓存 | 增加一个 CMake target 与代码生成步骤；Qt 6 的 `QJniObject` 本身已在做方法缓存，收益主要是"防手写签名错"而非性能；需维护生成脚本 |
| **B. 用 Qt 6 的 `QJniObject`/`QJniEnvironment` 全面替代手写 JNI，原生线程统一 attach** | 现有代码已大量用 `QJniObject`；把剩余裸 `env->GetIntArrayElements`/`GetObjectRefType`/`ANativeWindow_toSurface` 也包一层带 `isValid()` 检查的 helper，外来线程由 `QJniEnvironment` 统一 attach/detach | 修掉 P1-6、P2-7、P2-8；线程亲和语义由 Qt 保证 | NDK 专属调用（`ANativeWindow_*`）终究要裸 `JNIEnv`，无法 100% 消除；`QJniEnvironment` 在长生命周期外来线程上的 detach 策略需真机验证 |
| **C. 插件分发：`ContentProvider`/原生 assets 直接 `dlopen`，避免解压** | Android 的 `AssetManager` 只能给 `assets:` 路径，不能直接 `dlopen` 压缩在 APK 里的 `.so`；可行做法是把插件放进 APK `lib/<abi>/` 并统一加 `lib` 前缀（放弃 OBS 的无前缀命名），让系统解到 `nativeLibraryDir`，或在 `libobs` 增加"从 `AAssetManager` 读字节 → `android_dlopen_ext` + `ANDROID_DLEXT_USE_LIBRARY_FD`（memfd）"的平台钩子 | 去掉整条解压链（P1-1/P1-2/P2-1..5）、消除首启解压耗时与磁盘占用、不再有"用户往私有目录塞 .so"的信任边界 | 与 PORTING.md:255-263 实测的"系统只解 `lib*.so`"冲突，需要给全部插件改名并改 `build-apk.sh`/`helpers.cmake`；memfd 方案要 NDK API 23+ 且 `dlopen` 的 `DT_NEEDED`/`RPATH` 相对路径解析会变（插件互相依赖 libobs 符号），风险高。**结论：短期保留现有解压方案、按 P1-1/P1-2 加固，比推倒重来更划算** |
| **D. 清单逻辑并入 `obs_load_plugins` 的平台钩子** | 把 `MANIFEST`/`.managed`/`.bundle-stamp` 的判定从 `frontend/utility/AndroidRuntimeBootstrap.cpp` 下沉到 `libobs/obs-android.c` 的 `add_default_module_paths()` 附近，作为 `obs_load_plugins()` 的 Android 前置钩子 | 消除"解压在 `OBSApp::loadAppModules()`、扫目录在 `PluginModuleLoader_Android.cpp`、路径注册在 libobs"的三处割裂（PORTING.md:265-283 自己描述了这条链）；core 与 plugin 装载共享同一份一致性判定；删掉死代码 `android-plugin-modules.h`（P1-8） | 把 Qt（`assets:` / `QFile`）依赖带进 libobs，破坏 CI 的 `-DENABLE_FRONTEND=OFF 原生层不依赖 Qt` 声明（PORTING.md:34）；更现实的做法是保留在前端，但把入口收敛成 `AndroidRuntimeBootstrap` 单类并给 `bool` 返回 |
| **E. 单一 `PlatformDelegate` 接口替代 5 个散落的 `OBSAndroid*` 组件** | 定义 `class AndroidPlatformDelegate : QObject`，统一持有 JavaVM/Activity/Context、`QJniEnvironment`、`surfaceSerial` 生命周期槽、`obs_android_*_host` 三张总线、前台服务状态；5 个 `OBSAndroid*` 变成它的成员函数或命名空间内薄包装 | 解决 PORTING.md:760 "预览面所有权 5 处各自实现"、消除 Java 侧 `ObsDisplayHost` 与 native 侧 `g_lifecycleCtx` 两份状态（3.4 节）、修掉 P1-3 的静态 Activity、让 P1-4 的权限/投影/服务状态能在一处重算、干掉 `PluginModuleLoader_Android.cpp:26` 的反向依赖 | 改动面覆盖我范围内几乎全部文件 + M 文件（`OBSQTDisplay`/`OBSBasic_Preview`/`OBSProjector`/`Multiview`/`OBSApp.cpp`），需跨审查范围协调；Qt 6 的 `QAndroidApplication` / `QNativeInterface::QAndroidApplication` 已提供部分 context 能力，可先小步把 5 个 `hostActivity()` 合并 |
| **F. 崩溃可观测性** | 在 `CrashHandler_Android.cpp` 注册 `sigaction`（SIGSEGV/SIGABRT/SIGBUS/SIGILL），信号处理器里只做 async-signal-safe 的事：写一份紧凑的 backtrace（`<unwind.h>` `_Unwind_Backtrace`）到 app 私有目录 + `__android_log_print`，并在下次启动时由 `hasUncleanShutdown()` 的既有哨兵机制把 logcat 尾部一起附上 | 补上 3.6 节的可观测性缺口，直接服务 PORTING.md:734-737 那个 SIGSEGV 待办 | 信号处理器里能做的事极受限；tombstone 由 debuggerd 写 `/data/tombstones/` 非 root 读不到（注释判断正确），只能靠自身 backtrace；需要真机验证不引入新的死锁 |

**优先级建议**：先做 A 的"签名表 + 集中异常检查"最小版、D 的"入口收敛 + bool 返回"、E 的"合并 `hostActivity()` 与状态归属"这三步（都不需要推翻架构），再评估 C/F。

---

# 实现亮点

1. **B-8 状态驱动的 Surface 生命周期**：`ObsDisplayHost.java:118-119, :795-797` 的 `surfaceSerial`（活面才给序号，死了给 -1）+ `OBSAndroidDisplay.cpp:108-121` 的"事件只当门铃、状态现读"，把 Android Surface 可以无请求消失、回调乱序这个棘手问题抽象对了；`healIfStale`（Java `:482-525`）带 `healSeq`/`healStreak`/`HEAL_MAX` 的自愈上限，`alignToRequest`（`:532-607`）有限次收敛，都是工程上老练的处理。
2. **`claimLifecycle` / `ClaimLifecyclePreemptive` / `ReleaseLifecycle` 单槽 + 一层挂起**（`OBSAndroidDisplay.cpp:257-341`），并在注释里精确说明"在锁里发 Revoked 会自锁"（`:254-256`）、"槽空着时谁都算主人"与 `LifecycleOwner()` 的区别（`:105-122`）——把 `OBSQTDisplay.hpp:96-98` 提到的"对话框析构把主预览黑屏"事故固化成了可解释的机制。
3. **Java 回调绝不阻塞 Qt 线程**：`ObsDisplayHost.java:191-195, :226-234` 的注释与 `safeNativeEvent` 的 `UnsatisfiedLinkError` 兜底，配合 native 的 `Qt::QueuedConnection`（`OBSAndroidDisplay.cpp:131-137`），设计意图清晰。
4. **插件增量更新的两条规矩**：`.managed` 只动自己登记过的文件、`.bundle-stamp` 用 MANIFEST 的 SHA1 而非版本号（`AndroidRuntimeBootstrap.cpp:106-121, :171-196`），以及 `.part` + rename 的单文件原子写（`:208-226`），思路正确（问题在集合级原子性与校验，见 P1-1/P1-2）。
5. **JNI 细节上的诚实与自证**：`GetObjectRefType` 现问而非猜 global/local（`OBSAndroidPermissions.cpp:336-346`）、`JNI_ABORT` 只读不回写（`:283, :424, :517`）、`QJniEnvironment` 在外来线程上判空（`:495-508`）、`javap` 核对 `QtNative` 只有 `activity()`/`getContext()` 没有 `application()`（`:207-213`）、`getParcelableExtra` 按 SDK_INT 分派（`ObsUsbHost.java:291-295`）——大量注释附了实测 build 号与时间戳，可追溯性远超一般移植代码。
6. **防御性细节**：`ObsUsbHost.java:164-172` 的 `scrub()` 明确把"产品名是设备自报的不可信输入、必须过 scrub 才能进快照串"当作注入面处理；`:207-215` 关旧开新防拔插后死 fd；`:259-270` `dropDisconnected()` 关 fd 防泄漏；`ObsProjectionHost.java:79-86, :632-660` 的 `consentGen`/`myGen` 挡住旧一代 Activity 补送的 RESULT_CANCELED；`:704-730` 的 `onStop` 身份判定防换发令牌被旧回调擦掉。
7. **前台服务类型成套性**：`AndroidManifest.xml:62-78, :109-116` 与 `ObsForegroundService.java:234-289` 的权限/类型/property 三处对应，注释（`:20-23, :231-233`）把"缺任何一处 API 34+ 直接 SecurityException"讲清楚；`ensureProjectionForeground`（`:306-332`）的"先改档、再换令牌"顺序有 b109/b110 实测支撑。
8. **平台文件选择与上游同构**：`CrashHandler_Android.cpp` 与 `CrashHandler_Linux.cpp` 逐行同构、`PluginModuleLoader_Android.cpp` 与 `_Linux.cpp` 同构、`os-android.cmake` 复用上游 `NativeEventFilter.cpp` stub（`frontend/utility/NativeEventFilter.cpp:22-26`）而不重写——分层克制。
9. **诊断开关**：`OBSAndroidDisplay.hpp:67-80` 的 `OBS_PROBE_LOG` + `debug.obs.probe` 系统属性（默认关、只读一次），把"每事件探针"与"必须常驻的生命周期日志"分开，回应了 PORTING.md:758-759 的探针债。

---

# PORTING.md 声明核验表

| PORTING.md 位置 | 声明 | Verdict | 证据 |
|---|---|---|---|
| `:265-273` | 插件走 `assets/obs-plugins/ + MANIFEST` → `EnsureAndroidPluginBinaries()`（QApplication 之后）→ `$OBS_ROOT_PATH/lib` → `obs_load_plugins()/os_glob()` | **属实** | `build-apk.sh:95, :131`；`AndroidRuntimeBootstrap.cpp:147, :200`；`obs-main.cpp:892`（M）在 QApplication 前、`OBSApp.cpp:2706`（M）在其后；`PluginModuleLoader_Android.cpp:79-86`；`libobs/obs-module.c:796-827` 证实 `bin` 无 `%module%` 时 glob `*.so` |
| `:277-279` | "解压必须晚于 QApplication……由 loader 在装载前调 `EnsureAndroidPluginBinaries()`（幂等，只成功才置位）" | **部分存疑** | 时序属实（见上），但"由 loader 调"**有误**：实际调用点是 `OBSApp::loadAppModules()`（`OBSApp.cpp:2706`，M），`PluginModuleLoader_Android.cpp:72-74` 明确写"解压不在这里"。"只成功才置位"属实（`AndroidRuntimeBootstrap.cpp:253-254`） |
| `:280-282` | "装载器不再用编译期白名单：就是 `obs_load_plugins()` + `os_glob`" | **属实** | `PluginModuleLoader_Android.cpp:31-54, :64-93`；但 `os-android.cmake:150` 与 `feature-plugin-manager.cmake` 的注释仍描述旧白名单方案（见 P1-8） |
| `:284-305` | core 8 上游 + obs-libfdk / plugin 4 个采集插件的分法 | **范围外，未核验** | 分类实现在 `plugins/CMakeLists.txt:175-177`（M）与 `libobs/obs-core-modules-loader-android.c`，不在我的 A 范围 |
| `:310-319` | `.managed` 只动登记过的文件、`.bundle-stamp` 存 MANIFEST 的 SHA1、用户自放 `.so` 不碰 | **属实（有缺口）** | `AndroidRuntimeBootstrap.cpp:120-121, :171-187, :189-196, :230-231`；缺口见 P1-2（`.managed` 回读无校验） |
| `:321-333` | 三个构建坑（前端库被当插件 / assets 不经 strip / `lib/obs-plugins` 不变式）均已修 | **范围外，局部可见** | `build-apk.sh:99-114, :116-127` 对应后两条；均在 `android/` 与 `cmake/android/helpers.cmake`，不在我的 A 范围 |
| `:373-384` | `androidFitTopLevelToScreen()` 把窗口缩进 `availableGeometry()` 并居中、必须 show() 前调 | **属实（有硬编码）** | `android-window-fit.hpp:35-89`；但 `:52-54` 有 `margin=8`/`qMax(320,..)`/`qMax(240,..)`，`:484` "全部从 availableGeometry() 取" 不严格（见 P2-15） |
| `:385-401` | `clampTopLevelToScreen` 全局兜底、`CLAMP-BLOCKED` 逐条点名 | **范围外** | 调用方在 `OBSApp.cpp`（M），由 frontend-intrusion 核验 |
| `:175-198` | compileSdk 36 / targetSdk 35 / minSdk 29 / Qt ≥ 6.9 | **属实** | `os-android.cmake:203, :215, :216`；`AndroidManifest.xml` 无 `targetSdkVersion` 显式项但由 `QT_ANDROID_*` 注入 |
| `:217-249` | APK 打两遍、assets 先清空、不删 android-build | **范围外** | `android/build-apk.sh:84-135, :147-151` |
| `:648-653` | `versionCode` 两处一起改（os-android.cmake + AndroidManifest.xml），编码 330001 | **属实（但重复是维护风险）** | `os-android.cmake:189-190` 与 `AndroidManifest.xml:54-55` 完全一致；两处手工同步易漏，建议由构建期注入一处 |
| `:655-662`、`:747` | 现生成 debug keystore，正式版必须换固定 release keystore | **范围外，属实且为 P0** | `android/build-apk.sh`（见 P0 说明）；PORTING.md 自己列为未修待办 |
| `:664-668` | `<application>` 不能有 `android:debuggable="true"`，默认 false | **属实** | `AndroidManifest.xml:95-101` 无该属性 |
| `:680-691` | 硬编码规则（tab/8/120、末尾换行、CMake 2 空格、私有成员尾下划线）全部 ✅ | **基本属实** | `os-android.cmake` 2 空格、新常量多为 `kCamelCase`（风格表 #11）；但 `#pragma once` 之外无违规项需逐文件验（未逐行量 120 列） |
| `:693-701` | C++ 分支缺大括号约 60 处 | **属实** | 风格表 #13 举证；PORTING.md:769-773 另给约 82 处（含 `OBSAndroidDisplay.cpp 18` 等），与 `android-window-fit.hpp:39-40` 等一致 |
| `:703-710` | 约 24 处 `#ifdef __ANDROID__` 包 `#include`；"`android-window-fit.hpp` 在非 Android 展开为空，无条件包含是安全的" | **前半属实，后半是判断偏差** | `AndroidMenuPos.hpp:21-32` 即为实例；"自守卫算例外"的判断见风格表 #7 —— 技术上能让调用点无条件包含，但仍违反 CODESTYLE.md:286，正解是 CMake 选平台文件 |
| `:712-716`、`:777-778` | include 未按字母序，18 个新增文件；`SortIncludes: false` 不会自动排 | **属实** | 风格表 #12 举证；`.clang-format:84` `SortIncludes: false` |
| `:718-721`、`:779-780` | 新常量 `kCamelCase`；`OBSApp.cpp` 的 `HOLD_MS`/`MOVE_SLOP` 违规 | **我范围内属实合规；举的违规例在 M 文件** | 风格表 #11；`HOLD_MS`/`MOVE_SLOP` 在 `OBSApp.cpp`（M，frontend-intrusion 范围） |
| `:729-733` | 【已解决】非 debuggable 包插件全加载失败，实测 13 插件 0 失败 | **链路上自洽，运行时数据无法静态核验** | `build-apk.sh:84-131`、`AndroidRuntimeBootstrap.cpp:145-234`、`PluginModuleLoader_Android.cpp:79-93`；"13 插件 0 失败"属真机结论，CI 不覆盖运行时（PORTING.md:43） |
| `:734-737` | 【待办】浮动 dock 崩溃（SIGSEGV status=11） | **属实、未修；与我的范围相关但修复点在 M 文件** | Surface 销毁回调在 `ObsDisplayHost.java:177-188`、native 决策在 `OBSAndroidDisplay.cpp:108-121`；PORTING.md 自述倾向"去掉 DockWidgetFloatable"，那在 `OBSBasic_Docks.cpp`（M） |
| `:738-740` | 【待办】嵌套子菜单坐标错位；`AndroidMenuPos.hpp` 只覆盖部分场景 | **属实、未修；在我范围内但本文件无法修** | `AndroidMenuPos.hpp:34-77` 只影响顶层 `exec()` 位置，Qt 子菜单几何由 Qt 内部算；需改 Qt 平台插件或覆写子菜单 positioning，非本 helper 能力所及 |
| `:741-743` | 【待办】剩余 `CLAMP-BLOCKED` 对话框 | **范围外** | helper 在 `android-window-fit.hpp`（我范围），但 clamp 与日志在 `OBSApp.cpp`（M） |
| `:744` | 【待办】`ObsForegroundService.java` 缺 `microphone` 前台类型 | **有误 / 已过时（已实现，但覆盖不全）** | `AndroidManifest.xml:76`（`FOREGROUND_SERVICE_MICROPHONE`）、`:112`（`foregroundServiceType` 含 `microphone`）、`ObsForegroundService.java:263-267, :271-272, :276-277`（`canMic` 位或 `FOREGROUND_SERVICE_TYPE_MICROPHONE` + 类型串）；`:13-15` 的头注释也已按实现更新。残留缺口是"只在服务启动那一刻判定/不会重算"（P1-4），待办应改写而非保持 `[ ]` |
| `:745-746` | 【已完成】去掉 `debuggable="true"` | **属实** | `AndroidManifest.xml:95-101` 无该属性 |
| `:747` | 【待办】正式版签名，升级必须卸载重装 | **范围外、属实（P0）** | `android/build-apk.sh`；见 P0 说明 |
| `:755-757` | `AndroidInputFilter`（OBSApp.cpp）承担 5 件事 | **范围外** | `OBSApp.cpp`（M） |
| `:758-759` | 探针日志 63 处，建议加 `OBS_ANDROID_PROBE` 开关 | **部分已完成** | `OBSAndroidDisplay.hpp:67-80` 已有 `debug.obs.probe` 开关 + `OBS_PROBE_LOG`；`AndroidMenuPos.hpp:11-13` 仍自承"属待删探针清单一员" |
| `:760` | "预览面所有权 5 处各自实现 —— 收敛成单一入口" | **属实** | 我范围内已有两份状态：Java `ObsDisplayHost.java:88-133` 与 native `OBSAndroidDisplay.cpp:123-129`；`ObsDisplayHost.java:27-29` 自承"只做单个活动显示，多面到位再改按 id 建表" |
| `:761-765` | CI / 构建可重现性待办 | **范围外** | `.github/workflows/`、`android/deps-android/`、`android/build-apk.sh` |
| `:769-780` | 代码风格四项待办 | **属实** | 见风格表 #12/#13/#8/#11 与 P2-18 |

---

# 跨范围观察

1. **PORTING.md:279 的"由 loader 调 `EnsureAndroidPluginBinaries()`"与 `OBSApp.cpp:2706`（M）不符**——文档与 M 文件之间的口径差，建议 lead 让 frontend-intrusion 核对 `OBSApp.cpp:2696-2714` 后统一措辞。
2. **`feature-plugin-manager.cmake`（M）的注释仍描述废弃的"编译期白名单 dlopen"**（diff 输出），与 `PluginModuleLoader_Android.cpp:31-54` 矛盾；`os-android.cmake:150`、`plugins/CMakeLists.txt:40` 同类。属 M/A 交叉的文档债，修 P1-8 时要一起改。
3. **P0 发版阻塞在 `android/build-apk.sh`（keystore）**（PORTING.md:655-662, :747），不在我的 A 范围，请 build-ci / lead 复核。
4. **浮动 dock SIGSEGV（PORTING.md:734-737）的修复点在 M 文件**（`DockWidgetFloatable`），但其 Surface 销毁/重建路径由我范围内 `ObsDisplayHost.surfaceDestroyed`（`ObsDisplayHost.java:177-188`）与 `OBSAndroidDisplay.cpp:108-121` 承接；若 frontend-intrusion 在处理预览相关 M 文件时改动 `releaseDisplay` 的调用顺序，需回头核对 `obsAndroidDisplayIsLifecycleOwner()`（`OBSAndroidDisplay.cpp:343-349`）的语义。
5. **`CrashHandler.hpp:33`（M）新增 `PlatformType::Android`** 是我范围内 `CrashHandler_Android.cpp:24` 的前提；`frontend/CMakeLists.txt`（M）的 `elseif(OS_ANDROID) include(cmake/os-android.cmake)` 是全部 A 文件被编进目标的前提。这两处若回退，我的范围内文件会整体失联。
6. **`libobs/obs-android.h`（非 frontend，但被我范围内 3 张总线依赖）**：`obs_android_camera_host`/`obs_android_screen_host`/`obs_android_usb_host` 的字段顺序与 `kCameraHost`/`kScreenHost`/`kUsbHost` 的初始化列表逐项对齐（`libobs/obs-android.h:43-57, 145-164, 206-270` ↔ `OBSAndroidPermissions.cpp:149-158, :521-532`、`OBSAndroidUsb.cpp:108-114`），**接口契合无问题**；头文件 `:202-205, :254-255` 的"可能在视频线程/插件音频线程被调、实现方自己保证线程安全"是 P1-6 / P2-12 的直接约束来源，后续改这些总线时务必保持。
7. **`PORTING.md:744` 待办状态需要 lead 统一裁决**：代码已实现 microphone 类型，我按"有误/已过时"记；但残留的"权限晚到不重算"（P1-4）仍是真缺陷。建议把该待办改写为"服务类型需响应权限授予态变化"，不要简单勾掉。
8. **`android-window-fit.hpp` / `AndroidMenuPos.hpp` 是少数会被 M 文件无条件包含的 A 头文件**（PORTING.md:709 明示），把它们重构成"声明 + CMake 选平台实现"会同时改动 M 文件的 include 块，属于需要跨范围协调的改动，已在风格表 #7 给出判断与建议。

---

# 附：本报告使用的核验命令

```
git log --oneline -1 upstream/master        → 65c320b46 libobs: Update version to 33.0.1
git log --oneline -1 HEAD                   → dc6e156d6 docs: 仓库改名 ...
git diff --name-status upstream/master...HEAD -- frontend/        （判定 A/M）
git show upstream/master:frontend/utility/CrashHandler.hpp | grep 'enum class PlatformType'
                                           → enum class PlatformType { InvalidPlatform, Windows, macOS, Linux, FreeBSD };
git diff upstream/master...HEAD -- frontend/utility/CrashHandler.hpp   （确认 + Android）
git grep -n "CrashHandler" upstream/master -- 'frontend/cmake/*.cmake'
                                           → os-freebsd.cmake:4 / os-linux.cmake:4 / os-macos.cmake:9 / os-windows.cmake:25 / ui-utility.cmake:9,10
git show upstream/master:frontend/cmake/os-linux.cmake | head -30
git ls-tree --name-only upstream/master frontend/plugin-manager/   （5 个平台 loader + hpp）
git diff upstream/master...HEAD -- frontend/cmake/feature-plugin-manager.cmake   （if(OS_ANDROID) 分支）
git show upstream/master:frontend/utility/CrashHandler.cpp | grep -n 'setupSentinel\|findLastCrashLog\|NotAvailable'
grep -rn "android-plugin-modules|OBS_ANDROID_PLUGIN_MODULES" 全树 → 仅 os-android.cmake / 模板 / 注释命中
read libobs/obs-module.c:796-827  （find_modules_in_path 的 glob 规则）
read libobs/obs-android.h:43-57,145-164,206-270  （三张总线字段顺序）
read android/build-apk.sh:84-135 （MANIFEST 生成、strip、不变式断言）
```
