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

#include "OBSAndroidPath.hpp"

#include "OBSAndroidDisplay.hpp" // obsAndroidDisplayLog：与 3-3/3-4 其余探针同一个 tag，一条 logcat 看全

#include <qt-wrappers.hpp> // OBSMessageBox

#include <QApplication>
#include <QClipboard>

void obsAndroidShowLocalPath(QWidget *parent, const QString &title, const QString &path)
{
	QClipboard *clipboard = QApplication::clipboard();
	clipboard->setText(path);

	// 回读自证：Android 上剪贴板写入可能被平台规则挡掉，所以"已复制"这句话只在回读一致时才说。
	const bool copied = (clipboard->text(QClipboard::Clipboard) == path);
	OBS_PROBE_LOG("3-4 openUrl 替代：%s → %s | 剪贴板回读=%s", title.toUtf8().constData(),
			     path.toUtf8().constData(), copied ? "一致" : "不一致");

	OBSMessageBox::information(parent, title,
				   QString("%1\n\nAndroid 上没有文件管理器。%2")
					   .arg(path, copied ? "\n\n路径已复制到剪贴板，可粘贴到其他应用。" : ""));
}
