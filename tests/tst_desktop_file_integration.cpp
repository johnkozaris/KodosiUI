#include "models/SessionCatalogModel.hpp"
#include "logging/ApplicationLogStore.hpp"
#include "platform/DesktopFileIntegration.hpp"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

#include <sys/stat.h>
#include <unistd.h>

#include <utility>

namespace {

using Integration = kodosi::DesktopFileIntegration;
using Purpose = Integration::Purpose;
using ErrorCode = Integration::ErrorCode;

class FakeDirectoryPicker final : public kodosi::DirectoryPicker {
public:
    void open(Request request, Completion completion) override
    {
        lastRequest = std::move(request);
        pendingCompletion = std::move(completion);
        ++openCount;
    }

    void complete(Result result)
    {
        QVERIFY(static_cast<bool>(pendingCompletion));
        auto completion = std::move(pendingCompletion);
        completion(std::move(result));
    }

    void cancel() override
    {
        ++cancelCount;
        complete({
            .outcome = Outcome::Cancelled,
        });
    }

    Request lastRequest;
    Completion pendingCompletion;
    int openCount = 0;
    int cancelCount = 0;
};

struct Fixture {
    QTemporaryDir directory {
        QDir::current().filePath(
            QStringLiteral("desktop-file-integration-XXXXXX"))
    };
    kodosi::SessionCatalogModel sessions;
    FakeDirectoryPicker* picker = nullptr;
    QList<QUrl> openedUrls;
    bool openerResult = true;
    std::unique_ptr<Integration> integration;

    Fixture()
    {
        auto pickerOwner = std::make_unique<FakeDirectoryPicker>();
        picker = pickerOwner.get();
        integration = std::make_unique<Integration>(
            sessions,
            std::move(pickerOwner),
            [this](const QUrl& url) {
                openedUrls.append(url);
                return openerResult;
            });
    }
};

QString createFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    if (file.write("kodosi") != 6) {
        return {};
    }
    file.close();
    return path;
}

void loadSession(
    kodosi::SessionCatalogModel& sessions,
    const QString& sessionId,
    const QString& kind,
    const QString& project)
{
    constexpr auto epoch = 44;
    sessions.ingestAuthEvent(
        QJsonDocument(QJsonObject {
            {QStringLiteral("type"), QStringLiteral("auth.required")},
            {QStringLiteral("accountEpoch"), epoch},
        }).toJson(QJsonDocument::Compact));
    sessions.ingestSessionEvent(
        QJsonDocument(QJsonObject {
            {QStringLiteral("authority"),
             QStringLiteral("accountContext")},
            {QStringLiteral("accountUserId"), QString {}},
            {QStringLiteral("accountEpoch"), epoch},
            {QStringLiteral("type"), QStringLiteral("session.list")},
            {QStringLiteral("sessions"),
             QJsonArray {
                 QJsonObject {
                     {QStringLiteral("kind"), kind},
                     {QStringLiteral("id"), sessionId},
                     {QStringLiteral("incarnationId"),
                      QStringLiteral(
                          "01900000-0000-7000-8000-000000000044")},
                     {QStringLiteral("name"), QStringLiteral("Project")},
                     {QStringLiteral("project"), project},
                     {QStringLiteral("mode"), QStringLiteral("normal")},
                     {QStringLiteral("status"), QStringLiteral("active")},
                     {QStringLiteral("recovery"), QStringLiteral("live")},
                     {QStringLiteral("scope"), QStringLiteral("justMe")},
                     {QStringLiteral("access"), QStringLiteral("approve")},
                 },
             }},
        }).toJson(QJsonDocument::Compact));
}

