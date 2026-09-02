#include "platform/DesktopFileIntegration.hpp"

#include "models/SessionCatalogModel.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QWindow>

#if defined(Q_OS_LINUX)
#include <unistd.h>
#endif

#include <optional>
#include <utility>

namespace kodosi {
namespace {

QString translated(const char* source)
{
    return QCoreApplication::translate("DesktopFileIntegration", source);
}

QString nearestUsableDirectory(QString path)
{
    if (path.isEmpty() || path.contains(QChar::Null)
        || path.size() > 4'096 || !QDir::isAbsolutePath(path)
        || !QUrl(path).scheme().isEmpty()) {
        return {};
    }

    path = QDir::cleanPath(path);
    while (!path.isEmpty()) {
        const auto validation =
            DesktopFileIntegration::validateDirectory(path);
        if (validation.valid()) {
            return validation.canonicalPath;
        }

        const QFileInfo current(path);
        const auto parent = current.dir().absolutePath();
        if (parent == path) {
            break;
        }
        path = parent;
    }
    return {};
}

class NativeDirectoryPicker final : public DirectoryPicker {
public:
    void open(Request request, Completion completion) override
    {
        Q_ASSERT(*m_dialog == nullptr);
        auto* dialog = new QFileDialog;
        *m_dialog = dialog;
        dialog->setWindowTitle(request.title);
        dialog->setAccessibleName(request.title);
        dialog->setAccessibleDescription(request.message);
        dialog->setLabelText(QFileDialog::Accept, request.acceptLabel);
        dialog->setAcceptMode(QFileDialog::AcceptOpen);
        dialog->setFileMode(QFileDialog::Directory);
        dialog->setOption(QFileDialog::ShowDirsOnly, true);
        dialog->setOption(QFileDialog::DontUseNativeDialog, false);
        dialog->setSupportedSchemes({QStringLiteral("file")});
        dialog->setDirectoryUrl(request.initialDirectory);
        dialog->setWindowModality(Qt::ApplicationModal);

        QObject::connect(
            dialog,
            &QDialog::finished,
            dialog,
            [dialog,
             dialogState = m_dialog,
             completion = std::move(completion)](
                const int result) mutable {
                dialogState->clear();
                DirectoryPicker::Result pickerResult;
                if (result == QDialog::Rejected) {
                    pickerResult.outcome =
                        DirectoryPicker::Outcome::Cancelled;
                } else {
                    const auto selected = dialog->selectedUrls();
                    std::optional<QUrl> selectedDirectory;
                    if (selected.size() == 1
                        && selected.constFirst().isValid()
                        && selected.constFirst().isLocalFile()) {
                        selectedDirectory = selected.constFirst();
                    } else {
                        const auto fallback = dialog->directoryUrl();
                        if (fallback.isValid() && fallback.isLocalFile()) {
                            selectedDirectory = fallback;
                        }
                    }
                    if (selectedDirectory) {
                        pickerResult.outcome =
                            DirectoryPicker::Outcome::Selected;
                        pickerResult.selectedDirectory =
                            std::move(*selectedDirectory);
                    } else {
                        pickerResult.outcome =
                            DirectoryPicker::Outcome::Failed;
                        pickerResult.error = translated(
                            "The system folder picker returned no directory.");
                    }
                }
                completion(std::move(pickerResult));
                dialog->deleteLater();
            });

        dialog->setAttribute(Qt::WA_NativeWindow);
        if (auto* dialogWindow = dialog->windowHandle();
            dialogWindow != nullptr) {
            if (request.transientParent != nullptr
                && request.transientParent != dialogWindow) {
                dialogWindow->setTransientParent(request.transientParent);
            }
        }
        dialog->show();
    }

