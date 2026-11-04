/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>

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

#include "AndroidRuntimeBootstrap.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStandardPaths>

#include <android/log.h>

#include <cstdlib>
#include <fstream>
#include <string>

/* 这个函数跑在 OBSApp 构造之前，那时 libobs 的 blog 还没有 Qt 日志接管，
 * 所以直接写 logcat —— 判据要能在 adb logcat 里看到。 */
#define PKG_TAG "32-PKG"
#define PKG(...) __android_log_print(ANDROID_LOG_INFO, PKG_TAG, __VA_ARGS__)

using namespace std;

namespace {

/* 应用私有 files 目录。
 * 首选 QStandardPaths —— 壳工程 obs_smoke.cpp 走的就是它，实测拿到
 * /data/user/0/org.qtproject.example.obs_shell/files。但那次调用发生在 QApplication
 * 已经存在之后，本轮发生在 OBSApp 构造之前，"没有 QCoreApplication 实例时它还给不给值"
 * 是没测过的，所以配一条不依赖 Qt 的退路：Android 上 /proc/self/cmdline 的第一个
 * NUL 结尾串就是包名，私有目录固定是 /data/user/0/<包名>/files。
 * 走的是哪条都会打进日志，别让它变成看不见的分叉。 */
QString privateFilesDir()
{
	const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if (!appData.isEmpty()) {
		PKG("私有目录来自 QStandardPaths::AppDataLocation");
		return appData;
	}

	PKG("QStandardPaths::AppDataLocation 返回空（此时还没有 QCoreApplication 实例）→ 改走 /proc/self/cmdline");

	ifstream cmdline("/proc/self/cmdline", ios::binary);
	string pkg;
	getline(cmdline, pkg, '\0');
	if (pkg.empty()) {
		PKG("/proc/self/cmdline 也读不出包名 —— 数据无处可解");
		return QString();
	}
	return QStringLiteral("/data/user/0/%1/files").arg(QString::fromStdString(pkg));
}

/* 把资源树 resRoot 下的所有文件按同样的相对路径拷到磁盘 dstRoot。先整目录删掉，
 * 保证每次启动都是干净的：增量拷贝会让人拿着一份改之前的 .effect 去查现在的问题
 * （M1 实测踩过）。 */
bool extractTree(const QString &resRoot, const QString &dstRoot, int *count)
{
	QDir(dstRoot).removeRecursively();
	if (!QDir().mkpath(dstRoot)) {
		PKG("建不出目录 %s", dstRoot.toUtf8().constData());
		return false;
	}

	int n = 0;
	QDirIterator it(resRoot, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const QString src = it.next();
		const QString dst = dstRoot + src.mid(resRoot.length());
		QDir().mkpath(QFileInfo(dst).absolutePath());
		QFile::remove(dst);
		if (!QFile::copy(src, dst)) {
			PKG("拷贝失败 %s -> %s", src.toUtf8().constData(), dst.toUtf8().constData());
			return false;
		}
		++n;
	}
	*count = n;
	return true;
}

void unpack(const char *label, const QString &resRoot, const QString &dstRoot)
{
	int files = 0;
	const bool ok = extractTree(resRoot, dstRoot, &files);
	PKG("%s：%s，%d 个文件 -> %s", label, ok ? "解包成功" : "解包失败", files, dstRoot.toUtf8().constData());
}

/* 插件 .so：从 APK 的 assets/obs-plugins/ 解到 $ROOT/lib。
 *
 * 为什么是 assets 不是 Qt 资源：qt_add_resources 的 FILES 是 configure 期定死的清单，
 * 而插件 .so 是 build 期产物，configure 时还不存在。
 * 为什么不进 APK 的 lib/<abi>/：非 debuggable 包安装时系统只把 lib*.so 解到
 * nativeLibraryDir，我们的插件名不带 lib 前缀，一个都解不出来（实测 34 个只解出 20 个）。
 *
 * 按 build-apk.sh 生成的 MANIFEST 逐个解，不依赖 Qt 的 assets 目录列举能力。
 *
 * 按需加载的两条规矩（$ROOT/lib 是用户也能放插件的目录，所以不能粗暴对待）：
 *  ① **只动 MANIFEST 登记过的文件**（记在 .managed 里）。用户自己塞进来的 .so 一个都不碰。
 *     上一版登记过、这一版没有的会被删掉（插件从包里下架了）。
 *  ② **版本标记**：MANIFEST 的哈希写进 .bundle-stamp，内容没变且登记的文件都在就跳过解压。
 *     用 MANIFEST 哈希而不是版本号 —— 插件集合一变标记就变，比版本号准。 */
constexpr const char *kStampFile = "/.bundle-stamp";
constexpr const char *kManagedFile = "/.managed";

QStringList readLineList(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return {};
	return QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
}

void writeLineList(const QString &path, const QStringList &items)
{
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return;
	QByteArray out;
	for (const QString &s : items) {
		out += s.toUtf8();
		out += '\n';
	}
	f.write(out);
	f.close();
}

bool extractPluginBinaries(const QString &dstRoot)
{
	QFile manifest(QStringLiteral("assets:/obs-plugins/MANIFEST"));
	if (!manifest.open(QIODevice::ReadOnly)) {
		PKG("插件二进制：打不开 assets:/obs-plugins/MANIFEST —— 这一版的包里没有插件");
		return false;
	}
	const QByteArray manifestBytes = manifest.readAll();
	manifest.close();

	QStringList names;
	for (const QString &raw : QString::fromUtf8(manifestBytes).split('\n', Qt::SkipEmptyParts)) {
		const QString name = raw.trimmed();
		if (!name.isEmpty() && !name.contains('/'))
			names << name;
	}
	if (names.isEmpty()) {
		PKG("插件二进制：MANIFEST 是空的");
		return false;
	}

	if (!QDir().mkpath(dstRoot)) {
		PKG("插件二进制：建不出 %s", dstRoot.toUtf8().constData());
		return false;
	}

	const QString stamp =
		QString::fromLatin1(QCryptographicHash::hash(manifestBytes, QCryptographicHash::Sha1).toHex());

	/* 已经是最新？—— 标记一致，且登记的文件都还在（用户可能删了某个）*/
	bool upToDate = readLineList(dstRoot + kStampFile).value(0) == stamp;
	if (upToDate) {
		for (const QString &n : names) {
			if (!QFile::exists(dstRoot + '/' + n)) {
				upToDate = false;
				break;
			}
		}
	}
	if (upToDate) {
		PKG("插件二进制：已是最新（%d 个），跳过解压", int(names.size()));
		return true;
	}

	/* 上一版登记过、这一版没了的 → 删掉。用户自己的文件不在 .managed 里，不会被碰。 */
	const QStringList oldManaged = readLineList(dstRoot + kManagedFile);
	for (const QString &m : oldManaged) {
		if (!names.contains(m)) {
			QFile::remove(dstRoot + '/' + m);
			PKG("插件二进制：下架旧插件 %s", m.toUtf8().constData());
		}
	}

	int n = 0;
	for (const QString &name : names) {
		QFile src(QStringLiteral("assets:/obs-plugins/") + name);
		if (!src.open(QIODevice::ReadOnly)) {
			PKG("插件二进制：打不开 assets:/obs-plugins/%s", name.toUtf8().constData());
			return false;
		}
		const QByteArray data = src.readAll();
		src.close();

		/* 先写 .part 再改名：避免 dlopen 撞到写了一半的 .so（.part 不会被 *.so 匹配到）*/
		const QString tmp = dstRoot + '/' + name + QStringLiteral(".part");
		QFile out(tmp);
		if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			PKG("插件二进制：写不了 %s", tmp.toUtf8().constData());
			return false;
		}
		const qint64 wrote = out.write(data);
		out.close();
		if (wrote != data.size()) {
			PKG("插件二进制：%s 只写了 %lld/%d 字节", name.toUtf8().constData(), (long long)wrote,
			    int(data.size()));
			return false;
		}
		QFile::remove(dstRoot + '/' + name);
		if (!QFile::rename(tmp, dstRoot + '/' + name)) {
			PKG("插件二进制：改名失败 %s", name.toUtf8().constData());
			return false;
		}
		++n;
	}

