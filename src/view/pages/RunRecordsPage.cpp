#include "RunRecordsPage.h"

#include "../widgets/ViewHelpers.h"

#include <QComboBox>
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
    auto* keyword = new QLineEdit;
    keyword->setPlaceholderText(QStringLiteral("操作内容或对象"));
    auto* eventType = new QComboBox;
    eventType->addItems({QStringLiteral("全部事件"), QStringLiteral("参数"), QStringLiteral("控制"), QStringLiteral("状态"), QStringLiteral("数据")});
    auto* batch = new QComboBox;
    batch->addItems({QStringLiteral("全部批次"), QStringLiteral("涡流-20260706-001"), QStringLiteral("涡流-20260705-004")});
    filterLayout->addWidget(ViewHelpers::makeFilterField(QStringLiteral("关键词"), keyword), 2);
    filterLayout->addWidget(ViewHelpers::makeFilterField(QStringLiteral("事件类型"), eventType));
    filterLayout->addWidget(ViewHelpers::makeFilterField(QStringLiteral("关联批次"), batch), 2);
    filterLayout->addWidget(ViewHelpers::makeLabel(QStringLiteral("条件改变后自动筛选"), "fieldLabel"), 0, Qt::AlignBottom);
    layout->addWidget(filter);

    auto* table = ViewHelpers::makeTable(
        {QStringLiteral("时间"), QStringLiteral("操作员"), QStringLiteral("事件类型"), QStringLiteral("操作对象"),
         QStringLiteral("操作内容"), QStringLiteral("关联批次"), QStringLiteral("关联试验")},
        {{QStringLiteral("2026-07-06 10:38:21"), QStringLiteral("现场操作员"), QStringLiteral("设备自检"), QStringLiteral("安全链"), QStringLiteral("执行开机自检"), QStringLiteral("涡流-20260706-001"), QStringLiteral("T-003")},
         {QStringLiteral("2026-07-06 10:36:08"), QStringLiteral("现场操作员"), QStringLiteral("参数"), QStringLiteral("配置模板"), QStringLiteral("确认并锁定参数"), QStringLiteral("涡流-20260706-001"), QStringLiteral("T-003")},
         {QStringLiteral("2026-07-06 10:35:44"), QStringLiteral("现场操作员"), QStringLiteral("批次"), QStringLiteral("批次设定"), QStringLiteral("切换当前批次"), QStringLiteral("涡流-20260706-001"), QStringLiteral("--")},
         {QStringLiteral("2026-07-06 10:32:19"), QStringLiteral("系统"), QStringLiteral("状态"), QStringLiteral("控制器通信"), QStringLiteral("通信状态变更为在线"), QStringLiteral("--"), QStringLiteral("--")},
         {QStringLiteral("2026-07-06 10:31:57"), QStringLiteral("现场操作员"), QStringLiteral("登录"), QStringLiteral("上位机"), QStringLiteral("操作员登录"), QStringLiteral("--"), QStringLiteral("--")}});
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    layout->addWidget(table, 1);
}