    void cancel() override
    {
        if (*m_dialog != nullptr) {
            (*m_dialog)->reject();
        }
    }

private:
    std::shared_ptr<QPointer<QFileDialog>> m_dialog =
        std::make_shared<QPointer<QFileDialog>>();
};

} // namespace

std::unique_ptr<DirectoryPicker> createNativeDirectoryPicker()
{
    return std::make_unique<NativeDirectoryPicker>();
}

DesktopFileIntegration::DesktopFileIntegration(
    SessionCatalogModel& sessions,
    QObject* parent)
    : DesktopFileIntegration(
        sessions,
        createNativeDirectoryPicker(),
        [](const QUrl& url) { return QDesktopServices::openUrl(url); },
        parent)
{
}

DesktopFileIntegration::DesktopFileIntegration(
    SessionCatalogModel& sessions,
    std::unique_ptr<DirectoryPicker> picker,
    UrlOpener opener,
    QObject* parent)
    : QObject(parent)
    , m_sessions(sessions)
    , m_picker(std::move(picker))
    , m_opener(std::move(opener))
{
    Q_ASSERT(m_picker != nullptr);
    Q_ASSERT(static_cast<bool>(m_opener));
}

bool DesktopFileIntegration::busy() const noexcept
{
    return m_busy;
}

DesktopFileIntegration::ErrorCode
DesktopFileIntegration::errorCode() const noexcept
{
    return m_errorCode;
}

QString DesktopFileIntegration::errorMessage() const
{
    return m_errorMessage;
}

QString DesktopFileIntegration::errorRequestId() const
{
    return m_errorRequestId;
}

DesktopFileIntegration::Purpose
DesktopFileIntegration::errorPurpose() const noexcept
{
    return m_errorPurpose;
}

bool DesktopFileIntegration::requestDirectory(
    const QString& requestId,
    const Purpose purpose,
    const QString& initialDirectory)
{
    const auto requestValidation = validateRequestId(requestId);
    if (!requestValidation.valid()) {
        fail(
            requestId,
            purpose,
            requestValidation.errorCode,
            requestValidation.errorMessage);
        return false;
    }
    if (!isPickerPurpose(purpose)) {
        fail(
            requestId,
            purpose,
            ErrorCode::InvalidRequest,
            translated("This request cannot open the folder picker."));
        return false;
    }
    if (m_busy) {
        fail(
            requestId,
            purpose,
            ErrorCode::Busy,
            translated(
                "Another folder picker is already open. Finish or cancel it first."));
        return false;
    }

    clearErrorState();
    m_busy = true;
    m_activeRequestId = requestId;
    m_activePurpose = purpose;
    emit busyChanged();

    const auto message = purpose == Purpose::NewSessionWorkingDirectory
        ? translated("Select a working directory for the new session")
        : translated("Select the default working directory for sessions");
    const QPointer<DesktopFileIntegration> self(this);
    m_picker->open(
        {
            .title = translated("Choose Working Directory"),
            .message = message,
            .acceptLabel = translated("Choose"),
            .initialDirectory = QUrl::fromLocalFile(
                resolveInitialDirectoryHint(initialDirectory)),
            .transientParent = m_transientParent,
        },
        [self, requestId, purpose](DirectoryPicker::Result result) {
            if (self == nullptr) {
                return;
            }
            self->handlePickerResult(
                requestId,
                purpose,
                std::move(result));
        });
    return true;
}

bool DesktopFileIntegration::cancelDirectory(
    const QString& requestId,
    const Purpose purpose)
{
    if (!m_busy || requestId != m_activeRequestId
        || purpose != m_activePurpose) {
        return false;
    }
    m_picker->cancel();
    return true;
}

bool DesktopFileIntegration::openPath(
    const QString& path,
    const QString& requestId,
    const Purpose purpose)
{
    const auto requestValidation = validateRequestId(requestId);
    if (!requestValidation.valid()) {
        fail(
            requestId,
            purpose,
            requestValidation.errorCode,
            requestValidation.errorMessage);
        return false;
    }
    if (purpose != Purpose::SettingsOpenWorkingDirectory) {
        fail(
            requestId,
            purpose,
            ErrorCode::InvalidRequest,
            translated("This request cannot open a filesystem path."));
        return false;
    }

    return openValidatedPath(
        path,
        requestId,
        purpose,
        false);
}

bool DesktopFileIntegration::openSessionProject(
    const QString& sessionId,
    const QString& requestId,
    const Purpose purpose)
{
    const auto requestValidation = validateRequestId(requestId);
    if (!requestValidation.valid()) {
        fail(
            requestId,
            purpose,
            requestValidation.errorCode,
            requestValidation.errorMessage);
        return false;
    }
    if (!isSessionProjectPurpose(purpose)) {
        fail(
            requestId,
            purpose,
            ErrorCode::InvalidRequest,
            translated("This request cannot open a session project."));
        return false;
    }
    const auto sessionValidation = validateRequestId(sessionId);
    if (!sessionValidation.valid()) {
        fail(
            requestId,
            purpose,
            ErrorCode::InvalidRequest,
            translated("The session project request has no valid session identity."));
        return false;
    }

    const auto current = m_sessions.conversationContext(sessionId);
    if (!current) {
        fail(
            requestId,
            purpose,
            ErrorCode::InvalidRequest,
            translated("This session is no longer available."));
        return false;
    }
    if (current->kind != QStringLiteral("local")) {
        fail(
            requestId,
            purpose,
            ErrorCode::InvalidRequest,
            translated("Only a current local session has a project that can be opened."));
        return false;
    }
    return openValidatedPath(
        current->workingDirectory,
        requestId,
        purpose,
        true);
}

bool DesktopFileIntegration::openValidatedPath(
    const QString& path,
    const QString& requestId,
    const Purpose purpose,
    const bool directoryOnly)
{
    const auto validation = directoryOnly
        ? validateDirectory(path)
        : validateOpenPath(path);
    if (!validation.valid()) {
        fail(
            requestId,
            purpose,
            validation.errorCode,
            validation.errorMessage);
        return false;
    }
    const auto url = QUrl::fromLocalFile(validation.canonicalPath);
    if (!m_opener(url)) {
        fail(
            requestId,
            purpose,
            ErrorCode::LaunchFailed,
            translated("The system could not open the selected path in its default app."));
        return false;
    }

    clearErrorState();
    emit pathOpened(requestId, purpose, validation.canonicalPath);
    return true;
}

bool DesktopFileIntegration::canOpenSessionProject(
    const QString& sessionId) const
{
    if (sessionId.isEmpty() || sessionId.size() > maximumRequestIdLength
        || sessionId.contains(QChar::Null)) {
        return false;
    }
    const auto current = m_sessions.conversationContext(sessionId);
    if (!current || current->kind != QStringLiteral("local")) {
        return false;
    }
    return validateDirectory(current->workingDirectory)
        .valid();
}

void DesktopFileIntegration::setTransientParent(QWindow* transientParent)
{
    m_transientParent = transientParent;
}

void DesktopFileIntegration::clearError()
{
    clearErrorState();
}

DesktopFileIntegration::ValidationResult
DesktopFileIntegration::validateDirectory(const QString& path)
{
    return validatePath(path, true);
}

DesktopFileIntegration::ValidationResult
DesktopFileIntegration::validateOpenPath(const QString& path)
{
    return validatePath(path, false);
}

QString DesktopFileIntegration::resolveInitialDirectoryHint(
    const QString& path)
{
    auto candidate = path.trimmed();
    const auto home = QDir::homePath();
    if (candidate == QStringLiteral("~")) {
        candidate = home;
    } else if (candidate.startsWith(QStringLiteral("~/"))) {
        candidate = QDir(home).filePath(candidate.sliced(2));
    }

    if (const auto resolved = nearestUsableDirectory(candidate);
        !resolved.isEmpty()) {
        return resolved;
    }
    if (const auto resolvedHome = nearestUsableDirectory(home);
        !resolvedHome.isEmpty()) {
        return resolvedHome;
    }
    if (const auto resolvedCurrent =
            nearestUsableDirectory(QDir::currentPath());
        !resolvedCurrent.isEmpty()) {
        return resolvedCurrent;
    }
    return QDir::rootPath();
}

DesktopFileIntegration::ValidationResult
DesktopFileIntegration::validatePath(
    const QString& path,
    const bool directoryOnly)
{
    if (path.size() > maximumPathLength) {
        return {
            .canonicalPath = {},
            .errorCode = ErrorCode::TooLong,
            .errorMessage = translated(
                "The filesystem path is longer than 4096 characters."),
        };
    }
    if (path.contains(QChar::Null)) {
        return {
            .canonicalPath = {},
            .errorCode = ErrorCode::ContainsNul,
            .errorMessage = translated(
                "The filesystem path contains an invalid null character."),
        };
    }
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) {
        return {
            .canonicalPath = {},
            .errorCode = ErrorCode::NonLocalPath,
            .errorMessage = translated(
                "Choose an absolute path on the local filesystem."),
        };
    }
    const QUrl possibleUrl(path);
    if (!possibleUrl.scheme().isEmpty()) {
        return {
            .canonicalPath = {},
            .errorCode = ErrorCode::NonLocalPath,
            .errorMessage = translated(
                "URLs and remote locations are not supported; choose a local filesystem path."),
        };
    }

