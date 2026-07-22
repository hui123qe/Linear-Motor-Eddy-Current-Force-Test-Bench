#include "MainWindow.h"

#include "BottomStatusBar.h"
#include "DeviceStatusPanel.h"
#include "HeaderBar.h"
#include "Sidebar.h"
#include "pages/AlarmPage.h"
#include "pages/MaintenancePage.h"
#include "pages/ParametersPage.h"
#include "pages/RunRecordsPage.h"
#include "pages/WorkbenchPage.h"

#include <QHBoxLayout>
#include <QStackedWidget>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("高速涡流测试台"));
    resize(1680, 980);
    setMinimumSize(1366, 768);

    auto* root = new QWidget;
    root->setObjectName(QStringLiteral("applicationRoot"));
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(new HeaderBar(this));

    auto* body = new QWidget;
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    sidebar_ = new Sidebar(this);
    connect(sidebar_, &Sidebar::pageRequested, this, &MainWindow::setCurrentPage);
    bodyLayout->addWidget(sidebar_);

    pages_ = new QStackedWidget;
    pages_->setObjectName(QStringLiteral("pageStack"));

    auto* workbenchPage = new WorkbenchPage(this);
    pages_->addWidget(workbenchPage);

    auto* parametersPage = new ParametersPage(this);
    connect(parametersPage,
            &ParametersPage::configurationChanged,
            workbenchPage,
            &WorkbenchPage::setConfiguration);
    connect(parametersPage,
            &ParametersPage::configurationCleared,
            workbenchPage,
            &WorkbenchPage::clearConfiguration);
    connect(parametersPage,
            &ParametersPage::configurationLockChanged,
            workbenchPage,
            &WorkbenchPage::setConfigurationLocked);
    pages_->addWidget(parametersPage);
    pages_->addWidget(new MaintenancePage(this));
    pages_->addWidget(new RunRecordsPage(this));
    pages_->addWidget(new AlarmPage(this));
    bodyLayout->addWidget(pages_, 1);

    auto* devicePanel = new DeviceStatusPanel(this);
    bodyLayout->addWidget(devicePanel);

    rootLayout->addWidget(body, 1);

    rootLayout->addWidget(new BottomStatusBar(this));
    setCentralWidget(root);

    setCurrentPage(0);
}

void MainWindow::setCurrentPage(int index)
{
    if (pages_ != nullptr) {
        pages_->setCurrentIndex(index);
    }
    if (sidebar_ != nullptr) {
        sidebar_->setCurrentIndex(index);
    }
}
