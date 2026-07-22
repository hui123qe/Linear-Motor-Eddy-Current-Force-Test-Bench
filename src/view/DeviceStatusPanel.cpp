#include "DeviceStatusPanel.h"

#include "widgets/StatusPill.h"
#include "widgets/ViewHelpers.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

constexpr int kDevicePanelWidth = 310;

} // namespace

DeviceStatusPanel::DeviceStatusPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("deviceRail"));
    setFixedWidth(kDevicePanelWidth);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 14, 14, 14);
    outer->setSpacing(10);

    auto* titleRow = new QHBoxLayout;
    auto* titleBox = new QVBoxLayout;
    titleBox->setSpacing(1);
    titleBox->addWidget(ViewHelpers::makeLabel(QStringLiteral("设备状态"), "railTitle"));
    titleBox->addWidget(ViewHelpers::makeLabel(QStringLiteral("状态面板始终可见"), "railSubtitle"));
    titleRow->addLayout(titleBox);
    titleRow->addStretch();
    titleRow->addWidget(new StatusPill(QStringLiteral("自检通过"), QStringLiteral("ok"), this));
    outer->addLayout(titleRow);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget;
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 2, 0);
    contentLayout->setSpacing(10);

    auto addGroup = [this, contentLayout](const QString& name, const QList<QStringList>& items) {
        auto* group = new QFrame;
        group->setObjectName(QStringLiteral("deviceGroup"));
        auto* layout = new QVBoxLayout(group);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(7);
        layout->addWidget(ViewHelpers::makeLabel(name, "deviceGroupTitle"));
        for (const auto& item : items) {
            layout->addWidget(createStateItem(item.at(0), item.at(1), item.at(2), item.at(3)));
        }
        contentLayout->addWidget(group);
    };

    addGroup(QStringLiteral("安全链"),
             {{QStringLiteral("急停回路"), QStringLiteral("正常"), QStringLiteral("参与联锁 · 10:38:21"), QStringLiteral("ok")},
              {QStringLiteral("机械硬限位"), QStringLiteral("正常"), QStringLiteral("参与联锁 · 10:38:21"), QStringLiteral("ok")},
              {QStringLiteral("防护门 / 光幕"), QStringLiteral("正常"), QStringLiteral("参与联锁 · 10:38:20"), QStringLiteral("ok")}});
    addGroup(QStringLiteral("控制与通信"),
             {{QStringLiteral("实时总线主站"), QStringLiteral("在线"), QStringLiteral("1 ms · 10:38:21"), QStringLiteral("ok")},
              {QStringLiteral("拖动电机驱动器"), QStringLiteral("就绪"), QStringLiteral("站号 01 · 10:38:21"), QStringLiteral("ok")},
              {QStringLiteral("数据采集模块"), QStringLiteral("在线"), QStringLiteral("20 kS/s · 10:38:20"), QStringLiteral("ok")}});
    addGroup(QStringLiteral("传感器与辅助系统"),
             {{QStringLiteral("冷却水流量"), QStringLiteral("12.6 L/min"), QStringLiteral("范围 10-18 · 参与联锁"), QStringLiteral("ok")},
              {QStringLiteral("气浮台压力"), QStringLiteral("0.594 MPa"), QStringLiteral("范围 0.50-0.65 · 参与联锁"), QStringLiteral("ok")},
              {QStringLiteral("电机温度"), QStringLiteral("42.8 C"), QStringLiteral("预警阈值 75 C · 10:38:19"), QStringLiteral("info")}});
    contentLayout->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    auto* inspect = ViewHelpers::makeButton(QStringLiteral("执行设备自检"));
    connect(inspect,
            &QPushButton::clicked,
            this,
            &DeviceStatusPanel::onInspectButtonClicked);
    outer->addWidget(inspect);
}

void DeviceStatusPanel::onInspectButtonClicked()
{
    emit messageRequested(QStringLiteral("设备自检完成：全部项目通过（模拟）"));
}

QWidget* DeviceStatusPanel::createStateItem(const QString& name,
                                            const QString& state,
                                            const QString& detail,
                                            const QString& level) const
{
    auto* item = new QFrame;
    item->setObjectName(QStringLiteral("stateItem"));
    auto* layout = new QGridLayout(item);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setHorizontalSpacing(5);
    layout->setVerticalSpacing(3);
    layout->addWidget(ViewHelpers::makeLabel(name, "stateName"), 0, 0);
    layout->addWidget(new StatusPill(state, level), 0, 1, Qt::AlignRight);
    layout->addWidget(ViewHelpers::makeLabel(detail, "stateDetail"), 1, 0, 1, 2);
    return item;
}
