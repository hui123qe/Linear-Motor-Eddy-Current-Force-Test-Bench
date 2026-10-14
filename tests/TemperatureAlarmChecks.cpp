#include "../src/alarm/AlarmMonitor.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {
class MonitorCase
{
public:
    MonitorCase()
    {
        config.channels = {2, 3, 4}; config.limitsConfigured = true;
        config.warning = {50.0, 45.0, 300, 200}; config.fault = {80.0, 75.0, 0, 200};
        QObject::connect(&monitor, &AlarmMonitor::conditionEvaluated, &monitor,
            [this](const ConditionReport& report) { reports[report.code] = report; });
        monitor.configure(config); monitor.setSourceSession(config.deviceId, session);
        base = deviceObservationMonoMs() - 1500;
    }
    void sample(qint64 offset, double second, double third, double fourth, bool thirdValid = true,
                quint64 forcedSequence = 0, QUuid forcedSession = {})
    {
        TemperatureSnapshot snapshot;
        snapshot.deviceId = config.deviceId; snapshot.sessionId = forcedSession.isNull() ? session : forcedSession;
        snapshot.sequence = forcedSequence == 0 ? ++sequence : forcedSequence;
        snapshot.observedMonoMs = base + offset; snapshot.sampledAtUtc = QDateTime::currentDateTimeUtc();
        snapshot.channels = {{2, second, DataQuality::Good, {}},
            {3, third, thirdValid ? DataQuality::Good : DataQuality::Invalid, {}}, {4, fourth, DataQuality::Good, {}}};
        DeviceUpdate update; update.sourceId = config.deviceId; update.sessionId = snapshot.sessionId;
        update.points.insert(PointId::TemperatureBatch, {QVariant::fromValue(snapshot), DataQuality::Good,
            snapshot.sampledAtUtc, snapshot.observedMonoMs, snapshot.sequence}); monitor.update(update);
    }
    ConditionState state(const QString& code) const { return reports.value(code).state; }
    AlarmMonitor monitor;
    TemperatureConfiguration config;
    QMap<QString, ConditionReport> reports;
    QUuid session = QUuid::createUuid();
    qint64 base = 0;
    quint64 sequence = 0;
};
bool check(bool condition, const char* message)
{
    if (!condition) { QTextStream(stderr) << "FAILED: " << message << '\n'; }
    return condition;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    bool passed = true;
    {
        MonitorCase test;
        test.sample(0, 30.0, 60.0, 40.0);
        passed &= check(test.state("TEMP_WARNING") == ConditionState::Normal, "brief exceedance must not trigger warning");
        test.sample(350, 30.0, 60.0, 40.0);
        passed &= check(test.state("TEMP_WARNING") == ConditionState::Abnormal, "sustained maximum must trigger warning");
        passed &= check(test.reports.value("TEMP_WARNING").details.value("maximumChannel").toInt() == 3, "configured channel identity must be preserved");
        test.sample(450, 85.0, 60.0, 40.0);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Abnormal, "fault remains independent of active warning");
        test.sample(500, 40.0, 40.0, 40.0);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Abnormal, "recovery must respect delay");
        test.sample(750, 40.0, 40.0, 40.0);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Normal, "fresh recovery can clear condition despite past peak");
    }
    {
        MonitorCase test;
        test.sample(0, 90.0, 20.0, 20.0, false);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Abnormal, "known high channel must trigger even with incomplete coverage");
        test.sample(100, 20.0, 20.0, 20.0, false);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Unknown, "missing probe must not prove recovery");
        test.sample(350, 20.0, 20.0, 20.0);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Abnormal, "recovery delay starts with full coverage");
        test.sample(600, 20.0, 20.0, 20.0);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Normal, "full valid recovery succeeds");
    }
    {
        MonitorCase test;
        test.sample(0, -30.5, -11.9, -5.0);
        passed &= check(test.reports.value("TEMP_WARNING").details.value("maximumCelsius").toDouble() == -5.0, "negative temperatures must retain sign");
        test.sample(300, 90.0, 90.0, 90.0, true, 1);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Normal, "duplicate sequence must be ignored");
        test.sample(400, 90.0, 90.0, 90.0, true, 0, QUuid::createUuid());
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Normal, "unauthorized session must be ignored");
        test.monitor.setSourceSession(test.config.deviceId, QUuid::createUuid());
        test.sample(500, 90.0, 90.0, 90.0);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Unknown, "late old-session response must not restore current source");
    }
    {
        MonitorCase test;
        test.base = deviceObservationMonoMs() - 3000;
        test.sample(0, 90.0, 90.0, 90.0); test.monitor.tick();
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Unknown, "stale high cache must not prove current overtemperature");
    }
    {
        MonitorCase test;
        // Synthetic observation times cover the requested 3 s / 10 s cycle
        // without sleeping or connecting to any device.
        test.config.fault = {80.0, 75.0, 3000, 10000};
        test.config.staleAfterMs = 60000;
        test.monitor.configure(test.config);
        test.base = deviceObservationMonoMs() - 30000;
        test.sample(0, 86.0, 20.0, 20.0);
        test.sample(2999, 86.0, 20.0, 20.0);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Normal, "three-second trigger must not fire early");
        test.sample(3000, 86.0, 20.0, 20.0);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Abnormal, "three-second exceedance triggers fault");
        test.sample(4000, 78.0, 20.0, 20.0);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Abnormal, "78 C must retain fault until recovery threshold is reached");
        test.sample(5000, 75.0, 20.0, 20.0);
        test.sample(14999, 75.0, 20.0, 20.0);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Abnormal, "ten-second recovery must not complete early");
        test.sample(15000, 75.0, 20.0, 20.0);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Normal, "ten seconds at 75 C recovers fault");
        test.sample(16000, 86.0, 20.0, 20.0);
        test.sample(18999, 86.0, 20.0, 20.0);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Normal, "recurrence must satisfy a fresh trigger delay");
        test.sample(19000, 86.0, 20.0, 20.0);
        passed &= check(test.state("TEMP_FAULT") == ConditionState::Abnormal, "sustained exceedance after recovery triggers again");
    }
    QTextStream(stdout) << (passed ? "Temperature alarm checks passed\n" : "Temperature alarm checks failed\n");
    return passed ? 0 : 1;
}
