#pragma once

#include <QWidget>

class QTableWidget;
class QComboBox;

class AlarmPage final : public QWidget
{
    Q_OBJECT

public:
    explicit AlarmPage(QWidget* parent = nullptr);
private:
    void refresh();
    void acknowledgeSelected();
    QTableWidget* active_ = nullptr;
    QTableWidget* history_ = nullptr;
    QComboBox* severityFilter_ = nullptr;
    QComboBox* sourceFilter_ = nullptr;
};
