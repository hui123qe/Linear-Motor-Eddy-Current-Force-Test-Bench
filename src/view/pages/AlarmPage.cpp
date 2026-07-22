#include "AlarmPage.h"

#include "../widgets/MetricCard.h"
#include "../widgets/ViewHelpers.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

AlarmPage::AlarmPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(12);

    auto* heading = new QHBoxLayout;
    auto* titleBox = new QVBoxLayout;
    titleBox->setSpacing(1);
    titleBox->addWidget(ViewHelpers::makeLabel(QStringLiteral("报警记录"), "pageTitle"));
    titleBox->addWidget(ViewHelpers::makeLabel(QStringLiteral("区分报警触发、确认、恢复与关闭状态"), "pageDescription"));
    heading->addLayout(titleBox);
    heading->addStretch();
    heading->addWidget(ViewHelpers::makeButton(QStringLiteral("导出报警"), QStringLiteral("primary")));
    layout->addLayout(heading);

    auto* metrics = new QHBoxLayout;
    metrics->addWidget(new MetricCard(QStringLiteral("当前未恢复"), QStringLiteral("0")));
    metrics->addWidget(new MetricCard(QStringLiteral("今日报警"), QStringLiteral("2")));
    metrics->addWidget(new MetricCard(QStringLiteral("最高等级"), QStringLiteral("预警")));
    metrics->addWidget(new MetricCard(QStringLiteral("待补充备注"), QStringLiteral("1")));
    layout->addLayout(metrics);

    auto* tabs = new QTabWidget;
    auto* active = new QWidget;
    auto* activeLayout = new QVBoxLayout(active);
    auto* empty = new QFrame;
    empty->setObjectName(QStringLiteral("emptyState"));
    auto* emptyLayout = new QVBoxLayout(empty);
    auto* emptyTitle = ViewHelpers::makeLabel(QStringLiteral("当前无活动报警"), "emptyTitle");
    emptyTitle->setAlignment(Qt::AlignCenter);
    auto* emptyCopy = ViewHelpers::makeLabel(QStringLiteral("安全链、控制通信与关键传感器状态正常"), "emptyCopy");
    emptyCopy->setAlignment(Qt::AlignCenter);
    emptyLayout->addStretch();
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyCopy);
    emptyLayout->addStretch();
    activeLayout->addWidget(empty);
    tabs->addTab(active, QStringLiteral("当前报警  0"));

    auto* history = new QWidget;
    auto* historyLayout = new QVBoxLayout(history);
    auto* filterRow = new QHBoxLayout;
    for (const auto& values : QList<QStringList>{
             {QStringLiteral("全部等级"), QStringLiteral("提示"), QStringLiteral("预警"), QStringLiteral("故障")},
             {QStringLiteral("全部设备"), QStringLiteral("安全链"), QStringLiteral("驱动器"), QStringLiteral("传感器")},
             {QStringLiteral("全部状态"), QStringLiteral("已确认"), QStringLiteral("已恢复"), QStringLiteral("已关闭")}}) {
        auto* combo = new QComboBox;
        combo->addItems(values);
        filterRow->addWidget(combo);
    }
    filterRow->addStretch();
    filterRow->addWidget(ViewHelpers::makeButton(QStringLiteral("筛选")));
    historyLayout->addLayout(filterRow);
    historyLayout->addWidget(ViewHelpers::makeTable(
        {QStringLiteral("触发时间"), QStringLiteral("等级"), QStringLiteral("设备/传感器"), QStringLiteral("报警内容"),
         QStringLiteral("确认状态"), QStringLiteral("恢复时间"), QStringLiteral("关联试验")},
        {{QStringLiteral("2026-07-06 09:54:12"), QStringLiteral("预警"), QStringLiteral("电机温度"), QStringLiteral("温度接近预警阈值"), QStringLiteral("已确认"), QStringLiteral("09:57:08"), QStringLiteral("T-002")},
         {QStringLiteral("2026-07-06 09:22:46"), QStringLiteral("提示"), QStringLiteral("冷却水流量"), QStringLiteral("流量瞬时波动"), QStringLiteral("已确认"), QStringLiteral("09:22:51"), QStringLiteral("T-001")},
         {QStringLiteral("2026-07-05 16:41:07"), QStringLiteral("故障"), QStringLiteral("防护门"), QStringLiteral("测试准备阶段防护门打开"), QStringLiteral("已确认"), QStringLiteral("16:42:35"), QStringLiteral("T-009")}}));
    tabs->addTab(history, QStringLiteral("历史报警  28"));
    layout->addWidget(tabs, 1);
}
