#include "RunRecordsPage.h"

#include "../widgets/ViewHelpers.h"

#include <QDate>
#include <QDateEdit>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QTableWidget>
#include <QVBoxLayout>

RunRecordsPage::RunRecordsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(12);

    auto* heading = new QHBoxLayout;
    auto* titleBox = new QVBoxLayout;
    titleBox->setSpacing(1);
    titleBox->addWidget(ViewHelpers::makeLabel(QStringLiteral("运行记录"), "pageTitle"));
    titleBox->addWidget(ViewHelpers::makeLabel(QStringLiteral("追溯关键操作、流程节点与状态变化"), "pageDescription"));
    heading->addLayout(titleBox);
    heading->addStretch();
    heading->addWidget(ViewHelpers::makeButton(QStringLiteral("导出筛选结果"), QStringLiteral("primary")));
    layout->addLayout(heading);

    auto* filter = new QFrame;
    filter->setObjectName(QStringLiteral("filterBar"));
    auto* filterLayout = new QHBoxLayout(filter);
    filterLayout->setContentsMargins(14, 10, 14, 10);
    filterLayout->setSpacing(10);
    QDateEdit* startDateInput = new QDateEdit;
    startDateInput->setCalendarPopup(true);
    startDateInput->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    startDateInput->setDate(QDate::currentDate());
    QDateEdit* endDateInput = new QDateEdit;
    endDateInput->setCalendarPopup(true);
    endDateInput->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    endDateInput->setDate(QDate::currentDate());
    startDateInput->setMaximumDate(endDateInput->date());
    endDateInput->setMinimumDate(startDateInput->date());
    connect(startDateInput,
            &QDateEdit::dateChanged,
            endDateInput,
            &QDateEdit::setMinimumDate);
    connect(endDateInput,
            &QDateEdit::dateChanged,
            startDateInput,
            &QDateEdit::setMaximumDate);
    filterLayout->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("日期"), "fieldLabel"),
        0,
        Qt::AlignVCenter);
    filterLayout->addWidget(startDateInput);
    filterLayout->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("至"), "fieldLabel"),
        0,
        Qt::AlignVCenter);
    filterLayout->addWidget(endDateInput);
    auto* keyword = new QLineEdit;
    keyword->setPlaceholderText(QStringLiteral("搜索操作内容或关联实验"));
    filterLayout->addWidget(
        ViewHelpers::makeLabel(QStringLiteral("关键词"), "fieldLabel"),
        0,
        Qt::AlignVCenter);
    filterLayout->addWidget(keyword, 1);
    layout->addWidget(filter);

    auto* table = ViewHelpers::makeTable(
        {QStringLiteral("时间"),
         QStringLiteral("操作员"),
         QStringLiteral("操作内容"),
         QStringLiteral("关联实验")},
        {{QStringLiteral("2026-07-06 10:38:21"),
          QStringLiteral("现场操作员"),
          QStringLiteral("执行开机自检"),
          QStringLiteral("T-003")},
         {QStringLiteral("2026-07-06 10:36:08"),
          QStringLiteral("现场操作员"),
          QStringLiteral("确认并锁定参数"),
          QStringLiteral("T-003")},
         {QStringLiteral("2026-07-06 10:32:19"),
          QStringLiteral("系统"),
          QStringLiteral("通信状态变更为在线"),
          QStringLiteral("--")},
         {QStringLiteral("2026-07-06 10:31:57"),
          QStringLiteral("现场操作员"),
          QStringLiteral("操作员登录"),
          QStringLiteral("--")}});
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(table, 1);
}
