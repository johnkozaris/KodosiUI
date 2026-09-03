#include "models/MissionDirectoryModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QTest>

class FakeMissionDispatcher final : public kodosi::CommandDispatcher {
public:
    QVector<QJsonObject> commands;
    bool reject = false;

    Result send(const kodosi::CommandLane lane, const QByteArrayView json) override
    {
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        if (lane != kodosi::CommandLane::Rooms || !document.isObject() || reject) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("Rejected"),
            });
        }
        commands.push_back(document.object());
        return {};
    }
};

class MissionDirectoryModelTest final : public QObject {
    Q_OBJECT

private slots:
    void refreshesAndProjectsDirectory();
    void projectsInvitationsAtomically();
    void fencesAccountsAndRuntimeGenerations();
    void rejectsMalformedDirectoryWithoutDestroyingState();
    void retainsSafeStaleDirectoryDuringRecovery();
    void refreshRequiresBothCurrentSnapshotHalves();
    void coalescesOverlappingRefreshes();
    void drainsFailedRefreshBeforeRetry();
};

namespace {

QByteArray auth(const QString& userId, const quint64 epoch)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("auth.ready")},
        {QStringLiteral("userId"), userId},
        {QStringLiteral("accountEpoch"), static_cast<qint64>(epoch)},
    }).toJson(QJsonDocument::Compact);
}

QByteArray roomEvent(
    const QString& userId,
    const quint64 epoch,
    QJsonObject event)
{
    event.insert(QStringLiteral("authority"), QStringLiteral("accountContext"));
    event.insert(QStringLiteral("accountUserId"), userId);
    event.insert(QStringLiteral("accountEpoch"), static_cast<qint64>(epoch));
    return QJsonDocument(event).toJson(QJsonDocument::Compact);
}

QJsonObject invitation(const QString& id)
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("roomId"), QStringLiteral("mission-1")},
        {QStringLiteral("roomName"), QStringLiteral("Launch")},
        {QStringLiteral("roomSlug"), QStringLiteral("launch")},
        {QStringLiteral("inviteeUserId"), QStringLiteral("me")},
        {QStringLiteral("inviteeHandle"), QStringLiteral("me")},
        {QStringLiteral("invitedByUserId"), QStringLiteral("owner")},
        {QStringLiteral("invitedByHandle"), QStringLiteral("alice")},
        {QStringLiteral("status"), QStringLiteral("Pending")},
        {QStringLiteral("baseRosterGeneration"), 1},
        {QStringLiteral("proposedRosterGeneration"), 2},
        {QStringLiteral("createdAt"), QStringLiteral("2026-08-31T08:00:00Z")},
    };
}

} // namespace

void MissionDirectoryModelTest::refreshesAndProjectsDirectory()
{
    FakeMissionDispatcher dispatcher;
    kodosi::MissionDirectoryModel model(dispatcher);
    model.ingestAuthEvent(auth(QStringLiteral("me"), 1));

    QCOMPARE(dispatcher.commands.size(), 2);
    QVERIFY(model.loading());
    model.ingestRoomEvent(roomEvent(
        QStringLiteral("me"),
        1,
        {
            {QStringLiteral("type"), QStringLiteral("room.snapshot")},
            {QStringLiteral("rooms"),
             QJsonArray {
                 QJsonObject {
                     {QStringLiteral("id"), QStringLiteral("mission-1")},
                     {QStringLiteral("name"), QStringLiteral("Launch")},
                     {QStringLiteral("slug"), QStringLiteral("launch")},
                     {QStringLiteral("ownerUserId"), QStringLiteral("me")},
                     {QStringLiteral("rosterGeneration"), 4},
                 },
             }},
        }));

    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.loading());
    model.ingestRoomEvent(roomEvent(
        QStringLiteral("me"),
        1,
        {
            {QStringLiteral("type"), QStringLiteral("room.invitations")},
            {QStringLiteral("incoming"), QJsonArray {}},
            {QStringLiteral("outgoing"), QJsonArray {}},
        }));
    QVERIFY(!model.loading());
    QCOMPARE(
        model.data(model.index(0), kodosi::MissionDirectoryModel::NameRole)
            .toString(),
        QStringLiteral("Launch"));
}

