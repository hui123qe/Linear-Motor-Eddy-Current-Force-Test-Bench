#pragma once

#include "AlarmTypes.h"

#include <QObject>
#include <memory>

class AlarmMonitor final : public QObject
{
    Q_OBJECT
public:
    explicit AlarmMonitor(QObject* parent = nullptr);
    ~AlarmMonitor() override;
    void configure(const TemperatureConfiguration& configuration);
    void setSourceSession(const QString& sourceId, const QUuid& sessionId);
    void update(const DeviceUpdate& update);
    void tick();
signals:
    void conditionEvaluated(const ConditionReport& report);
private:
    void updateTemperature(const DeviceUpdate& update, const PointValue& point, qint64 now);
    void updateSafetyCircuit(const DeviceUpdate& update, const PointValue& point, qint64 now);
    struct State;
    std::unique_ptr<State> state_;
};
