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

// 3-5（plan.md §五）：把"OBS 有没有输出在跑"翻译成 Android 前台服务的起停。
// 与 OBSAndroidDisplay 同一类宿主桥，方向相反：那个是把 Surface 递给 obs_display，
// 这个是把 Qt 侧的 Streaming/Recording 信号递给 Java 的 Service。
#pragma once

#include <QString>

// running=true → 起前台服务（state 进通知栏文案，如 "streaming"/"recording"/"streaming+recording"）；
// running=false → 停。幂等：重复 true 只刷文案，重复 false 不再惊动 Java。
// 返回是否让 Java 侧认了这次请求；detail 带一行原文（含 Java describe() 的计数器）供 blog。
bool obsAndroidKeepAliveSetRunning(bool running, const char *state, QString *detail = nullptr);

// 一行状态快照（起停请求数 / running / wakelock / 最后一次错误），给日志与冒烟用。
QString obsAndroidKeepAliveStatus();