void MissionDirectoryModelTest::projectsInvitationsAtomically()
{
    FakeMissionDispatcher dispatcher;
    kodosi::MissionDirectoryModel model(dispatcher);
    QSignalSpy errors(&model, &kodosi::MissionDirectoryModel::decodeError);
    model.ingestAuthEvent(auth(QStringLiteral("me"), 1));
    model.ingestRoomEvent(roomEvent(
        QStringLiteral("me"),
        1,
        {
            {QStringLiteral("type"), QStringLiteral("room.invitations")},
            {QStringLiteral("incoming"), QJsonArray {invitation(QStringLiteral("i-1"))}},
            {QStringLiteral("outgoing"), QJsonArray {}},
        }));
    QCOMPARE(model.invitations()->rowCount(), 1);
    QCOMPARE(model.invitations()->incomingCount(), 1);

    auto duplicate = invitation(QStringLiteral("i-1"));
    model.ingestRoomEvent(roomEvent(
        QStringLiteral("me"),
        1,
        {
            {QStringLiteral("type"), QStringLiteral("room.invitations")},
            {QStringLiteral("incoming"), QJsonArray {duplicate}},
            {QStringLiteral("outgoing"), QJsonArray {duplicate}},
        }));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(model.invitations()->rowCount(), 1);

    auto invalidTimestamp = invitation(QStringLiteral("i-2"));
    invalidTimestamp.insert(
        QStringLiteral("createdAt"),
        QStringLiteral("2026-08-31T08:00Z"));
    model.ingestRoomEvent(roomEvent(
        QStringLiteral("me"),
        1,
        {
            {QStringLiteral("type"), QStringLiteral("room.invitations")},
            {QStringLiteral("incoming"), QJsonArray {invalidTimestamp}},
            {QStringLiteral("outgoing"), QJsonArray {}},
        }));
    QCOMPARE(errors.count(), 2);
    QCOMPARE(model.invitations()->rowCount(), 1);

    auto missingHandle = invitation(QStringLiteral("i-3"));
    missingHandle.remove(QStringLiteral("inviteeHandle"));
    model.ingestRoomEvent(roomEvent(
        QStringLiteral("me"),
        1,
        {
            {QStringLiteral("type"), QStringLiteral("room.invitations")},
            {QStringLiteral("incoming"), QJsonArray {missingHandle}},
            {QStringLiteral("outgoing"), QJsonArray {}},
        }));
    QCOMPARE(errors.count(), 3);
    QCOMPARE(model.invitations()->rowCount(), 1);
}

void MissionDirectoryModelTest::fencesAccountsAndRuntimeGenerations()
{
    FakeMissionDispatcher dispatcher;
    kodosi::MissionDirectoryModel model(dispatcher);
    model.ingestAuthEvent(auth(QStringLiteral("old"), 9));
    model.ingestRoomEvent(roomEvent(
        QStringLiteral("new"),
        10,
        {
            {QStringLiteral("type"), QStringLiteral("room.snapshot")},
            {QStringLiteral("rooms"), QJsonArray {}},
        }));
    model.resetRuntimeAuthority();
    model.ingestAuthEvent(auth(QStringLiteral("new"), 1));
    model.ingestRoomEvent(roomEvent(
        QStringLiteral("new"),
        1,
        {
            {QStringLiteral("type"), QStringLiteral("room.snapshot")},
            {QStringLiteral("rooms"),
             QJsonArray {
                 QJsonObject {
                     {QStringLiteral("id"), QStringLiteral("fresh")},
                     {QStringLiteral("name"), QStringLiteral("Fresh")},
                     {QStringLiteral("slug"), QStringLiteral("fresh")},
                     {QStringLiteral("ownerUserId"), QStringLiteral("new")},
                     {QStringLiteral("rosterGeneration"), 1},
                 },
             }},
        }));
    QCOMPARE(model.rowCount(), 1);
}

void MissionDirectoryModelTest::rejectsMalformedDirectoryWithoutDestroyingState()
{
    FakeMissionDispatcher dispatcher;
    kodosi::MissionDirectoryModel model(dispatcher);
    QSignalSpy errors(&model, &kodosi::MissionDirectoryModel::decodeError);
    model.ingestAuthEvent(auth(QStringLiteral("me"), 1));
    model.ingestRoomEvent(roomEvent(
        QStringLiteral("me"),
        1,
        {
            {QStringLiteral("type"), QStringLiteral("room.snapshot")},
            {QStringLiteral("rooms"),
             QJsonArray {
                 QJsonObject {
                     {QStringLiteral("id"), QStringLiteral("safe")},
                     {QStringLiteral("name"), QStringLiteral("Safe")},
                     {QStringLiteral("slug"), QStringLiteral("safe")},
                     {QStringLiteral("ownerUserId"), QStringLiteral("me")},
                     {QStringLiteral("rosterGeneration"), 1},
                 },
             }},
        }));
    model.ingestRoomEvent(roomEvent(
        QStringLiteral("me"),
        1,
        {
            {QStringLiteral("type"), QStringLiteral("room.snapshot")},
            {QStringLiteral("rooms"),
             QJsonArray {
                 QJsonObject {
                     {QStringLiteral("id"), QStringLiteral("bad")},
                     {QStringLiteral("name"), QStringLiteral("Bad")},
                     {QStringLiteral("slug"), QStringLiteral("bad")},
                     {QStringLiteral("ownerUserId"), QStringLiteral("me")},
                     {QStringLiteral("rosterGeneration"), 1.5},
                 },
             }},
        }));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(
        model.authorityState(),
        kodosi::MissionDirectoryModel::AuthorityState::Failed);
    QVERIFY(model.staleDataVisible());
}

