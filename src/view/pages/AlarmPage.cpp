#include "AlarmPage.h"

#include "../widgets/ViewHelpers.h"
#include "../../alarm/AlarmService.h"

#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QString timeText(const QDateTime& time)
{
    return time.isValid() ? time.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("--");
}
QString stateText(const AlarmRecord& record)
{
    switch (record.state()) {
    case AlarmState::UnacknowledgedUnrecovered: return QStringLiteral("未确认，未恢复");
    case AlarmState::AcknowledgedUnrecovered: return QStringLiteral("已确认，未恢复");
    case AlarmState::RecoveredUnacknowledged: return QStringLiteral("已恢复，未确认");
    case AlarmState::Closed: return QStringLiteral("已关闭");
    }
    return {};
}
QUuid selectedId(QTableWidget* table)
{
    const int row = table->currentRow();
    return row < 0 || table->item(row, 0) == nullptr ? QUuid() : QUuid(table->item(row, 0)->data(Qt::UserRole).toString());
}
void fillTable(QTableWidget* table, const QVector<AlarmRecord>& records, bool history)
{
    const QUuid selected = selectedId(table);
    table->setRowCount(static_cast<int>(records.size()));
    for (qsizetype row = 0; row < records.size(); ++row) {
        const AlarmRecord& record = records.at(row);
        QString title = record.title;
        if (record.triggerDetails.contains("maximumCelsius")) {
            title += QStringLiteral("：%1 ℃（通道 %2）").arg(record.triggerDetails.value("maximumCelsius").toDouble(), 0, 'f', 1)
                .arg(record.triggerDetails.value("maximumChannel").toInt());
        }
        QStringList cells{timeText(record.raisedAtUtc), record.severity == AlarmSeverity::Fault ? QStringLiteral("故障") : QStringLiteral("预警"),
            record.sourceId, title};
        if (history) { cells.append(timeText(record.closedAtUtc)); }
        else { cells.append(stateText(record)); cells.append(timeText(record.recoveredAtUtc)); }
        cells.append(record.experimentName.isEmpty() ? QStringLiteral("--") : record.experimentName);
        for (int column = 0; column < cells.size(); ++column) {
            auto* item = new QTableWidgetItem(cells.at(column)); item->setData(Qt::UserRole, record.occurrenceId.toString());
            table->setItem(static_cast<int>(row), column, item);
        }
        if (record.occurrenceId == selected) { table->selectRow(static_cast<int>(row)); }
    }
}
} // namespace

AlarmPage::AlarmPage(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(18, 14, 18, 14); layout->setSpacing(12);
    layout->addWidget(ViewHelpers::makeLabel(QStringLiteral("报警记录"), "pageTitle"));
    auto* filters = new QHBoxLayout;
    severityFilter_ = new QComboBox; severityFilter_->addItems({QStringLiteral("全部等级"), QStringLiteral("预警"), QStringLiteral("故障")});
    sourceFilter_ = new QComboBox; sourceFilter_->addItems({QStringLiteral("全部来源"), QStringLiteral("温度采集模块"), QStringLiteral("安全回路")});
    for (QComboBox* filter : {severityFilter_, sourceFilter_}) {
        filters->addWidget(filter); connect(filter, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    }
    filters->addStretch(); layout->addLayout(filters); auto* tabs = new QTabWidget;
    const QStringList activeHeaders{QStringLiteral("触发时间"), QStringLiteral("等级"), QStringLiteral("来源"), QStringLiteral("报警内容"),
        QStringLiteral("当前状态"), QStringLiteral("恢复时间"), QStringLiteral("关联实验")};
    const QStringList historyHeaders{QStringLiteral("触发时间"), QStringLiteral("等级"), QStringLiteral("来源"), QStringLiteral("报警内容"),
        QStringLiteral("关闭时间"), QStringLiteral("关联实验")};
    active_ = ViewHelpers::makeTable(activeHeaders, {}); history_ = ViewHelpers::makeTable(historyHeaders, {});
    for (QTableWidget* table : {active_, history_}) {
        table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection); table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setStretchLastSection(true);
    }
    tabs->addTab(active_, QStringLiteral("当前报警")); tabs->addTab(history_, QStringLiteral("历史报警")); layout->addWidget(tabs, 1);
    auto* actions = new QHBoxLayout;
    QPushButton* acknowledge = ViewHelpers::makeButton(QStringLiteral("确认所选报警"));
    actions->addWidget(acknowledge); actions->addStretch(); layout->addLayout(actions);
    connect(acknowledge, &QPushButton::clicked, this, &AlarmPage::acknowledgeSelected);
    connect(&AlarmService::instance(), &AlarmService::recordsChanged, this, [this] { refresh(); }, Qt::QueuedConnection);
    refresh();
}
void AlarmPage::refresh()
{
    QVector<AlarmRecord> active; QVector<AlarmRecord> history;
    const QVector<AlarmRecord> records = AlarmService::instance().records();
    for (const AlarmRecord& record : records) {
        if ((severityFilter_->currentIndex() == 1 && record.severity != AlarmSeverity::Warning)
            || (severityFilter_->currentIndex() == 2 && record.severity != AlarmSeverity::Fault)
            || (sourceFilter_->currentIndex() == 1 && record.code == "SAFETY_CIRCUIT_FAULT")
            || (sourceFilter_->currentIndex() == 2 && record.code != "SAFETY_CIRCUIT_FAULT")) { continue; }
        (record.state() == AlarmState::Closed ? history : active).prepend(record);
    }
    const auto newestFirst = [](const AlarmRecord& left, const AlarmRecord& right) { return left.raisedAtUtc > right.raisedAtUtc; };
    std::sort(active.begin(), active.end(), newestFirst); std::sort(history.begin(), history.end(), newestFirst);
    fillTable(active_, active, false); fillTable(history_, history, true);
}
void AlarmPage::acknowledgeSelected()
{
    const QUuid id = selectedId(active_);
    if (id.isNull()) { QMessageBox::information(this, QStringLiteral("报警确认"), QStringLiteral("请选择当前报警。")); return; }
    bool accepted = false;
    const QString user = QInputDialog::getText(this, QStringLiteral("报警确认"), QStringLiteral("确认人"),
        QLineEdit::Normal, qEnvironmentVariable("USERNAME"), &accepted);
    if (accepted && !user.trimmed().isEmpty()) { AlarmService::instance().acknowledge(id, user.trimmed()); }
}