	writeLineList(dstRoot + kManagedFile, names);
	writeLineList(dstRoot + kStampFile, QStringList{stamp});
	PKG("插件二进制：解出 %d 个 .so -> %s", n, dstRoot.toUtf8().constData());
	return true;
}

} // namespace

void EnsureAndroidPluginBinaries()
{
	/* 必须在 QApplication 之后调用 —— Qt 的 `assets:` 文件引擎是平台插件初始化时
	 * 注册的，而 PrepareAndroidRuntimePaths() 跑在 QApplication 之前。插件装载发生在
	 * OBSApp（QApplication 子类）构造之后，所以由 loader 在装载前调这里。 */
	static bool done = false;
	if (done)
		return;

	const char *root = getenv("OBS_ROOT_PATH");
	if (!root || !*root) {
		PKG("插件二进制：OBS_ROOT_PATH 未设置，跳过解压");
		return;
	}

	/* 只有成功才置位：失败的话下次调用还能再试。 */
	done = extractPluginBinaries(QString::fromUtf8(root) + "/lib");
}

void PrepareAndroidRuntimePaths()
{
	const QString files = privateFilesDir();
	if (files.isEmpty()) {
		PKG("FAIL 拿不到应用私有目录，OBS_ROOT_PATH 未设置");
		return;
	}

	/* 与壳工程同一个布局约定（libobs/obs-android.c 顶部注释）：
	 *   $ROOT/share/libobs/                 libobs 的 .effect / region.ini
	 *   $ROOT/share/obs/obs-studio/         前端数据（locale / themes / images）
	 *   $ROOT/share/obs/obs-plugins/<模块>/ 插件数据（本轮还没打插件，见 os-android.cmake）
	 *   $ROOT/lib/                          插件 .so（同上） */
	const QString root = files + "/obsroot";
	PKG("OBS_ROOT_PATH = %s", root.toUtf8().constData());

	if (!QDir().mkpath(root + "/lib"))
		PKG("FAIL 建不出 %s/lib", root.toUtf8().constData());

	unpack("libobs 数据 :/obsdata", QStringLiteral(":/obsdata"), root + "/share/libobs");
	unpack("前端数据 :/obsfrontenddata", QStringLiteral(":/obsfrontenddata"), root + "/share/obs/obs-studio");
	unpack("插件数据 :/obsplugindata", QStringLiteral(":/obsplugindata"), root + "/share/obs/obs-plugins");

	setenv("OBS_ROOT_PATH", root.toUtf8().constData(), 1);

	/* 判据不是"拷贝返回了 true"，而是消费者按各自的拼法真能看见这几个文件。 */
	struct Probe {
		const char *what;
		const char *rel;
	};
	static const Probe probes[] = {
		{"libobs 渲染要的第一张 effect", "share/libobs/default.effect"},
		{"前端 locale 清单", "share/obs/obs-studio/locale.ini"},
		{"默认语言翻译", "share/obs/obs-studio/locale/en-US.ini"},
	};
	for (const Probe &p : probes) {
		const QString path = root + '/' + p.rel;
		PKG("探针 %s：%s → %s", p.what, path.toUtf8().constData(), QFile::exists(path) ? "在" : "缺失");
	}

	/* 插件数据没有写死的文件名可探（哪些插件在包里由 plugins/CMakeLists.txt 的白名单决定），
	 * 所以探消费者自己拼出来的那个目录：obs-module.c 的 make_data_directory() 把
	 * <root>/share/obs/obs-plugins/%module% 里的 %module% 换成模块名当 data_path，
	 * obs_find_module_file() 再往里 stat 具体文件 —— 目录在不在、里有几张 effect，
	 * 就是这条链能不能走通的证据。 */
	QDir plugRoot(root + "/share/obs/obs-plugins");
	for (const QString &mod : plugRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
		const QDir m(plugRoot.filePath(mod));
		const QStringList files = m.entryList(QDir::Files, QDir::Name);
		PKG("探针插件数据 %s → %d 个顶层文件：%s", m.absolutePath().toUtf8().constData(), int(files.size()),
		    files.join(' ').toUtf8().constData());
	}
}