void MissionDirectoryModelTest::retainsSafeStaleDirectoryDuringRecovery()
{
    FakeMissionDispatcher dispatcher;
    kodosi::MissionDirectoryModel model(dispatcher);
    model.ingestAuthEvent(auth(QStringLiteral("me"), 1));
    model.ingestRoomEvent(roomEvent(
        QStringLiteral("me"),
        1,
        {
            {QStringLiteral("type"), QStringLiteral("room.snapshot")},
            {QStringLiteral("rooms"),
             QJsonArray {
                 QJsonObject {
                     {QStringLiteral("id"), QStringLiteral("safe")},
                     {QStringLiteral("name"), QStringLiteral("Safe")},
                     {QStringLiteral("slug"), QStringLiteral("safe")},
                     {QStringLiteral("ownerUserId"), QStringLiteral("me")},
                     {QStringLiteral("rosterGeneration"), 1},
                 },
             }},
        }));
    model.ingestRoomEvent(roomEvent(
        QStringLiteral("me"),
        1,
        {
            {QStringLiteral("type"), QStringLiteral("room.invitations")},
            {QStringLiteral("incoming"), QJsonArray {}},
            {QStringLiteral("outgoing"), QJsonArray {}},
        }));

    QCOMPARE(
        model.authorityState(),
        kodosi::MissionDirectoryModel::AuthorityState::Loaded);
    QVERIFY(model.canRefresh());
    QVERIFY(!model.staleDataVisible());
    QVERIFY(!model.roleNames().values().contains(
        QByteArrayLiteral("ownerUserId")));
    QVERIFY(!model.roleNames().values().contains(
        QByteArrayLiteral("rosterGeneration")));

    dispatcher.reject = true;
    QVERIFY(!model.refresh());
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(
        model.authorityState(),
        kodosi::MissionDirectoryModel::AuthorityState::Failed);
    QVERIFY(model.staleDataVisible());
    QVERIFY(model.canRetry());

    dispatcher.reject = false;
    QVERIFY(model.refresh());
    QCOMPARE(
        model.authorityState(),
        kodosi::MissionDirectoryModel::AuthorityState::Recovering);
    QCOMPARE(model.rowCount(), 1);
}

void MissionDirectoryModelTest::refreshRequiresBothCurrentSnapshotHalves()
{
    FakeMissionDispatcher dispatcher;
    kodosi::MissionDirectoryModel model(dispatcher);
    const auto rooms = [] {
        return QJsonArray {
            QJsonObject {
                {QStringLiteral("id"), QStringLiteral("safe")},
                {QStringLiteral("name"), QStringLiteral("Safe")},
                {QStringLiteral("slug"), QStringLiteral("safe")},
                {QStringLiteral("ownerUserId"), QStringLiteral("me")},
                {QStringLiteral("rosterGeneration"), 1},
            },
        };
    };
    const auto sendRooms = [&] {
        model.ingestRoomEvent(roomEvent(
            QStringLiteral("me"),
            1,
            {
                {QStringLiteral("type"), QStringLiteral("room.snapshot")},
                {QStringLiteral("rooms"), rooms()},
            }));
    };
    const auto sendInvitations = [&] {
        model.ingestRoomEvent(roomEvent(
            QStringLiteral("me"),
            1,
            {
                {QStringLiteral("type"), QStringLiteral("room.invitations")},
                {QStringLiteral("incoming"), QJsonArray {}},
                {QStringLiteral("outgoing"), QJsonArray {}},
            }));
    };
    const auto sendError = [&](const QString& operation, const QString& message) {
        model.ingestRoomEvent(roomEvent(
            QStringLiteral("me"),
            1,
            {
                {QStringLiteral("type"), QStringLiteral("room.error")},
                {QStringLiteral("operation"), operation},
                {QStringLiteral("message"), message},
            }));
    };

    model.ingestAuthEvent(auth(QStringLiteral("me"), 1));
    sendRooms();
    sendInvitations();
    QCOMPARE(
        model.authorityState(),
        kodosi::MissionDirectoryModel::AuthorityState::Loaded);
    QCOMPARE(model.rowCount(), 1);

    QVERIFY(model.refresh());
    sendError(
        QStringLiteral("refresh"),
        QStringLiteral("room refresh failed"));
    sendInvitations();
    QCOMPARE(
        model.authorityState(),
        kodosi::MissionDirectoryModel::AuthorityState::Failed);
    QCOMPARE(model.lastError(), QStringLiteral("room refresh failed"));
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.staleDataVisible());

    QVERIFY(model.refresh());
    sendInvitations();
    QCOMPARE(
        model.authorityState(),
        kodosi::MissionDirectoryModel::AuthorityState::Recovering);
    sendRooms();
    QCOMPARE(
        model.authorityState(),
        kodosi::MissionDirectoryModel::AuthorityState::Loaded);
    QVERIFY(model.lastError().isEmpty());

    QVERIFY(model.refresh());
    sendRooms();
    sendError(
        QStringLiteral("refreshInvitations"),
        QStringLiteral("invitation refresh failed"));
    QCOMPARE(
        model.authorityState(),
        kodosi::MissionDirectoryModel::AuthorityState::Failed);
    QCOMPARE(
        model.lastError(),
        QStringLiteral("invitation refresh failed"));
    QCOMPARE(model.rowCount(), 1);

    QVERIFY(model.refresh());
    sendRooms();
    QCOMPARE(
        model.authorityState(),
        kodosi::MissionDirectoryModel::AuthorityState::Recovering);
    sendInvitations();
    QCOMPARE(
        model.authorityState(),
        kodosi::MissionDirectoryModel::AuthorityState::Loaded);
    QVERIFY(model.lastError().isEmpty());
}

