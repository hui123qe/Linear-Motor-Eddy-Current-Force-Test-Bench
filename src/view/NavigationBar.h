#pragma once

#include <QWidget>

class QButtonGroup;

class NavigationBar final : public QWidget
{
    Q_OBJECT

public:
    explicit NavigationBar(QWidget* parent = nullptr);

    void setCurrentIndex(int index);

signals:
    void currentIndexChanged(int index);

private slots:
    void onNavigationButtonClicked(int index);

private:
    QButtonGroup* navigationGroup_{};
};
