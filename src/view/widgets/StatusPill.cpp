#include "StatusPill.h"

#include <QStyle>

StatusPill::StatusPill(const QString& text, const QString& level, QWidget* parent)
    : QLabel(text, parent)
{
    setObjectName(QStringLiteral("statusPill"));
    setAlignment(Qt::AlignCenter);
    setLevel(level);
}

void StatusPill::setLevel(const QString& level)
{
    setProperty("level", level);
    style()->unpolish(this);
    style()->polish(this);
}
