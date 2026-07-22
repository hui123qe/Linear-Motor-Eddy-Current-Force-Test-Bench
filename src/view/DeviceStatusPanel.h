#pragma once

#include <QWidget>

class DeviceStatusPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceStatusPanel(QWidget* parent = nullptr);

signals:
    void messageRequested(const QString& message);

private slots:
    void onInspectButtonClicked();

private:
    QWidget* createStateItem(const QString& name,
                             const QString& state,
                             const QString& detail,
                             const QString& level = QStringLiteral("ok")) const;
};
