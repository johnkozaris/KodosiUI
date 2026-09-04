#include "models/DesktopSettings.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <memory>

namespace {

constexpr auto structuredKey = "desktop/settings.v1";

std::unique_ptr<QSettings> settingsFor(const QTemporaryDir& directory)
{
    return std::make_unique<QSettings>(
        directory.filePath(QStringLiteral("settings.ini")),
        QSettings::IniFormat);
}

QByteArray storedSettings(
    const int fontSize = 14,
    const double lineHeight = 1.1,
    const int scrollbackLines = 10'000)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("version"), 1},
        {
            QStringLiteral("terminal"),
            QJsonObject {
                {QStringLiteral("fontFamily"), QStringLiteral("JetBrains Mono")},
                {QStringLiteral("fontSize"), fontSize},
                {QStringLiteral("cursorStyle"), QStringLiteral("block")},
                {QStringLiteral("lineHeight"), lineHeight},
                {QStringLiteral("scrollbackLines"), scrollbackLines},
                {QStringLiteral("cursorBlink"), false},
            },
        },
        {QStringLiteral("toolApprovalAlerts"), true},
        {QStringLiteral("lastWorkingDirectory"), QJsonValue::Null},
    }).toJson(QJsonDocument::Compact);
}

} // namespace

class DesktopSettingsTest final : public QObject {
    Q_OBJECT

private slots:
    void defaultsMatchSwiftParity();
    void malformedStructuredValueRecoversDefaults();
    void outOfRangeValueIsNormalized();
    void migratesLegacyValuesTransactionally();
    void applyIsAtomicAndResetPersistsDefaults();
    void workingDirectoryFallsBackWhenPersistedPathIsStale();
};

void DesktopSettingsTest::defaultsMatchSwiftParity()
{
    QTemporaryDir directory(
        QDir::current().filePath(QStringLiteral("desktop-settings-defaults-XXXXXX")));
    QVERIFY(directory.isValid());
    kodosi::DesktopSettings settings(settingsFor(directory));

    QCOMPARE(settings.fontFamily(), QStringLiteral("JetBrains Mono"));
    QCOMPARE(settings.fontSize(), 14);
    QCOMPARE(settings.cursorStyle(), kodosi::DesktopSettings::CursorStyle::Block);
    QCOMPARE(settings.lineHeight(), 1.1);
    QCOMPARE(settings.scrollbackLines(), 10'000);
    QVERIFY(!settings.cursorBlink());
    QVERIFY(settings.toolApprovalAlerts());
    QVERIFY(settings.lastWorkingDirectory().isEmpty());
    QVERIFY(settings.settingsError().isEmpty());
}

void DesktopSettingsTest::malformedStructuredValueRecoversDefaults()
{
    QTemporaryDir directory(
        QDir::current().filePath(QStringLiteral("desktop-settings-malformed-XXXXXX")));
    QVERIFY(directory.isValid());
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(structuredKey),
            QByteArrayLiteral("{\"version\":1,\"terminal\":false}"));
        storage->sync();
    }

    kodosi::DesktopSettings settings(settingsFor(directory));
    QCOMPARE(settings.fontFamily(), QStringLiteral("JetBrains Mono"));
    QCOMPARE(settings.fontSize(), 14);
    QCOMPARE(settings.scrollbackLines(), 10'000);
    QVERIFY(settings.settingsError().isEmpty());
}

