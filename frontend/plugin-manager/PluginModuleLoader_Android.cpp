/******************************************************************************
    Copyright (C) 2026 OBS for Android port contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "PluginModuleLoader.hpp"
#include "PluginManager.hpp"

#include <obs-module.h>
#include <util/platform.h>

/* 装载失败要打到 logcat：blog() 在 Android 上只写应用私有目录里的日志文件，
 * 而非 debuggable 的包 run-as 不进去 —— logcat 是唯一现场。 */
#include "widgets/OBSAndroidDisplay.hpp"

#include <cstdlib>
#include <string>

/* Android 上改用**扫目录**装载插件，与 Linux/BSD 的 loader 同一套逻辑
 * （obs_load_plugins() → find_modules_in_path() → os_glob()）。
 *
 * 前提是插件 .so 是真实文件系统上的文件：AndroidRuntimeBootstrap 在
 * OBSApp::loadAppModules() 里（**早于** obs_load_core_modules()）把 APK 的
 * assets/obs-plugins/ 解到 $OBS_ROOT_PATH/lib，这里扫这个目录。
 *
 * 分工：内置插件（obs_core_modules[] 里那 14 个）由
 * libobs/obs-core-modules-loader-android.c 以 OBS_MODULE_TYPE_CORE 先装载，
 * 所以这里扫到它们时会被 obs-module.c 的"already loaded"检查跳过 ——
 * **只有用户自己放进 $OBS_ROOT_PATH/lib 的插件才在这里以 PLUGIN 类型载入**，
 * 也就只有它们会出现在插件管理器里（与桌面一致）。
 *
 * 以前插件留在 APK 的 lib/<abi>/ 里 —— os_glob() 扫不到 APK 内部，才被迫改成
 * 编译期白名单逐个裸名 dlopen。解到私有目录之后那条限制就不存在了
 * （实测：os_glob 在私有目录正常命中，绝对路径 dlopen 也成功）。
 *
 * 路径由 libobs/obs-android.c 的 add_default_module_paths() 注册，模板相同：
 *   bin  = $OBS_ROOT_PATH/lib
 *   data = $OBS_ROOT_PATH/share/obs/obs-plugins/%module%
 * 这里用同一个模板再喂一次 obs_load_plugins()。
 *
 * 好处：用户往 $OBS_ROOT_PATH/lib 放一个 .so 就能装插件、删掉就卸载，
 * 不需要重新打包。 */

namespace {

using State = OBS::PluginManager::State;

} // namespace

namespace OBS {

State PluginManager::loadPlugins(bool /*usePortableMode*/)
{
	const char *rootEnv = std::getenv("OBS_ROOT_PATH");
	if (!rootEnv || !*rootEnv) {
		blog(LOG_ERROR, "[android] OBS_ROOT_PATH 未设置，无法定位插件目录");
		return State::Failure;
	}

	/* 注意：插件 .so 的解压（EnsureAndroidPluginBinaries）**不在这里** ——
	 * obs_load_core_modules() 比这里先跑，它要按 obs_core_modules[] 找 .so，
	 * 所以解压必须在 OBSApp::loadAppModules() 最前面完成。这里只扫目录。 */

	/* bin 不含 %module% ⇒ find_modules_in_path() 按"平铺目录"处理：
	 * glob 出 <root>/lib 下的所有 .so，逐个当模块加载。
	 * 内置插件已作为 CORE 装载过，这里会被"already loaded"检查跳过。 */
	const std::string bin = std::string(rootEnv) + "/lib";
	const std::string data = std::string(rootEnv) + "/share/obs/obs-plugins/%module%";

	ModuleLoadInfo info = {.path_info = {.binary = bin.c_str(), .data = data.c_str()},
			       .type = OBS_MODULE_TYPE_PLUGIN,
			       .name = nullptr};

	const int failed = loadPluginsByInfo(info, failedModules_);

	obsAndroidDisplayLog("PLUGIN-LOAD 扫描 %s：失败 %d 个", bin.c_str(), failed);
	for (const std::string &m : failedModules_)
		obsAndroidDisplayLog("PLUGIN-LOAD 失败 %s", m.c_str());

	return failed > 0 ? State::PartialFailure : State::Success;
}

State PluginManager::loadLegacyPlugins(bool /*usePortableMode*/)
{
	/* Android 上没有"旧式插件目录布局"（$HOME/obs-studio/plugins/…），也无需兼容。 */
	return State::Success;
}

} // namespace OBS
