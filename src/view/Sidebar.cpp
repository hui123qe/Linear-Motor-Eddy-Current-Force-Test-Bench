#include "Sidebar.h"

#include "NavigationBar.h"
#include "widgets/ViewHelpers.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
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
    layout->addWidget(ViewHelpers::makeDivider());

    auto* account = new QFrame;
    account->setObjectName(QStringLiteral("accountCard"));
    auto* accountLayout = new QVBoxLayout(account);
    accountLayout->setContentsMargins(12, 12, 12, 12);
    accountLayout->setSpacing(5);
    accountLayout->addWidget(ViewHelpers::makeLabel(QStringLiteral("当前账户"), "accountCaption"));
    accountLayout->addWidget(ViewHelpers::makeLabel(QStringLiteral("现场操作员"), "accountName"));
    accountLayout->addWidget(ViewHelpers::makeLabel(QStringLiteral("操作员 · 标准权限"), "accountRole"));

    auto* accountActions = new QHBoxLayout;
    auto* switchAccount = ViewHelpers::makeButton(QStringLiteral("切换账户"));
    auto* lock = ViewHelpers::makeButton(QStringLiteral("锁定"));
    switchAccount->setProperty("compact", true);
    lock->setProperty("compact", true);
    connect(switchAccount,
            &QPushButton::clicked,
            this,
            &Sidebar::onSwitchAccountClicked);
    connect(lock, &QPushButton::clicked, this, &Sidebar::onLockClicked);
    accountActions->addWidget(switchAccount);
    accountActions->addWidget(lock);
    accountLayout->addLayout(accountActions);
    layout->addWidget(account);
}

void Sidebar::setCurrentIndex(int index)
{
    navigationBar_->setCurrentIndex(index);
}

void Sidebar::onSwitchAccountClicked()
{
    emit messageRequested(QStringLiteral("账户切换入口（演示）"));
}

void Sidebar::onLockClicked()
{
    emit messageRequested(QStringLiteral("界面已锁定（演示）"));
}