void DesktopSettingsTest::outOfRangeValueIsNormalized()
{
    QTemporaryDir directory(
        QDir::current().filePath(QStringLiteral("desktop-settings-range-XXXXXX")));
    QVERIFY(directory.isValid());
    {
        auto storage = settingsFor(directory);
        storage->setValue(
            QString::fromLatin1(structuredKey),
            storedSettings(33, 1.4, 20'000));
        storage->sync();
    }

    kodosi::DesktopSettings settings(settingsFor(directory));
    QCOMPARE(settings.fontSize(), 32);
    QCOMPARE(settings.lineHeight(), 1.4);
    QCOMPARE(settings.scrollbackLines(), 20'000);
    QVERIFY(settings.settingsError().isEmpty());
}

void DesktopSettingsTest::migratesLegacyValuesTransactionally()
{
    QTemporaryDir directory(
        QDir::current().filePath(QStringLiteral("desktop-settings-migration-XXXXXX")));
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkdir(QStringLiteral("project")));
    const auto project = root.filePath(QStringLiteral("project"));
    {
        auto storage = settingsFor(directory);
        storage->setValue(QStringLiteral("term.fontFamily"), QStringLiteral("Iosevka"));
        storage->setValue(QStringLiteral("term.cursorStyle"), QStringLiteral("bar"));
        storage->setValue(QStringLiteral("term.fontSize"), 18);
        storage->setValue(QStringLiteral("term.lineHeight"), 1.4);
        storage->setValue(QStringLiteral("term.scrollbackLines"), 25'000);
        storage->setValue(QStringLiteral("term.cursorBlink"), true);
        storage->setValue(
            QStringLiteral("term.cursorBlinkEfficientDefaultApplied"),
            true);
        storage->setValue(QStringLiteral("session.lastWorkingDir"), project);
        storage->setValue(QStringLiteral("claude.toolApprovalAlerts"), false);
        storage->sync();
    }

    kodosi::DesktopSettings settings(settingsFor(directory));
    QCOMPARE(settings.fontFamily(), QStringLiteral("Iosevka"));
    QCOMPARE(settings.fontSize(), 18);
    QCOMPARE(settings.cursorStyle(), kodosi::DesktopSettings::CursorStyle::Bar);
    QCOMPARE(settings.lineHeight(), 1.4);
    QCOMPARE(settings.scrollbackLines(), 25'000);
    QVERIFY(settings.cursorBlink());
    QVERIFY(!settings.toolApprovalAlerts());
    QCOMPARE(settings.effectiveWorkingDirectory(), QFileInfo(project).canonicalFilePath());
    QVERIFY(settings.settingsError().isEmpty());

    auto storage = settingsFor(directory);
    QVERIFY(storage->contains(QString::fromLatin1(structuredKey)));
    QVERIFY(!storage->contains(QStringLiteral("term.fontFamily")));
    QVERIFY(!storage->contains(QStringLiteral("session.lastWorkingDir")));
}

void DesktopSettingsTest::applyIsAtomicAndResetPersistsDefaults()
{
    QTemporaryDir directory(
        QDir::current().filePath(QStringLiteral("desktop-settings-atomic-XXXXXX")));
    QVERIFY(directory.isValid());
    kodosi::DesktopSettings settings(settingsFor(directory));
    QSignalSpy changed(&settings, &kodosi::DesktopSettings::settingsChanged);

    QVERIFY(settings.apply(
        QStringLiteral("Iosevka"),
        20,
        static_cast<int>(kodosi::DesktopSettings::CursorStyle::Underline),
        1.5,
        30'000,
        true,
        false,
        directory.path()));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(settings.fontSize(), 20);
    QCOMPARE(settings.scrollbackLines(), 30'000);

    QVERIFY(settings.apply(
        QStringLiteral(""),
        40,
        99,
        7.0,
        1,
        false,
        true,
        QStringLiteral("/ignored")));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(settings.fontFamily(), QStringLiteral("JetBrains Mono"));
    QCOMPARE(settings.fontSize(), 32);
    QCOMPARE(
        settings.cursorStyle(),
        kodosi::DesktopSettings::CursorStyle::Block);
    QCOMPARE(settings.lineHeight(), 2.0);
    QCOMPARE(settings.scrollbackLines(), 100);

    kodosi::DesktopSettings reloaded(settingsFor(directory));
    QCOMPARE(reloaded.fontFamily(), QStringLiteral("JetBrains Mono"));
    QCOMPARE(reloaded.fontSize(), 32);
    QCOMPARE(reloaded.scrollbackLines(), 100);

    QVERIFY(settings.resetTerminal());
    QCOMPARE(changed.count(), 3);
    QCOMPARE(settings.fontFamily(), QStringLiteral("JetBrains Mono"));
    QVERIFY(settings.toolApprovalAlerts());
    QCOMPARE(settings.lastWorkingDirectory(), QStringLiteral("/ignored"));

    QVERIFY(settings.reset());
    QCOMPARE(changed.count(), 4);
    kodosi::DesktopSettings resetReloaded(settingsFor(directory));
    QCOMPARE(resetReloaded.fontFamily(), QStringLiteral("JetBrains Mono"));
    QCOMPARE(resetReloaded.fontSize(), 14);
    QCOMPARE(resetReloaded.scrollbackLines(), 10'000);
}

void DesktopSettingsTest::workingDirectoryFallsBackWhenPersistedPathIsStale()
{
    QTemporaryDir directory(
        QDir::current().filePath(QStringLiteral("desktop-settings-directory-XXXXXX")));
    QVERIFY(directory.isValid());
    const auto stale = directory.filePath(QStringLiteral("missing"));
    kodosi::DesktopSettings settings(settingsFor(directory));
    QVERIFY(settings.apply(
        QStringLiteral("JetBrains Mono"),
        14,
        static_cast<int>(kodosi::DesktopSettings::CursorStyle::Block),
        1.1,
        10'000,
        false,
        true,
        stale));

    QCOMPARE(settings.lastWorkingDirectory(), stale);
    QCOMPARE(settings.effectiveWorkingDirectory(), QDir::homePath());

    QDir root(directory.path());
    QVERIFY(root.mkdir(QStringLiteral("not-searchable")));
    const auto notSearchable =
        root.filePath(QStringLiteral("not-searchable"));
    QVERIFY(QFile::setPermissions(
        notSearchable,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QVERIFY(settings.apply(
        QStringLiteral("JetBrains Mono"),
        14,
        static_cast<int>(kodosi::DesktopSettings::CursorStyle::Block),
        1.1,
        10'000,
        false,
        true,
        notSearchable));
    QCOMPARE(settings.effectiveWorkingDirectory(), QDir::homePath());
    QVERIFY(QFile::setPermissions(
        notSearchable,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner
            | QFileDevice::ExeOwner));
}

QTEST_MAIN(DesktopSettingsTest)

#include "tst_desktop_settings.moc"