void MissionDirectoryModelTest::coalescesOverlappingRefreshes()
{
    FakeMissionDispatcher dispatcher;
    kodosi::MissionDirectoryModel model(dispatcher);
    const auto sendRooms = [&] {
        model.ingestRoomEvent(roomEvent(
            QStringLiteral("me"),
            1,
            {
                {QStringLiteral("type"), QStringLiteral("room.snapshot")},
                {QStringLiteral("rooms"), QJsonArray {}},
            }));
    };
    const auto sendInvitations = [&] {
        model.ingestRoomEvent(roomEvent(
            QStringLiteral("me"),
            1,
            {
                {QStringLiteral("type"), QStringLiteral("room.invitations")},
                {QStringLiteral("incoming"), QJsonArray {}},
                {QStringLiteral("outgoing"), QJsonArray {}},
            }));
    };

    model.ingestAuthEvent(auth(QStringLiteral("me"), 1));
    QCOMPARE(dispatcher.commands.size(), 2);
    QVERIFY(model.loading());

    QVERIFY(model.refresh());
    QCOMPARE(dispatcher.commands.size(), 2);

    sendRooms();
    sendInvitations();
    QCOMPARE(dispatcher.commands.size(), 4);
    QVERIFY(model.loading());

    sendInvitations();
    sendRooms();
    QCOMPARE(
        model.authorityState(),
        kodosi::MissionDirectoryModel::AuthorityState::Loaded);
    QVERIFY(!model.loading());
}

void MissionDirectoryModelTest::drainsFailedRefreshBeforeRetry()
{
    FakeMissionDispatcher dispatcher;
    kodosi::MissionDirectoryModel model(dispatcher);
    const auto sendRooms = [&] {
        model.ingestRoomEvent(roomEvent(
            QStringLiteral("me"),
            1,
            {
                {QStringLiteral("type"), QStringLiteral("room.snapshot")},
                {QStringLiteral("rooms"), QJsonArray {}},
            }));
    };
    const auto sendInvitations = [&] {
        model.ingestRoomEvent(roomEvent(
            QStringLiteral("me"),
            1,
            {
                {QStringLiteral("type"), QStringLiteral("room.invitations")},
                {QStringLiteral("incoming"), QJsonArray {}},
                {QStringLiteral("outgoing"), QJsonArray {}},
            }));
    };

    model.ingestAuthEvent(auth(QStringLiteral("me"), 1));
    model.ingestRoomEvent(roomEvent(
        QStringLiteral("me"),
        1,
        {
            {QStringLiteral("type"), QStringLiteral("room.error")},
            {QStringLiteral("operation"), QStringLiteral("refresh")},
            {QStringLiteral("message"), QStringLiteral("refresh failed")},
        }));
    QCOMPARE(
        model.authorityState(),
        kodosi::MissionDirectoryModel::AuthorityState::Failed);
    QVERIFY(model.loading());
    QVERIFY(!model.canRetry());
    QVERIFY(!model.refresh());
    QCOMPARE(dispatcher.commands.size(), 2);

    sendInvitations();
    QVERIFY(!model.loading());
    QVERIFY(model.canRetry());

    QVERIFY(model.refresh());
    QCOMPARE(dispatcher.commands.size(), 4);
    sendRooms();
    QVERIFY(model.loading());
    sendInvitations();
    QCOMPARE(
        model.authorityState(),
        kodosi::MissionDirectoryModel::AuthorityState::Loaded);
    QVERIFY(!model.loading());
}

QTEST_APPLESS_MAIN(MissionDirectoryModelTest)

#include "tst_mission_directory_model.moc"
