#include "Logger.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QTextStream>

namespace InstaMAT2Remix {

LogLevel ParseLogLevel(const QString& value, LogLevel fallback) {
    const QString v = value.trimmed().toLower();
    if (v == "debug") return LogLevel::Debug;
    if (v == "info") return LogLevel::Info;
    if (v == "warn" || v == "warning") return LogLevel::Warning;
    if (v == "err" || v == "error") return LogLevel::Error;
    return fallback;
}

QString LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "debug";
        case LogLevel::Info:
            return "info";
        case LogLevel::Warning:
            return "warning";
        case LogLevel::Error:
            return "error";
        default:
            return "info";
    }
}

Logger::Logger(const QString& logFilePath) {
    SetLogFilePath(logFilePath);
}

void Logger::SetLogFilePath(const QString& path) {
    QMutexLocker lock(&m_mutex);

    if (m_file.isOpen()) m_file.close();
    m_logFilePath = path;
    ensureOpenLocked();
}

QString Logger::GetLogFilePath() const {
    QMutexLocker lock(&m_mutex);
    return m_logFilePath;
}

void Logger::SetLevel(LogLevel level) {
    QMutexLocker lock(&m_mutex);
    m_level = level;
}

LogLevel Logger::GetLevel() const {
    QMutexLocker lock(&m_mutex);
    return m_level;
}

void Logger::ensureOpenLocked() {
    if (m_logFilePath.isEmpty()) return;

    const QFileInfo fi(m_logFilePath);
    QDir().mkpath(fi.absolutePath());

    m_file.setFileName(m_logFilePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        // If the file can't be opened (permissions, etc), keep going without file logging.
        m_logFilePath.clear();
        return;
    }
}

QString Logger::formatLine(LogLevel level, const QString& message) {
    const QString ts = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    QString tag = "INFO";
    switch (level) {
        case LogLevel::Debug:
            tag = "DEBUG";
            break;
        case LogLevel::Info:
            tag = "INFO";
            break;
        case LogLevel::Warning:
            tag = "WARN";
            break;
        case LogLevel::Error:
            tag = "ERROR";
            break;
    }
    return QString("[%1] [%2] %3\n").arg(ts, tag, message);
}

void Logger::Log(LogLevel level, const QString& message) {
    {
        QMutexLocker lock(&m_mutex);
        if (static_cast<int>(level) < static_cast<int>(m_level)) return;

        if (!m_logFilePath.isEmpty()) {
            ensureOpenLocked();
            if (m_file.isOpen()) {
                QTextStream ts(&m_file);
                ts << formatLine(level, message);
                m_file.flush();
            }
        }
    }

    // Also forward to Qt logging.
    switch (level) {
        case LogLevel::Debug:
            qDebug().noquote() << "[InstaMAT2Remix]" << message;
            break;
        case LogLevel::Info:
            qInfo().noquote() << "[InstaMAT2Remix]" << message;
            break;
        case LogLevel::Warning:
            qWarning().noquote() << "[InstaMAT2Remix]" << message;
            break;
        case LogLevel::Error:
            qCritical().noquote() << "[InstaMAT2Remix]" << message;
            break;
    }
}

} // namespace InstaMAT2Remix


