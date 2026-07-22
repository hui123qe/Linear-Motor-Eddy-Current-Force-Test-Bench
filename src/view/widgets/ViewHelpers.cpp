#include "ViewHelpers.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ViewHelpers {

QLabel* makeLabel(const QString& text, const char* objectName)
{
    auto* label = new QLabel(text);
    if (objectName != nullptr) {
        label->setObjectName(QString::fromLatin1(objectName));
    }
    return label;
}

QFrame* makeDivider()
{
    auto* divider = new QFrame;
    divider->setFrameShape(QFrame::HLine);
    divider->setObjectName(QStringLiteral("divider"));
    return divider;
}

QPushButton* makeButton(const QString& text, const QString& style)
{
    auto* button = new QPushButton(text);
    button->setProperty("buttonStyle", style);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QFrame* makePanel(const QString& title, const QString& caption)
{
    auto* panel = new QFrame;
    panel->setObjectName(QStringLiteral("panel"));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(15, 13, 15, 14);
    layout->setSpacing(10);

    auto* titleBox = new QVBoxLayout;
    titleBox->setSpacing(1);
    titleBox->addWidget(makeLabel(title, "panelTitle"));
    if (!caption.isEmpty()) {
        titleBox->addWidget(makeLabel(caption, "panelCaption"));
    }
    layout->addLayout(titleBox);
    return panel;
}

QTableWidget* makeTable(const QStringList& headers, const QList<QStringList>& rows)
{
    auto* table = new QTableWidget(rows.size(), headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(38);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    for (int row = 0; row < rows.size(); ++row) {
        for (int column = 0; column < headers.size(); ++column) {
            table->setItem(row, column, new QTableWidgetItem(rows.at(row).at(column)));
        }
    }
    return table;
}

QWidget* makeFilterField(const QString& title, QWidget* input)
{
    auto* field = new QWidget;
    auto* layout = new QVBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    layout->addWidget(makeLabel(title, "fieldLabel"));
    layout->addWidget(input);
    return field;
}

} // namespace ViewHelpers
