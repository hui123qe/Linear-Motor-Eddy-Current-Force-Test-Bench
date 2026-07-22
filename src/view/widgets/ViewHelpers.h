#pragma once

#include <QFrame>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QWidget>

namespace ViewHelpers {

QLabel* makeLabel(const QString& text, const char* objectName = nullptr);
QFrame* makeDivider();
QPushButton* makeButton(const QString& text,
                        const QString& style = QStringLiteral("secondary"));
QFrame* makePanel(const QString& title, const QString& caption = {});
QTableWidget* makeTable(const QStringList& headers, const QList<QStringList>& rows);
QWidget* makeFilterField(const QString& title, QWidget* input);

} // namespace ViewHelpers
