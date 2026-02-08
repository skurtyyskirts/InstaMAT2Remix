#pragma once

#include <QFile>
#include <QMutex>
#include <QString>

namespace InstaMAT2Remix {

enum class LogLevel : int {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
};

LogLevel ParseLogLevel(const QString& value, LogLevel fallback = LogLevel::Info);
QString LogLevelToString(LogLevel level);

class Logger {
public:
    Logger() = default;
    explicit Logger(const QString& logFilePath);

    void SetLogFilePath(const QString& path);
    QString GetLogFilePath() const;

    void SetLevel(LogLevel level);
    LogLevel GetLevel() const;

    void Log(LogLevel level, const QString& message);

    void Debug(const QString& message) { Log(LogLevel::Debug, message); }
    void Info(const QString& message) { Log(LogLevel::Info, message); }
    void Warning(const QString& message) { Log(LogLevel::Warning, message); }
    void Error(const QString& message) { Log(LogLevel::Error, message); }

private:
    void ensureOpenLocked();
    static QString formatLine(LogLevel level, const QString& message);

    mutable QMutex m_mutex;
    QFile m_file;
    QString m_logFilePath;
    LogLevel m_level = LogLevel::Info;
};

} // namespace InstaMAT2Remix