void updateSessionProject(
    kodosi::SessionCatalogModel& sessions,
    const QString& sessionId,
    const QString& kind,
    const QString& project)
{
    constexpr auto epoch = 44;
    sessions.ingestSessionEvent(
        QJsonDocument(QJsonObject {
            {QStringLiteral("authority"),
             QStringLiteral("accountContext")},
            {QStringLiteral("accountUserId"), QString {}},
            {QStringLiteral("accountEpoch"), epoch},
            {QStringLiteral("type"), QStringLiteral("session.upsert")},
            {QStringLiteral("session"),
             QJsonObject {
                 {QStringLiteral("kind"), kind},
                 {QStringLiteral("id"), sessionId},
                 {QStringLiteral("incarnationId"),
                  QStringLiteral(
                      "01900000-0000-7000-8000-000000000044")},
                 {QStringLiteral("name"), QStringLiteral("Project")},
                 {QStringLiteral("project"), project},
                 {QStringLiteral("mode"), QStringLiteral("normal")},
                 {QStringLiteral("status"), QStringLiteral("active")},
                 {QStringLiteral("recovery"), QStringLiteral("live")},
                 {QStringLiteral("scope"), QStringLiteral("justMe")},
                 {QStringLiteral("access"), QStringLiteral("approve")},
             }},
        }).toJson(QJsonDocument::Compact));
}

void removeSession(
    kodosi::SessionCatalogModel& sessions,
    const QString& sessionId)
{
    constexpr auto epoch = 44;
    sessions.ingestSessionEvent(
        QJsonDocument(QJsonObject {
            {QStringLiteral("authority"),
             QStringLiteral("accountContext")},
            {QStringLiteral("accountUserId"), QString {}},
            {QStringLiteral("accountEpoch"), epoch},
            {QStringLiteral("type"), QStringLiteral("session.removed")},
            {QStringLiteral("sessionId"), sessionId},
        }).toJson(QJsonDocument::Compact));
}

QFileDialog* activeFileDialog()
{
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (auto* dialog = qobject_cast<QFileDialog*>(widget);
            dialog != nullptr && dialog->isVisible()) {
            return dialog;
        }
    }
    return nullptr;
}

} // namespace

class DesktopFileIntegrationTest final : public QObject {
    Q_OBJECT

private slots:
    void validatesAndCanonicalizesDirectories();
    void resolvesFileAndDirectorySymlinks();
    void rejectsMissingNonlocalAndSpecialPaths();
    void rejectsUnreadableAndUnsearchablePaths();
    void rejectsOversizedAndNulInputs();
    void opensCanonicalPathsWithoutShells();
    void opensOnlyTheNativeApplicationLogDirectory();
    void reportsDeterministicOpenerFailure();
    void opensCurrentLocalSessionProjectAtInvocationTime();
    void rejectsRemovedRemoteAndArbitrarySessionPaths();
    void rejectsConcurrentPickersWithoutLosingTheFirstRequest();
    void cancelsOnlyTheExactActivePickerRequest();
    void reportsCancellationSeparatelyFromErrors();
    void strictlyValidatesAcceptedPickerResults();
    void preservesExactPickerRequestCorrelation();
    void treatsInitialDirectoryAsAResilientHint();
    void survivesDestroyedTransientParent();
    void nativePickerIsApplicationModalAndTransient();
    void nativePickerAcceptsDirectoryUrlFallback();
    void validatesSessionProjectAvailabilityNatively();
};

void DesktopFileIntegrationTest::validatesAndCanonicalizesDirectories()
{
    Fixture fixture;
    QVERIFY(fixture.directory.isValid());
    const auto nested = fixture.directory.filePath(
        QStringLiteral("one/../project"));
    QVERIFY(QDir().mkpath(nested));

    const auto result = Integration::validateDirectory(nested);
    QVERIFY(result.valid());
    QCOMPARE(
        result.canonicalPath,
        QFileInfo(nested).canonicalFilePath());

    const auto filePath = createFile(
        fixture.directory.filePath(QStringLiteral("regular.txt")));
    QVERIFY(!filePath.isEmpty());
    const auto fileResult = Integration::validateDirectory(filePath);
    QCOMPARE(fileResult.errorCode, ErrorCode::NotDirectory);
}

