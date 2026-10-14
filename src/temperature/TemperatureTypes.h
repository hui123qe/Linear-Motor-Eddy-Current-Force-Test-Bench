#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QUuid>
#include <QVector>

#include <chrono>
#include <optional>

inline qint64 deviceObservationMonoMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

enum class DataQuality { Good, Invalid, Stale };
enum class TemperatureConnectionState { Disconnected, Connecting, WaitingForData, Ready, Error };

struct TemperatureLimitParameters
{
    double triggerCelsius = 0.0;
    double recoveryCelsius = 0.0;
    int triggerDelayMs = 0;
    int recoveryDelayMs = 0;
};

struct TemperatureConfiguration
{
    QString deviceId = QStringLiteral("temperature/main");
    QString backend = QStringLiteral("simulator");
    int deviceChannelCount = 4;
    QVector<int> channels{1, 2, 3, 4};
    QString role = QStringLiteral("tcp-client");
    QString protocol = QStringLiteral("modbus-tcp");
    QString host = QStringLiteral("192.168.1.253");
    int port = 1030;
    int unitId = 1;
    int pollIntervalMs = 500;
    int requestTimeoutMs = 1000;
    int staleAfterMs = 2000;
    bool limitsConfigured = false;
    TemperatureLimitParameters warning;
    TemperatureLimitParameters fault;
    QVector<double> simulatedCelsius{25.0, 26.0, 27.0, 28.0};
    QVector<int> simulatedInvalidChannels;
    double simulatedRiseCelsiusPerSecond = 0.0;
};

struct TemperatureChannelSample
{
    int channel = 0;
    double celsius = 0.0;
    DataQuality quality = DataQuality::Invalid;
    QString detail;
};

struct TemperatureSnapshot
{
    QString deviceId;
    QUuid sessionId;
    quint64 sequence = 0;
    QDateTime sampledAtUtc;
    qint64 observedMonoMs = 0;
    QVector<TemperatureChannelSample> channels;
    std::optional<double> sessionPeakCelsius;
    int sessionPeakChannel = 0;
    std::optional<double> experimentPeakCelsius;
    int experimentPeakChannel = 0;
};

struct TemperatureExperimentSummary
{
    QString experimentName;
    QString backend;
    QVector<int> channels;
    QDateTime startedAtUtc;
    QDateTime finishedAtUtc;
    std::optional<double> maximumCelsius;
    int maximumChannel = 0;
    QDateTime maximumAtUtc;
    quint64 goodScanCount = 0;
    quint64 incompleteScanCount = 0;
};

Q_DECLARE_METATYPE(DataQuality)
Q_DECLARE_METATYPE(TemperatureConnectionState)
Q_DECLARE_METATYPE(TemperatureConfiguration)
Q_DECLARE_METATYPE(TemperatureSnapshot)
Q_DECLARE_METATYPE(TemperatureExperimentSummary)
