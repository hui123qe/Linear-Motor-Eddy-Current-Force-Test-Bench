#pragma once

#include <QWidget>

class NavigationBar;

class Sidebar final : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);

    void setCurrentIndex(int index);

signals:
    void pageRequested(int index);

private:
    NavigationBar* navigationBar_{};
};
