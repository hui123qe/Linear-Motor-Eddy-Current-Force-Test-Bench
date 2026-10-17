#pragma once

#include "temperature/TemperatureTypes.h"

// 启动前设置；修改后重新编译，监控运行中不要修改。
inline constexpr int g_temperatureChannelCount = 4; // 固定采集 CH1～CH4。
inline bool g_temperatureUseSimulator = true;
inline QString g_temperatureHost = QStringLiteral("192.168.1.253");
inline int g_temperaturePort = 1030;
inline int g_temperatureUnitId = 1;
inline int g_temperaturePollIntervalMs = 500;
inline int g_temperatureRequestTimeoutMs = 1000;
inline int g_temperatureStaleAfterMs = 2000;

// 依次为触发温度、恢复温度、触发持续毫秒、恢复持续毫秒。
inline TemperatureLimitParameters g_temperatureWarning{30.0, 28.0, 1000, 1000};
inline TemperatureLimitParameters g_temperatureFault{35.0, 32.0, 1000, 1000};
inline QVector<double> g_temperatureSimulatedCelsius{25.0, 26.0, 27.0, 28.0};

// 温度故障先停止指定 Buffer，再 Kill 所有运动。
inline int g_temperatureFaultStopBuffer = 2;