void DesktopFileIntegrationTest::resolvesFileAndDirectorySymlinks()
{
    Fixture fixture;
    const auto targetDirectory =
        fixture.directory.filePath(QStringLiteral("target-directory"));
    const auto targetFile =
        createFile(fixture.directory.filePath(QStringLiteral("target-file")));
    QVERIFY(QDir().mkpath(targetDirectory));
    QVERIFY(!targetFile.isEmpty());

    const auto directoryLink =
        fixture.directory.filePath(QStringLiteral("directory-link"));
    const auto fileLink =
        fixture.directory.filePath(QStringLiteral("file-link"));
    QVERIFY(QFile::link(targetDirectory, directoryLink));
    QVERIFY(QFile::link(targetFile, fileLink));

    const auto directoryResult =
        Integration::validateDirectory(directoryLink);
    QVERIFY(directoryResult.valid());
    QCOMPARE(
        directoryResult.canonicalPath,
        QFileInfo(targetDirectory).canonicalFilePath());

    const auto fileResult = Integration::validateOpenPath(fileLink);
    QVERIFY(fileResult.valid());
    QCOMPARE(
        fileResult.canonicalPath,
        QFileInfo(targetFile).canonicalFilePath());
}

void DesktopFileIntegrationTest::rejectsMissingNonlocalAndSpecialPaths()
{
    Fixture fixture;
    QCOMPARE(
        Integration::validateOpenPath(QStringLiteral("relative/path"))
            .errorCode,
        ErrorCode::NonLocalPath);
    QCOMPARE(
        Integration::validateOpenPath(QStringLiteral("https://example.com"))
            .errorCode,
        ErrorCode::NonLocalPath);
    QCOMPARE(
        Integration::validateOpenPath(
            fixture.directory.filePath(QStringLiteral("missing")))
            .errorCode,
        ErrorCode::MissingPath);

    const auto fifoPath =
        fixture.directory.filePath(QStringLiteral("named-pipe"));
    QCOMPARE(
        ::mkfifo(QFile::encodeName(fifoPath).constData(), 0600),
        0);
    QCOMPARE(
        Integration::validateOpenPath(fifoPath).errorCode,
        ErrorCode::UnsupportedFileType);
}

