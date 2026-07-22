#include "CurveSelector.h"

#include <QCheckBox>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>

CurveSelector::CurveSelector(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("chartCurveSelector"));
    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(6, 2, 6, 2);
    layout_->setSpacing(12);
    layout_->addWidget(new QLabel(QStringLiteral("曲线："), this));
    layout_->addStretch();
}

bool CurveSelector::addCurve(
    const QString& curveId,
    const QString& displayName,
    const QColor& color,
    bool visible)
{
    if (curveId.isEmpty() || checkBoxes_.contains(curveId)) {
        return false;
    }

    auto* checkBox = new QCheckBox(displayName, this);
    checkBox->setChecked(visible);
    checkBox->setProperty("curveId", curveId);
    checkBox->setStyleSheet(QStringLiteral("QCheckBox { color: %1; }").arg(color.name()));
    layout_->insertWidget(layout_->count() - 1, checkBox);
    checkBoxes_.insert(curveId, checkBox);

    connect(checkBox,
            &QCheckBox::toggled,
            this,
            &CurveSelector::onCurveToggled);
    return true;
}

void CurveSelector::onCurveToggled(bool checked)
{
    const QCheckBox* checkBox = qobject_cast<QCheckBox*>(sender());
    if (checkBox == nullptr) {
        return;
    }

    emit curveVisibilityChanged(
        checkBox->property("curveId").toString(), checked);
}

bool CurveSelector::removeCurve(const QString& curveId)
{
    QCheckBox* checkBox = checkBoxes_.take(curveId);
    if (checkBox == nullptr) {
        return false;
    }

    layout_->removeWidget(checkBox);
    checkBox->deleteLater();
    return true;
}

void CurveSelector::setCurveName(const QString& curveId, const QString& displayName)
{
    QCheckBox* checkBox = checkBoxes_.value(curveId);
    if (checkBox != nullptr) {
        checkBox->setText(displayName);
    }
}

void CurveSelector::setCurveVisible(const QString& curveId, bool visible)
{
    QCheckBox* checkBox = checkBoxes_.value(curveId);
    if (checkBox == nullptr) {
        return;
    }

    const QSignalBlocker blocker(checkBox);
    checkBox->setChecked(visible);
}
