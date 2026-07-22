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
    void messageRequested(const QString& message);

private slots:
    void onSwitchAccountClicked();
    void onLockClicked();

private:
    NavigationBar* navigationBar_{};
};
