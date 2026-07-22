#pragma once

#include <QWidget>

class MaintenancePage final : public QWidget
{
    Q_OBJECT

public:
    explicit MaintenancePage(QWidget* parent = nullptr);

signals:
    void messageRequested(const QString& message);

private slots:
    void onConnectButtonClicked();
    void onDisconnectButtonClicked();
    void onEnableButtonClicked();
    void onDisableButtonClicked();
    void onRelativeMoveClicked();
    void onAbsoluteMoveClicked();
    void onJogNegativePressed();
    void onJogPositivePressed();

private:
    void emitActionMessage(const QString& action);
};