    const QFileInfo source(path);
    if (!source.exists()) {
        return {
            .canonicalPath = {},
            .errorCode = ErrorCode::MissingPath,
            .errorMessage = translated(
                "The selected filesystem path does not exist."),
        };
    }
    const auto canonicalPath = source.canonicalFilePath();
    if (canonicalPath.isEmpty()) {
        return {
            .canonicalPath = {},
            .errorCode = ErrorCode::MissingPath,
            .errorMessage = translated(
                "The selected filesystem path could not be resolved."),
        };
    }

    const QFileInfo canonical(canonicalPath);
    if (directoryOnly && !canonical.isDir()) {
        return {
            .canonicalPath = {},
            .errorCode = ErrorCode::NotDirectory,
            .errorMessage = translated(
                "The selected path is not a directory."),
        };
    }
    if (!directoryOnly && !canonical.isDir() && !canonical.isFile()) {
        return {
            .canonicalPath = {},
            .errorCode = ErrorCode::UnsupportedFileType,
            .errorMessage = translated(
                "Only regular files and directories can be opened."),
        };
    }

#if defined(Q_OS_LINUX)
    const auto nativePath = QFile::encodeName(canonicalPath);
    if (::access(nativePath.constData(), R_OK) != 0) {
#else
    if (!canonical.isReadable()) {
#endif
        return {
            .canonicalPath = {},
            .errorCode = ErrorCode::UnreadablePath,
            .errorMessage = translated(
                "The selected filesystem path is not readable."),
        };
    }
    if (canonical.isDir()) {
#if defined(Q_OS_LINUX)
        if (::access(nativePath.constData(), X_OK) != 0) {
#else
        if (!canonical.isExecutable()) {
#endif
            return {
                .canonicalPath = {},
                .errorCode = ErrorCode::UnsearchableDirectory,
                .errorMessage = translated(
                    "The selected directory cannot be searched."),
            };
        }
    }

    return {
        .canonicalPath = canonicalPath,
        .errorCode = ErrorCode::None,
        .errorMessage = {},
    };
}

DesktopFileIntegration::ValidationResult
DesktopFileIntegration::validateRequestId(const QString& requestId)
{
    if (requestId.isEmpty()) {
        return {
            .canonicalPath = {},
            .errorCode = ErrorCode::InvalidRequest,
            .errorMessage = translated(
                "The desktop integration request has no identity."),
        };
    }
    if (requestId.size() > maximumRequestIdLength) {
        return {
            .canonicalPath = {},
            .errorCode = ErrorCode::TooLong,
            .errorMessage = translated(
                "The desktop integration request identity is too long."),
        };
    }
    if (requestId.contains(QChar::Null)) {
        return {
            .canonicalPath = {},
            .errorCode = ErrorCode::ContainsNul,
            .errorMessage = translated(
                "The desktop integration request identity is invalid."),
        };
    }
    return {
        .canonicalPath = {},
        .errorCode = ErrorCode::None,
        .errorMessage = {},
    };
}

bool DesktopFileIntegration::isPickerPurpose(const Purpose purpose) noexcept
{
    return purpose == Purpose::NewSessionWorkingDirectory
        || purpose == Purpose::SettingsWorkingDirectory;
}

bool DesktopFileIntegration::isSessionProjectPurpose(
    const Purpose purpose) noexcept
{
    return purpose == Purpose::SessionProject
        || purpose == Purpose::TerminalProject;
}

void DesktopFileIntegration::finishPicker()
{
    if (!m_busy) {
        return;
    }
    m_busy = false;
    m_activeRequestId.clear();
    emit busyChanged();
}

void DesktopFileIntegration::handlePickerResult(
    const QString& requestId,
    const Purpose purpose,
    DirectoryPicker::Result result)
{
    if (!m_busy || requestId != m_activeRequestId
        || purpose != m_activePurpose) {
        return;
    }
    finishPicker();

    if (result.outcome == DirectoryPicker::Outcome::Cancelled) {
        emit directoryPickCancelled(requestId, purpose);
        return;
    }
    if (result.outcome == DirectoryPicker::Outcome::Failed) {
        fail(
            requestId,
            purpose,
            ErrorCode::PickerFailed,
            result.error.isEmpty()
                ? translated("The system folder picker failed.")
                : std::move(result.error));
        return;
    }
    if (!result.selectedDirectory.isValid()
        || !result.selectedDirectory.isLocalFile()) {
        fail(
            requestId,
            purpose,
            ErrorCode::NonLocalPath,
            translated(
                "The folder picker returned a nonlocal location."));
        return;
    }
    const auto validation =
        validateDirectory(result.selectedDirectory.toLocalFile());
    if (!validation.valid()) {
        fail(
            requestId,
            purpose,
            validation.errorCode,
            validation.errorMessage);
        return;
    }

    clearErrorState();
    emit directoryPicked(
        requestId,
        purpose,
        validation.canonicalPath);
}

void DesktopFileIntegration::fail(
    const QString& requestId,
    const Purpose purpose,
    const ErrorCode code,
    QString message)
{
    const auto changed = m_errorCode != code
        || m_errorMessage != message
        || m_errorRequestId != requestId
        || m_errorPurpose != purpose;
    m_errorCode = code;
    m_errorMessage = std::move(message);
    m_errorRequestId = requestId;
    m_errorPurpose = purpose;
    if (changed) {
        emit errorChanged();
    }
    emit operationFailed(
        requestId,
        purpose,
        m_errorCode,
        m_errorMessage);
}

void DesktopFileIntegration::clearErrorState()
{
    if (m_errorCode == ErrorCode::None
        && m_errorMessage.isEmpty()
        && m_errorRequestId.isEmpty()) {
        return;
    }
    m_errorCode = ErrorCode::None;
    m_errorMessage.clear();
    m_errorRequestId.clear();
    m_errorPurpose = Purpose::NewSessionWorkingDirectory;
    emit errorChanged();
}

} // namespace kodosi
