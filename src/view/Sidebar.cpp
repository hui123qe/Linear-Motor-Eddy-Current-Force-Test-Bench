#include "Sidebar.h"

#include "NavigationBar.h"
#include "widgets/ViewHelpers.h"

#include <QVBoxLayout>

namespace {

constexpr int kNavWidth = 210;

} // namespace

Sidebar::Sidebar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sidebar"));
    setFixedWidth(kNavWidth);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 18, 12, 14);
    layout->setSpacing(8);

    layout->addWidget(ViewHelpers::makeLabel(QStringLiteral("功能导航"), "navCaption"));
    navigationBar_ = new NavigationBar(this);
    connect(navigationBar_, &NavigationBar::currentIndexChanged, this, &Sidebar::pageRequested);
    layout->addWidget(navigationBar_);
    layout->addStretch();
}

void Sidebar::setCurrentIndex(int index)
{
    navigationBar_->setCurrentIndex(index);
}
