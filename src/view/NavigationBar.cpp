#include "NavigationBar.h"

#include <QButtonGroup>
#include <QList>
#include <QPair>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QToolButton* makeNavButton(const QString& text, const QString& symbol, int index)
{
    auto* button = new QToolButton;
    button->setText(symbol + QStringLiteral("   ") + text);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setProperty("navIndex", index);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return button;
}

} // namespace

NavigationBar::NavigationBar(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    navigationGroup_ = new QButtonGroup(this);
    navigationGroup_->setExclusive(true);
    connect(navigationGroup_,
            &QButtonGroup::idClicked,
            this,
            &NavigationBar::onNavigationButtonClicked);

    const QList<QPair<QString, QString>> items{
        {QStringLiteral("测试工作台"), QStringLiteral("●")},
        {QStringLiteral("参数设置"), QStringLiteral("≡")},
        {QStringLiteral("维护界面"), QStringLiteral("◆")},
        {QStringLiteral("运行记录"), QStringLiteral("●")},
        {QStringLiteral("报警记录"), QStringLiteral("■")},
    };

    for (int index = 0; index < items.size(); ++index) {
        auto* button = makeNavButton(items.at(index).first, items.at(index).second, index);
        navigationGroup_->addButton(button, index);
        layout->addWidget(button);
    }
}

void NavigationBar::setCurrentIndex(int index)
{
    if (navigationGroup_ != nullptr && navigationGroup_->button(index) != nullptr) {
        navigationGroup_->button(index)->setChecked(true);
    }
}

void NavigationBar::onNavigationButtonClicked(int index)
{
    emit currentIndexChanged(index);
}
