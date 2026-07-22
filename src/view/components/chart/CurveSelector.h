#pragma once

#include <QHash>
#include <QWidget>

class QCheckBox;
class QColor;
class QHBoxLayout;

class CurveSelector final : public QWidget
{
    Q_OBJECT

public:
    explicit CurveSelector(QWidget* parent = nullptr);

    bool addCurve(
        const QString& curveId,
        const QString& displayName,
        const QColor& color,
        bool visible);
    bool removeCurve(const QString& curveId);
    void setCurveName(const QString& curveId, const QString& displayName);
    void setCurveVisible(const QString& curveId, bool visible);

signals:
    void curveVisibilityChanged(const QString& curveId, bool visible);

private slots:
    void onCurveToggled(bool checked);

private:
    QHBoxLayout* layout_ = nullptr;
    QHash<QString, QCheckBox*> checkBoxes_;
};
