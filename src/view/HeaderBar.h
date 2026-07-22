#pragma once

#include <QWidget>

class HeaderBar final : public QWidget
{
    Q_OBJECT

public:
    explicit HeaderBar(QWidget* parent = nullptr);

private slots:
    void onEmergencyStopClicked();
};
