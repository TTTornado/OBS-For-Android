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

// 3-4（plan.md §五）：桌面平台那 6 处 "QDesktopServices::openUrl(本地路径)" 的 Android 替代。
// 实测依据（34l / emulator-5554 / 文件→显示录像，21:21:53）：Qt 会把本地路径换成
// content://com.obsproject.studio.qtprovider/... 再发 ACTION_VIEW，系统把它派给了
// com.android.packageinstaller/.InstallStart —— 装包器秒退，用户侧看到的是"点了没反应"。
// Android 上没有"桌面文件管理器"这个概念，所以改成：把绝对路径显示出来 + 复制进剪贴板。
#pragma once

#include <QString>

class QWidget;

// 模态框显示 path（标题用 title），并把 path 写进剪贴板。
// 这个框会走 3-3 的遮挡逻辑（弹出时销毁预览面，关闭后自动重建），不需要额外处理。
void obsAndroidShowLocalPath(QWidget *parent, const QString &title, const QString &path);
