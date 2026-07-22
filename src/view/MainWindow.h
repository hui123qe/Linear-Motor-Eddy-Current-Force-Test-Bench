#pragma once

#include <QMainWindow>

class QStackedWidget;
class Sidebar;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void setCurrentPage(int index);

    Sidebar* sidebar_{};
    QStackedWidget* pages_{};
};
