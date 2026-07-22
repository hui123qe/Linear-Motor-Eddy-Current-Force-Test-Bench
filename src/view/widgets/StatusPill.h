#pragma once

#include <QLabel>

class StatusPill final : public QLabel
{
    Q_OBJECT

public:
    explicit StatusPill(const QString& text,
                        const QString& level = QStringLiteral("neutral"),
                        QWidget* parent = nullptr);

    void setLevel(const QString& level);
};
