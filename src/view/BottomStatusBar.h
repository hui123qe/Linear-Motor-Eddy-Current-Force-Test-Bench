#pragma once

#include <QWidget>

class QLabel;

class BottomStatusBar final : public QWidget
{
    Q_OBJECT

public:
    explicit BottomStatusBar(QWidget* parent = nullptr);

private:
    void updateClock();

    QLabel* clockLabel_{};
};
