#pragma once

#include <QDialog>

class QPlainTextEdit;
class QPushButton;

namespace InstaMAT2Remix {

class DiagnosticsDialog : public QDialog {
    Q_OBJECT
public:
    explicit DiagnosticsDialog(const QString& diagnosticsText, QWidget* parent = nullptr);

private slots:
    void onCopy();

private:
    QPlainTextEdit* m_text = nullptr;
    QPushButton* m_copyBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;
};

} // namespace InstaMAT2Remix


