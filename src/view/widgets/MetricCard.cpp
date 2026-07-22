#include "MetricCard.h"

#include <QLabel>
#include <QVBoxLayout>

namespace {

QLabel* makeLabel(const QString& text, const char* objectName)
{
    auto* label = new QLabel(text);
    label->setObjectName(QString::fromLatin1(objectName));
    return label;
}

} // namespace

MetricCard::MetricCard(const QString& label,
                       const QString& value,
                       QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("metricCard"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 11, 14, 11);
    layout->setSpacing(3);
    layout->addWidget(makeLabel(label, "metricLabel"));
    valueLabel_ = makeLabel(value, "metricValue");
    layout->addWidget(valueLabel_);
}

void MetricCard::setValue(const QString& value)
{
    valueLabel_->setText(value);
}
