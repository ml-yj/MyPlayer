#include "qt_ui_theme.h"

#include <QAbstractButton>
#include <QFileDialog>

namespace
{
QString MessageBoxStyleSheet()
{
    return QStringLiteral(
        "QMessageBox {"
        " background-color: #f4efe6;"
        "}"
        "QMessageBox QLabel {"
        " color: #1f2328;"
        " background: transparent;"
        " min-width: 420px;"
        " font-size: 14px;"
        "}"
        "QMessageBox QPushButton {"
        " min-width: 96px;"
        " min-height: 34px;"
        " padding: 4px 14px;"
        " border-radius: 6px;"
        " border: 1px solid #5a6675;"
        " background-color: #29303a;"
        " color: #ffffff;"
        "}"
        "QMessageBox QPushButton:hover {"
        " background-color: #36404d;"
        "}"
        "QMessageBox QPushButton:pressed {"
        " background-color: #1f252d;"
        "}"
        "QMessageBox QTextEdit {"
        " color: #1f2328;"
        " background-color: #fbf8f2;"
        " border: 1px solid #d6c7b2;"
        "}");
}

}

namespace QtUiTheme
{
void ApplyMenuStyle(QMenu& menu)
{
    menu.setStyleSheet(
        "QMenu {"
        " background: #f4efe6;"
        " color: #1f2328;"
        " border: 1px solid #ccb89b;"
        " padding: 6px;"
        "}"
        "QMenu::item {"
        " color: #1f2328;"
        " background: transparent;"
        " padding: 7px 30px 7px 12px;"
        " margin: 2px 4px;"
        " border-radius: 6px;"
        "}"
        "QMenu::item:selected {"
        " background: #c96a16;"
        " color: #ffffff;"
        "}"
        "QMenu::item:disabled {"
        " color: #7d7469;"
        " background: transparent;"
        "}"
        "QMenu::separator {"
        " height: 1px;"
        " background: #d9ccb9;"
        " margin: 6px 12px;"
        "}");
}

QMessageBox::StandardButton ShowMessageBox(
    QWidget* parent,
    QMessageBox::Icon icon,
    const QString& title,
    const QString& text,
    QMessageBox::StandardButtons buttons,
    QMessageBox::StandardButton defaultButton)
{
    QMessageBox box(icon, title, QString(), buttons, parent);
    box.setWindowTitle(title);
    box.setTextFormat(Qt::PlainText);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);
    box.setMinimumWidth(520);
    box.setStyleSheet(MessageBoxStyleSheet());

    const int split = text.indexOf('\n');
    if (split >= 0)
    {
        box.setText(text.left(split).trimmed());
        box.setInformativeText(text.mid(split + 1).trimmed());
    }
    else
    {
        box.setText(text.trimmed());
    }

    if (defaultButton != QMessageBox::NoButton)
        box.setDefaultButton(defaultButton);

    const auto buttonsList = box.buttons();
    for (QAbstractButton* button : buttonsList)
        button->setCursor(Qt::PointingHandCursor);

    return static_cast<QMessageBox::StandardButton>(box.exec());
}

QStringList GetOpenFileNames(
    QWidget* parent,
    const QString& caption,
    const QString& dir,
    const QString& filter,
    QFileDialog::Options options)
{
    return QFileDialog::getOpenFileNames(parent, caption, dir, filter, nullptr, options);
}

QString GetOpenFileName(
    QWidget* parent,
    const QString& caption,
    const QString& dir,
    const QString& filter,
    QFileDialog::Options options)
{
    return QFileDialog::getOpenFileName(parent, caption, dir, filter, nullptr, options);
}

QString GetSaveFileName(
    QWidget* parent,
    const QString& caption,
    const QString& dir,
    const QString& filter,
    QFileDialog::Options options)
{
    return QFileDialog::getSaveFileName(parent, caption, dir, filter, nullptr, options);
}

QString GetExistingDirectory(
    QWidget* parent,
    const QString& caption,
    const QString& dir,
    QFileDialog::Options options)
{
    return QFileDialog::getExistingDirectory(parent, caption, dir, options);
}
}
