#pragma once

class QQmlApplicationEngine;

namespace lens {

class AppSettings;
class ChatController;

// --e2e-chat GUI 端到端诊断（详见 E2eDriver.cpp 顶部说明）
void runE2eIfNeeded(QQmlApplicationEngine &engine, ChatController *chat, AppSettings *settings);

} // namespace lens
