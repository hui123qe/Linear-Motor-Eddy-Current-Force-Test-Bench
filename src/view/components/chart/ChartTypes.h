#pragma once

#include <QColor>
#include <QString>

enum class ChartXAxisMode
{
    Numeric,
    DateTime
};

struct ChartCurveConfig
{
    QString id;
    QString displayName;
    QColor color = QColor(QStringLiteral("#1f78d1"));
    int lineWidth = 2;
    int maximumPointCount = 100000;
    bool visible = true;
};
