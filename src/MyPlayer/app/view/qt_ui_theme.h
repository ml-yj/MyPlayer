#pragma once

#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QWidget>

namespace QtUiTheme
{
void ApplyMenuStyle(QMenu& menu);

QMessageBox::StandardButton ShowMessageBox(
    QWidget* parent,
    QMessageBox::Icon icon,
    const QString& title,
    const QString& text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

QStringList GetOpenFileNames(
    QWidget* parent,
    const QString& caption,
    const QString& dir,
    const QString& filter,
    QFileDialog::Options options = {});

QString GetOpenFileName(
    QWidget* parent,
    const QString& caption,
    const QString& dir,
    const QString& filter,
    QFileDialog::Options options = {});

QString GetSaveFileName(
    QWidget* parent,
    const QString& caption,
    const QString& dir,
    const QString& filter,
    QFileDialog::Options options = {});

QString GetExistingDirectory(
    QWidget* parent,
    const QString& caption,
    const QString& dir,
    QFileDialog::Options options = QFileDialog::ShowDirsOnly);

inline void ShowWarning(QWidget* parent, const QString& title, const QString& text)
{
    (void)ShowMessageBox(parent, QMessageBox::Warning, title, text, QMessageBox::Ok);
}

inline void ShowInfo(QWidget* parent, const QString& title, const QString& text)
{
    (void)ShowMessageBox(parent, QMessageBox::Information, title, text, QMessageBox::Ok);
}

inline QMessageBox::StandardButton AskQuestion(
    QWidget* parent,
    const QString& title,
    const QString& text,
    QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No,
    QMessageBox::StandardButton defaultButton = QMessageBox::No)
{
    return ShowMessageBox(parent, QMessageBox::Question, title, text, buttons, defaultButton);
}
}
