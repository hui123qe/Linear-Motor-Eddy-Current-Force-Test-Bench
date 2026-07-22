#pragma once

#include <QList>
#include <QObject>

class QAction;

class QExclusiveActionGroup final : public QObject
{
    Q_OBJECT

public:
    explicit QExclusiveActionGroup(QObject* parent = nullptr);

    void addAction(QAction* action);
    void removeAction(QAction* action);
    [[nodiscard]] QList<QAction*> actions() const;
    [[nodiscard]] QAction* checkedAction() const;

private slots:
    void onActionTriggered();

private:
    QList<QAction*> actions_;
};
