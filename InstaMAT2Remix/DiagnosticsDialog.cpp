#include "DiagnosticsDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace InstaMAT2Remix {

DiagnosticsDialog::DiagnosticsDialog(const QString& diagnosticsText, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("RTX Remix Connector - Diagnostics");
    setMinimumWidth(780);
    setMinimumHeight(520);

    auto* root = new QVBoxLayout(this);

    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setPlainText(diagnosticsText);
    root->addWidget(m_text, 1);

    auto* row = new QHBoxLayout();
    row->addStretch(1);

    m_copyBtn = new QPushButton("Copy to Clipboard", this);
    m_closeBtn = new QPushButton("Close", this);
    row->addWidget(m_copyBtn);
    row->addWidget(m_closeBtn);
    root->addLayout(row);

    connect(m_copyBtn, &QPushButton::clicked, this, &DiagnosticsDialog::onCopy);
    connect(m_closeBtn, &QPushButton::clicked, this, &DiagnosticsDialog::accept);
}

void DiagnosticsDialog::onCopy() {
    if (!m_text) return;
    if (!QApplication::clipboard()) return;
    QApplication::clipboard()->setText(m_text->toPlainText());
}

} // namespace InstaMAT2Remix


