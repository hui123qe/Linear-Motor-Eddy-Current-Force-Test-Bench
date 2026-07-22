#include "QExclusiveActionGroup.h"

#include <QAction>

QExclusiveActionGroup::QExclusiveActionGroup(QObject* parent)
    : QObject(parent)
{
}

void QExclusiveActionGroup::addAction(QAction* action)
{
    if (action == nullptr || actions_.contains(action)) {
        return;
    }

    action->setCheckable(true);
    connect(action, &QAction::triggered, this, &QExclusiveActionGroup::onActionTriggered);
    actions_.append(action);
}

void QExclusiveActionGroup::removeAction(QAction* action)
{
    if (action == nullptr) {
        return;
    }

    disconnect(action, &QAction::triggered, this, &QExclusiveActionGroup::onActionTriggered);
    actions_.removeAll(action);
}

QList<QAction*> QExclusiveActionGroup::actions() const
{
    return actions_;
}

QAction* QExclusiveActionGroup::checkedAction() const
{
    for (QAction* action : actions_) {
        if (action->isChecked()) {
            return action;
        }
    }
    return nullptr;
}

void QExclusiveActionGroup::onActionTriggered()
{
    QAction* clickedAction = qobject_cast<QAction*>(sender());
    if (clickedAction == nullptr) {
        return;
    }

    if (clickedAction->isChecked()) {
        for (QAction* action : actions_) {
            action->setChecked(false);
        }
        clickedAction->setChecked(true);
    } else {
        clickedAction->setChecked(false);
    }
}
