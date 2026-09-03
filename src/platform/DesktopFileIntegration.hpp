#pragma once

#include <QFile>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QWindow>

#include <functional>
#include <memory>

namespace kodosi {

class SessionCatalogModel;
class ApplicationLogStore;

class DirectoryPicker {
public:
    struct Request {
        QString title;
        QString message;
        QString acceptLabel;
        QUrl initialDirectory;
        QPointer<QWindow> transientParent;
    };

    enum class Outcome {
        Selected,
        Cancelled,
        Failed,
    };

    struct Result {
        Outcome outcome = Outcome::Failed;
        QUrl selectedDirectory;
        QString error;
    };

    using Completion = std::function<void(Result)>;

    virtual ~DirectoryPicker() = default;
    virtual void open(Request request, Completion completion) = 0;
    virtual void cancel() = 0;
};

class DesktopFileIntegration final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(ErrorCode errorCode READ errorCode NOTIFY errorChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
    Q_PROPERTY(QString errorRequestId READ errorRequestId NOTIFY errorChanged)
    Q_PROPERTY(Purpose errorPurpose READ errorPurpose NOTIFY errorChanged)

public:
    enum class Purpose {
        NewSessionWorkingDirectory = 0,
        SettingsWorkingDirectory,
        SettingsOpenWorkingDirectory,
        SessionProject,
        TerminalProject,
        DiagnosticsLogDirectory,
        ProjectMemory,
        ProjectCustomAgent,
        ExternalSource,
        ResumeAgentWork,
    };
    Q_ENUM(Purpose)

    enum class ErrorCode {
        None = 0,
        Busy,
        InvalidRequest,
        TooLong,
        ContainsNul,
        NonLocalPath,
        MissingPath,
        NotDirectory,
        UnsupportedFileType,
        UnreadablePath,
        UnsearchableDirectory,
        PickerFailed,
        LaunchFailed,
    };
    Q_ENUM(ErrorCode)

    struct ValidationResult {
        QString canonicalPath;
        ErrorCode errorCode = ErrorCode::None;
        QString errorMessage;

        [[nodiscard]] bool valid() const noexcept
        {
            return errorCode == ErrorCode::None;
        }
    };

    using UrlOpener = std::function<bool(const QUrl&)>;

    explicit DesktopFileIntegration(
        SessionCatalogModel& sessions,
        ApplicationLogStore* applicationLog = nullptr,
        QObject* parent = nullptr);
    DesktopFileIntegration(
        SessionCatalogModel& sessions,
        std::unique_ptr<DirectoryPicker> picker,
        UrlOpener opener,
        ApplicationLogStore* applicationLog = nullptr,
        QObject* parent = nullptr);
    ~DesktopFileIntegration() override;

    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] ErrorCode errorCode() const noexcept;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QString errorRequestId() const;
    [[nodiscard]] Purpose errorPurpose() const noexcept;

    Q_INVOKABLE [[nodiscard]] bool requestDirectory(
        const QString& requestId,
        Purpose purpose,
        const QString& initialDirectory);
    Q_INVOKABLE [[nodiscard]] bool cancelDirectory(
        const QString& requestId,
        Purpose purpose);
    Q_INVOKABLE [[nodiscard]] bool openPath(
        const QString& path,
        const QString& requestId,
        Purpose purpose);
    Q_INVOKABLE [[nodiscard]] bool openSessionProject(
        const QString& sessionId,
        const QString& requestId,
        Purpose purpose);
    Q_INVOKABLE [[nodiscard]] bool openLogDirectory(
        const QString& requestId,
        Purpose purpose);
    Q_INVOKABLE [[nodiscard]] bool canOpenSessionProject(
        const QString& sessionId) const;
    Q_INVOKABLE void clearError();
    void setTransientParent(QWindow* transientParent);
    [[nodiscard]] bool openBoundHandoff(
        const QString& handoffPath,
        const QString& requestId,
        Purpose purpose);
    [[nodiscard]] bool revealBoundSource(
        const QString& sourcePath,
        const QString& requestId,
        Purpose purpose);

    [[nodiscard]] static ValidationResult validateDirectory(
        const QString& path);
    [[nodiscard]] static ValidationResult validateOpenPath(
        const QString& path);
    [[nodiscard]] static QString resolveInitialDirectoryHint(
        const QString& path);

signals:
    void busyChanged();
    void errorChanged();
    void directoryPicked(
        QString requestId,
        Purpose purpose,
        QString canonicalDirectory);
    void directoryPickCancelled(QString requestId, Purpose purpose);
    void pathOpened(
        QString requestId,
        Purpose purpose,
        QString canonicalPath);
    void operationFailed(
        QString requestId,
        Purpose purpose,
        ErrorCode errorCode,
        QString message);

private:
    static constexpr qsizetype maximumPathLength = 4'096;
    static constexpr qsizetype maximumRequestIdLength = 256;

    SessionCatalogModel& m_sessions;
    ApplicationLogStore* m_applicationLog = nullptr;
    std::unique_ptr<DirectoryPicker> m_picker;
    UrlOpener m_opener;
    QPointer<QWindow> m_transientParent;
    QString m_activeRequestId;
    Purpose m_activePurpose = Purpose::NewSessionWorkingDirectory;
    ErrorCode m_errorCode = ErrorCode::None;
    QString m_errorMessage;
    QString m_errorRequestId;
    Purpose m_errorPurpose = Purpose::NewSessionWorkingDirectory;
    QHash<quint64, std::shared_ptr<QFile>> m_retainedBoundHandoffs;
    quint64 m_nextBoundHandoffId = 0;
    bool m_busy = false;

    [[nodiscard]] static ValidationResult validatePath(
        const QString& path,
        bool directoryOnly);
    [[nodiscard]] static ValidationResult validateRequestId(
        const QString& requestId);
    [[nodiscard]] static bool isPickerPurpose(Purpose purpose) noexcept;
    [[nodiscard]] static bool isSessionProjectPurpose(
        Purpose purpose) noexcept;
    [[nodiscard]] static bool isBoundHandoffPurpose(
        Purpose purpose) noexcept;
    [[nodiscard]] bool openValidatedPath(
        const QString& path,
        const QString& requestId,
        Purpose purpose,
        bool directoryOnly);
    void finishPicker();
    void handlePickerResult(
        const QString& requestId,
        Purpose purpose,
        DirectoryPicker::Result result);
    void fail(
        const QString& requestId,
        Purpose purpose,
        ErrorCode code,
        QString message);
    void clearErrorState();
};

[[nodiscard]] std::unique_ptr<DirectoryPicker> createNativeDirectoryPicker();

} // namespace kodosi