void DesktopFileIntegrationTest::rejectsUnreadableAndUnsearchablePaths()
{
    if (::geteuid() == 0) {
        QSKIP("Effective access checks require a non-root test process.");
    }
    Fixture fixture;
    const auto unreadableFile =
        createFile(fixture.directory.filePath(QStringLiteral("unreadable")));
    const auto unsearchableDirectory =
        fixture.directory.filePath(QStringLiteral("unsearchable"));
    const auto unreadableDirectory =
        fixture.directory.filePath(QStringLiteral("unreadable-directory"));
    QVERIFY(!unreadableFile.isEmpty());
    QVERIFY(QDir().mkpath(unsearchableDirectory));
    QVERIFY(QDir().mkpath(unreadableDirectory));

    QVERIFY(QFile::setPermissions(unreadableFile, QFile::Permissions {}));
    QCOMPARE(
        Integration::validateOpenPath(unreadableFile).errorCode,
        ErrorCode::UnreadablePath);

    QVERIFY(QFile::setPermissions(
        unsearchableDirectory,
        QFile::ReadOwner | QFile::WriteOwner));
    QCOMPARE(
        Integration::validateDirectory(unsearchableDirectory).errorCode,
        ErrorCode::UnsearchableDirectory);

    QVERIFY(QFile::setPermissions(
        unreadableDirectory,
        QFile::WriteOwner | QFile::ExeOwner));
    QCOMPARE(
        Integration::validateDirectory(unreadableDirectory).errorCode,
        ErrorCode::UnreadablePath);

    QVERIFY(QFile::setPermissions(
        unreadableFile,
        QFile::ReadOwner | QFile::WriteOwner));
    QVERIFY(QFile::setPermissions(
        unsearchableDirectory,
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    QVERIFY(QFile::setPermissions(
        unreadableDirectory,
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
}

void DesktopFileIntegrationTest::rejectsOversizedAndNulInputs()
{
    Fixture fixture;
    const auto oversized =
        QStringLiteral("/") + QString(4'096, QLatin1Char('a'));
    QCOMPARE(
        Integration::validateOpenPath(oversized).errorCode,
        ErrorCode::TooLong);

    auto nulPath = fixture.directory.path();
    nulPath.append(QChar::Null);
    nulPath.append(QStringLiteral("suffix"));
    QCOMPARE(
        Integration::validateOpenPath(nulPath).errorCode,
        ErrorCode::ContainsNul);

    QSignalSpy failures(
        fixture.integration.get(),
        &Integration::operationFailed);
    QVERIFY(!fixture.integration->requestDirectory(
        QString(257, QLatin1Char('r')),
        Purpose::SettingsWorkingDirectory,
        fixture.directory.path()));
    QCOMPARE(failures.size(), 1);
    QCOMPARE(
        failures.constFirst().at(2).value<ErrorCode>(),
        ErrorCode::TooLong);
}

void DesktopFileIntegrationTest::opensCanonicalPathsWithoutShells()
{
    Fixture fixture;
    const auto target =
        createFile(fixture.directory.filePath(QStringLiteral("target")));
    const auto link =
        fixture.directory.filePath(QStringLiteral("target-link"));
    QVERIFY(!target.isEmpty());
    QVERIFY(QFile::link(target, link));

    QSignalSpy opened(
        fixture.integration.get(),
        &Integration::pathOpened);
    QVERIFY(fixture.integration->openPath(
        link,
        QStringLiteral("open.file.1"),
        Purpose::SettingsOpenWorkingDirectory));
    QVERIFY(fixture.integration->openPath(
        fixture.directory.path(),
        QStringLiteral("open.directory.2"),
        Purpose::SettingsOpenWorkingDirectory));
    QCOMPARE(fixture.openedUrls.size(), 2);
    QVERIFY(fixture.openedUrls.constFirst().isLocalFile());
    QCOMPARE(
        fixture.openedUrls.constFirst().toLocalFile(),
        QFileInfo(target).canonicalFilePath());
    QCOMPARE(
        fixture.openedUrls.at(1).toLocalFile(),
        QFileInfo(fixture.directory.path()).canonicalFilePath());
    QCOMPARE(opened.size(), 2);
    QCOMPARE(
        opened.constFirst().at(0).toString(),
        QStringLiteral("open.file.1"));
}

void DesktopFileIntegrationTest::opensOnlyTheNativeApplicationLogDirectory()
{
    QTemporaryDir directory {
        QDir::current().filePath(
            QStringLiteral("desktop-log-integration-XXXXXX"))
    };
    QVERIFY(directory.isValid());
    kodosi::ApplicationLogStore logStore({
        .directory = directory.filePath(QStringLiteral("state/logs")),
        .installQtMessageHandler = false,
    });
    QVERIFY(logStore.healthy());

    kodosi::SessionCatalogModel sessions;
    auto picker = std::make_unique<FakeDirectoryPicker>();
    QList<QUrl> opened;
    Integration integration(
        sessions,
        std::move(picker),
        [&opened](const QUrl& url) {
            opened.append(url);
            return true;
        },
        &logStore);

    QVERIFY(integration.openLogDirectory(
        QStringLiteral("diagnostics.logs.1"),
        Purpose::DiagnosticsLogDirectory));
    QCOMPARE(opened.size(), 1);
    QCOMPARE(
        opened.constFirst().toLocalFile(),
        QFileInfo(logStore.directory()).canonicalFilePath());
    QVERIFY(!integration.openLogDirectory(
        QStringLiteral("diagnostics.logs.2"),
        Purpose::SettingsOpenWorkingDirectory));
    QVERIFY(!integration.openPath(
        directory.path(),
        QStringLiteral("diagnostics.logs.3"),
        Purpose::DiagnosticsLogDirectory));

    kodosi::ApplicationLogStore unhealthyStore({
        .directory = logStore.directory(),
        .rotationBytes = 1,
        .installQtMessageHandler = false,
    });
    QVERIFY(!unhealthyStore.healthy());
    QVERIFY(unhealthyStore.directoryAvailable());
    auto unhealthyPicker = std::make_unique<FakeDirectoryPicker>();
    Integration unhealthyIntegration(
        sessions,
        std::move(unhealthyPicker),
        [&opened](const QUrl& url) {
            opened.append(url);
            return true;
        },
        &unhealthyStore);
    QVERIFY(unhealthyIntegration.openLogDirectory(
        QStringLiteral("diagnostics.logs.unhealthy"),
        Purpose::DiagnosticsLogDirectory));
    QCOMPARE(opened.size(), 2);
}

void DesktopFileIntegrationTest::reportsDeterministicOpenerFailure()
{
    Fixture fixture;
    fixture.openerResult = false;
    QSignalSpy failures(
        fixture.integration.get(),
        &Integration::operationFailed);

    QVERIFY(!fixture.integration->openPath(
        fixture.directory.path(),
        QStringLiteral("open.directory.false"),
        Purpose::SettingsOpenWorkingDirectory));
    QCOMPARE(failures.size(), 1);
    QCOMPARE(
        failures.constFirst().at(2).value<ErrorCode>(),
        ErrorCode::LaunchFailed);
    QCOMPARE(fixture.integration->errorCode(), ErrorCode::LaunchFailed);
}

void DesktopFileIntegrationTest::
    opensCurrentLocalSessionProjectAtInvocationTime()
{
    Fixture fixture;
    const auto original =
        fixture.directory.filePath(QStringLiteral("original"));
    const auto current =
        fixture.directory.filePath(QStringLiteral("current"));
    QVERIFY(QDir().mkpath(original));
    QVERIFY(QDir().mkpath(current));
    loadSession(
        fixture.sessions,
        QStringLiteral("changing-project"),
        QStringLiteral("local"),
        original);
    updateSessionProject(
        fixture.sessions,
        QStringLiteral("changing-project"),
        QStringLiteral("local"),
        current);

    QVERIFY(fixture.integration->openSessionProject(
        QStringLiteral("changing-project"),
        QStringLiteral("session.open.current"),
        Purpose::SessionProject));
    QCOMPARE(fixture.openedUrls.size(), 1);
    QCOMPARE(
        fixture.openedUrls.constFirst().toLocalFile(),
        QFileInfo(current).canonicalFilePath());
}

void DesktopFileIntegrationTest::
    rejectsRemovedRemoteAndArbitrarySessionPaths()
{
    Fixture fixture;
    loadSession(
        fixture.sessions,
        QStringLiteral("removed-project"),
        QStringLiteral("local"),
        fixture.directory.path());
    removeSession(
        fixture.sessions,
        QStringLiteral("removed-project"));
    QVERIFY(!fixture.integration->openSessionProject(
        QStringLiteral("removed-project"),
        QStringLiteral("session.open.removed"),
        Purpose::SessionProject));

    loadSession(
        fixture.sessions,
        QStringLiteral("remote-project"),
        QStringLiteral("remote"),
        fixture.directory.path());
    QVERIFY(!fixture.integration->openSessionProject(
        QStringLiteral("remote-project"),
        QStringLiteral("session.open.remote"),
        Purpose::TerminalProject));

    QVERIFY(!fixture.integration->openPath(
        fixture.directory.path(),
        QStringLiteral("session.open.arbitrary"),
        Purpose::SessionProject));
    QCOMPARE(fixture.openedUrls.size(), 0);
    QCOMPARE(fixture.integration->errorCode(), ErrorCode::InvalidRequest);
}

void DesktopFileIntegrationTest::
    rejectsConcurrentPickersWithoutLosingTheFirstRequest()
{
    Fixture fixture;
    QSignalSpy failures(
        fixture.integration.get(),
        &Integration::operationFailed);
    QSignalSpy picked(
        fixture.integration.get(),
        &Integration::directoryPicked);

    QVERIFY(fixture.integration->requestDirectory(
        QStringLiteral("picker.first"),
        Purpose::NewSessionWorkingDirectory,
        fixture.directory.path()));
    QVERIFY(fixture.integration->busy());
    QCOMPARE(fixture.picker->openCount, 1);

    QVERIFY(!fixture.integration->requestDirectory(
        QStringLiteral("picker.second"),
        Purpose::SettingsWorkingDirectory,
        fixture.directory.path()));
    QVERIFY(fixture.integration->busy());
    QCOMPARE(fixture.picker->openCount, 1);
    QCOMPARE(failures.size(), 1);
    QCOMPARE(
        failures.constFirst().at(0).toString(),
        QStringLiteral("picker.second"));
    QCOMPARE(
        failures.constFirst().at(2).value<ErrorCode>(),
        ErrorCode::Busy);

    fixture.picker->complete({
        .outcome = kodosi::DirectoryPicker::Outcome::Selected,
        .selectedDirectory =
            QUrl::fromLocalFile(fixture.directory.path()),
    });
    QVERIFY(!fixture.integration->busy());
    QCOMPARE(picked.size(), 1);
    QCOMPARE(
        picked.constFirst().at(0).toString(),
        QStringLiteral("picker.first"));
    QCOMPARE(
        picked.constFirst().at(1).value<Purpose>(),
        Purpose::NewSessionWorkingDirectory);
}

void DesktopFileIntegrationTest::reportsCancellationSeparatelyFromErrors()
{
    Fixture fixture;
    QSignalSpy cancelled(
        fixture.integration.get(),
        &Integration::directoryPickCancelled);
    QSignalSpy failures(
        fixture.integration.get(),
        &Integration::operationFailed);

    QVERIFY(fixture.integration->requestDirectory(
        QStringLiteral("picker.cancel"),
        Purpose::SettingsWorkingDirectory,
        fixture.directory.path()));
    fixture.picker->complete({
        .outcome = kodosi::DirectoryPicker::Outcome::Cancelled,
    });

    QVERIFY(!fixture.integration->busy());
    QCOMPARE(cancelled.size(), 1);
    QCOMPARE(failures.size(), 0);
    QCOMPARE(fixture.integration->errorCode(), ErrorCode::None);
}

void DesktopFileIntegrationTest::strictlyValidatesAcceptedPickerResults()
{
    Fixture fixture;
    QSignalSpy failures(
        fixture.integration.get(),
        &Integration::operationFailed);

    QVERIFY(fixture.integration->requestDirectory(
        QStringLiteral("picker.remote"),
        Purpose::SettingsWorkingDirectory,
        fixture.directory.path()));
    fixture.picker->complete({
        .outcome = kodosi::DirectoryPicker::Outcome::Selected,
        .selectedDirectory =
            QUrl(QStringLiteral("https://example.com/project")),
    });
    QVERIFY(!fixture.integration->busy());
    QCOMPARE(fixture.integration->errorCode(), ErrorCode::NonLocalPath);

    QVERIFY(fixture.integration->requestDirectory(
        QStringLiteral("picker.missing"),
        Purpose::SettingsWorkingDirectory,
        fixture.directory.path()));
    fixture.picker->complete({
        .outcome = kodosi::DirectoryPicker::Outcome::Selected,
        .selectedDirectory = QUrl::fromLocalFile(
            fixture.directory.filePath(QStringLiteral("missing"))),
    });
    QVERIFY(!fixture.integration->busy());
    QCOMPARE(fixture.integration->errorCode(), ErrorCode::MissingPath);

    QVERIFY(fixture.integration->requestDirectory(
        QStringLiteral("picker.failed"),
        Purpose::SettingsWorkingDirectory,
        fixture.directory.path()));
    fixture.picker->complete({
        .outcome = kodosi::DirectoryPicker::Outcome::Failed,
        .error = QStringLiteral("Native picker failed"),
    });
    QVERIFY(!fixture.integration->busy());
    QCOMPARE(fixture.integration->errorCode(), ErrorCode::PickerFailed);
    QCOMPARE(failures.size(), 3);
}

void DesktopFileIntegrationTest::cancelsOnlyTheExactActivePickerRequest()
{
    Fixture fixture;
    QSignalSpy cancelled(
        fixture.integration.get(),
        &Integration::directoryPickCancelled);

    QVERIFY(fixture.integration->requestDirectory(
        QStringLiteral("picker.current"),
        Purpose::SettingsWorkingDirectory,
        fixture.directory.path()));
    QVERIFY(!fixture.integration->cancelDirectory(
        QStringLiteral("picker.stale"),
        Purpose::SettingsWorkingDirectory));
    QVERIFY(!fixture.integration->cancelDirectory(
        QStringLiteral("picker.current"),
        Purpose::NewSessionWorkingDirectory));
    QVERIFY(fixture.integration->busy());
    QCOMPARE(fixture.picker->cancelCount, 0);

    QVERIFY(fixture.integration->cancelDirectory(
        QStringLiteral("picker.current"),
        Purpose::SettingsWorkingDirectory));
    QVERIFY(!fixture.integration->busy());
    QCOMPARE(fixture.picker->cancelCount, 1);
    QCOMPARE(cancelled.size(), 1);
    QVERIFY(!fixture.integration->cancelDirectory(
        QStringLiteral("picker.current"),
        Purpose::SettingsWorkingDirectory));
    QCOMPARE(fixture.picker->cancelCount, 1);
}

void DesktopFileIntegrationTest::preservesExactPickerRequestCorrelation()
{
    Fixture fixture;
    QWindow transientParent;
    fixture.integration->setTransientParent(&transientParent);
    const auto selected =
        fixture.directory.filePath(QStringLiteral("selected"));
    QVERIFY(QDir().mkpath(selected));
    QSignalSpy picked(
        fixture.integration.get(),
        &Integration::directoryPicked);

    QVERIFY(fixture.integration->requestDirectory(
        QStringLiteral("settings.sessions.directory.42"),
        Purpose::SettingsWorkingDirectory,
        fixture.directory.path()));
    QCOMPARE(
        fixture.picker->lastRequest.initialDirectory.toLocalFile(),
        QFileInfo(fixture.directory.path()).canonicalFilePath());
    QCOMPARE(
        fixture.picker->lastRequest.transientParent.data(),
        &transientParent);
    QVERIFY(fixture.picker->lastRequest.message.contains(
        QStringLiteral("default working directory")));

    fixture.picker->complete({
        .outcome = kodosi::DirectoryPicker::Outcome::Selected,
        .selectedDirectory = QUrl::fromLocalFile(selected),
    });
    QCOMPARE(picked.size(), 1);
    QCOMPARE(
        picked.constFirst().at(0).toString(),
        QStringLiteral("settings.sessions.directory.42"));
    QCOMPARE(
        picked.constFirst().at(1).value<Purpose>(),
        Purpose::SettingsWorkingDirectory);
    QCOMPARE(
        picked.constFirst().at(2).toString(),
        QFileInfo(selected).canonicalFilePath());
}

void DesktopFileIntegrationTest::treatsInitialDirectoryAsAResilientHint()
{
    Fixture fixture;
    const auto previousHome = qgetenv("HOME");
    const auto restoreHome = qScopeGuard([&] {
        qputenv("HOME", previousHome);
    });
    qputenv("HOME", fixture.directory.path().toUtf8());

    const auto project =
        fixture.directory.filePath(QStringLiteral("project"));
    const auto deleted =
        QDir(project).filePath(QStringLiteral("deleted/child"));
    QVERIFY(QDir().mkpath(project));
    QCOMPARE(
        Integration::resolveInitialDirectoryHint(deleted),
        QFileInfo(project).canonicalFilePath());

    QCOMPARE(
        Integration::resolveInitialDirectoryHint(
            QStringLiteral("~/project")),
        QFileInfo(project).canonicalFilePath());
    QCOMPARE(
        Integration::resolveInitialDirectoryHint(
            QStringLiteral("relative/garbage")),
        QFileInfo(fixture.directory.path()).canonicalFilePath());

    const auto unavailableHome =
        fixture.directory.filePath(QStringLiteral("missing/home"));
    qputenv("HOME", unavailableHome.toUtf8());
    const auto fallback =
        Integration::resolveInitialDirectoryHint(
            QStringLiteral("still/relative"));
    QVERIFY(Integration::validateDirectory(fallback).valid());
    QCOMPARE(
        fallback,
        QFileInfo(fixture.directory.path()).canonicalFilePath());

    QSignalSpy failures(
        fixture.integration.get(),
        &Integration::operationFailed);
    QVERIFY(fixture.integration->requestDirectory(
        QStringLiteral("picker.invalid.hint"),
        Purpose::SettingsWorkingDirectory,
        QStringLiteral("https://invalid.example/path")));
    QCOMPARE(fixture.picker->openCount, 1);
    QCOMPARE(failures.size(), 0);
    QVERIFY(
        Integration::validateDirectory(
            fixture.picker->lastRequest.initialDirectory.toLocalFile())
            .valid());
    QVERIFY(fixture.integration->cancelDirectory(
        QStringLiteral("picker.invalid.hint"),
        Purpose::SettingsWorkingDirectory));
}

void DesktopFileIntegrationTest::survivesDestroyedTransientParent()
{
    Fixture fixture;
    auto parent = std::make_unique<QWindow>();
    fixture.integration->setTransientParent(parent.get());
    parent.reset();

    QVERIFY(fixture.integration->requestDirectory(
        QStringLiteral("picker.destroyed.parent"),
        Purpose::NewSessionWorkingDirectory,
        fixture.directory.path()));
    QVERIFY(fixture.picker->lastRequest.transientParent.isNull());
    QVERIFY(fixture.integration->cancelDirectory(
        QStringLiteral("picker.destroyed.parent"),
        Purpose::NewSessionWorkingDirectory));
    QVERIFY(!fixture.integration->busy());
}

void DesktopFileIntegrationTest::nativePickerIsApplicationModalAndTransient()
{
    Fixture fixture;
    QWindow parent;
    parent.create();
    parent.show();

    auto picker = kodosi::createNativeDirectoryPicker();
    bool completed = false;
    kodosi::DirectoryPicker::Result result;
    picker->open(
        {
            .title = QStringLiteral("Choose Project"),
            .message = QStringLiteral("Choose a project directory"),
            .acceptLabel = QStringLiteral("Choose"),
            .initialDirectory =
                QUrl::fromLocalFile(fixture.directory.path()),
            .transientParent = &parent,
        },
        [&](kodosi::DirectoryPicker::Result pickerResult) {
            completed = true;
            result = std::move(pickerResult);
        });

    QTRY_VERIFY(activeFileDialog() != nullptr);
    auto* dialog = activeFileDialog();
    QVERIFY(dialog != nullptr);
    QCOMPARE(dialog->windowModality(), Qt::ApplicationModal);
    QVERIFY(dialog->windowHandle() != nullptr);
    QCOMPARE(dialog->windowHandle()->transientParent(), &parent);

    picker->cancel();
    QTRY_VERIFY(completed);
    QCOMPARE(result.outcome, kodosi::DirectoryPicker::Outcome::Cancelled);
}

void DesktopFileIntegrationTest::nativePickerAcceptsDirectoryUrlFallback()
{
    Fixture fixture;
    auto picker = kodosi::createNativeDirectoryPicker();
    bool completed = false;
    kodosi::DirectoryPicker::Result result;
    picker->open(
        {
            .title = QStringLiteral("Choose Project"),
            .message = QStringLiteral("Choose a project directory"),
            .acceptLabel = QStringLiteral("Choose"),
            .initialDirectory =
                QUrl::fromLocalFile(fixture.directory.path()),
            .transientParent = nullptr,
        },
        [&](kodosi::DirectoryPicker::Result pickerResult) {
            completed = true;
            result = std::move(pickerResult);
        });

    QTRY_VERIFY(activeFileDialog() != nullptr);
    auto* dialog = activeFileDialog();
    QVERIFY(dialog != nullptr);
    dialog->setDirectoryUrl(
        QUrl::fromLocalFile(fixture.directory.path()));
    static_cast<QDialog*>(dialog)->done(QDialog::Accepted);

    QTRY_VERIFY(completed);
    QCOMPARE(result.outcome, kodosi::DirectoryPicker::Outcome::Selected);
    QVERIFY(result.selectedDirectory.isLocalFile());
    QCOMPARE(
        QFileInfo(result.selectedDirectory.toLocalFile())
            .canonicalFilePath(),
        QFileInfo(fixture.directory.path()).canonicalFilePath());
}

void DesktopFileIntegrationTest::validatesSessionProjectAvailabilityNatively()
{
    Fixture local;
    loadSession(
        local.sessions,
        QStringLiteral("local-project"),
        QStringLiteral("local"),
        local.directory.path());
    QVERIFY(local.integration->canOpenSessionProject(
        QStringLiteral("local-project")));

    Fixture remote;
    loadSession(
        remote.sessions,
        QStringLiteral("remote-project"),
        QStringLiteral("remote"),
        remote.directory.path());
    QVERIFY(!remote.integration->canOpenSessionProject(
        QStringLiteral("remote-project")));

    Fixture missing;
    loadSession(
        missing.sessions,
        QStringLiteral("missing-project"),
        QStringLiteral("local"),
        missing.directory.filePath(QStringLiteral("missing")));
    QVERIFY(!missing.integration->canOpenSessionProject(
        QStringLiteral("missing-project")));
}

QTEST_MAIN(DesktopFileIntegrationTest)

#include "tst_desktop_file_integration.moc"
