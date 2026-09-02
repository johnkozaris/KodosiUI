#pragma once

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringView>

#include <atomic>
#include <mutex>

Q_DECLARE_LOGGING_CATEGORY(kodosiPerformance)

namespace kodosi {

class ApplicationLogStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool healthy READ healthy NOTIFY stateChanged)
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(QString directory READ directory CONSTANT)
    Q_PROPERTY(bool directoryAvailable READ directoryAvailable NOTIFY stateChanged)
    Q_PROPERTY(qint64 sizeBytes READ sizeBytes NOTIFY stateChanged)
    Q_PROPERTY(int rotationCount READ rotationCount NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)

public:
    struct Options {
        QString directory;
        qint64 rotationBytes = 2 * 1024 * 1024;
        int maximumArchives = 5;
        int maximumCompletedFamilies = 8;
        bool installQtMessageHandler = true;
    };

    explicit ApplicationLogStore(
        Options options,
        QObject* parent = nullptr);
    ~ApplicationLogStore() override;

    ApplicationLogStore(const ApplicationLogStore&) = delete;
    ApplicationLogStore& operator=(const ApplicationLogStore&) = delete;

    [[nodiscard]] bool healthy() const;
    [[nodiscard]] QString path() const;
    [[nodiscard]] QString directory() const;
    [[nodiscard]] bool directoryAvailable() const;
    [[nodiscard]] qint64 sizeBytes() const;
    [[nodiscard]] int rotationCount() const;
    [[nodiscard]] QString lastError() const;

    void record(
        QtMsgType type,
        QStringView category,
        QStringView message);

    [[nodiscard]] static QString standardLogDirectory();
    [[nodiscard]] static QString redact(QString message);

signals:
    void stateChanged();

private:
    friend void applicationQtMessageHandler(
        QtMsgType,
        const QMessageLogContext&,
        const QString&);

    static constexpr qsizetype maximumPersistedMessageCharacters = 32 * 1024;
    static constexpr qsizetype maximumCategoryCharacters = 256;

    Options m_options;
    QString m_path;
    mutable std::mutex m_mutex;
    QtMessageHandler m_previousHandler = nullptr;
    std::atomic_bool m_notificationQueued = false;
    QString m_lastError;
    qint64 m_sizeBytes = 0;
    int m_rotationCount = 0;
    int m_fileDescriptor = -1;
    bool m_healthy = false;
    bool m_handlerInstalled = false;

    void initialize();
    [[nodiscard]] bool openCurrentFile();
    [[nodiscard]] bool rotate();
    [[nodiscard]] bool writeAll(const QByteArray& bytes);
    [[nodiscard]] bool sanitizeCurrentFamilyLocked();
    [[nodiscard]] bool pruneCompletedFamiliesLocked();
    void failLocked(QString error);
    void refreshArchiveCountLocked();
    void scheduleStateChanged();
};

enum class PerformanceCategory {
    EventLane,
    FfiDecode,
    Terminal,
};

class ScopedPerformanceSpan final {
public:
    ScopedPerformanceSpan(
        PerformanceCategory category,
        QStringView operation,
        qint64 thresholdMilliseconds);
    ~ScopedPerformanceSpan();

    ScopedPerformanceSpan(const ScopedPerformanceSpan&) = delete;
    ScopedPerformanceSpan& operator=(const ScopedPerformanceSpan&) = delete;

    void setOutcome(QStringView outcome);

private:
    PerformanceCategory m_category;
    QString m_operation;
    QString m_outcome = QStringLiteral("ok");
    qint64 m_thresholdMilliseconds = 0;
    QElapsedTimer m_timer;
};

} // namespace kodosi
