#pragma once

#include <QFrame>

class QLabel;

class MetricCard final : public QFrame
{
    Q_OBJECT

public:
    explicit MetricCard(const QString& label,
                        const QString& value,
                        QWidget* parent = nullptr);

    void setValue(const QString& value);

private:
    QLabel* valueLabel_ = nullptr;
};
