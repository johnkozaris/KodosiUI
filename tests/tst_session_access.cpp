#include "models/PeopleModel.hpp"
#include "models/SessionAccess.hpp"
#include "models/SessionActions.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest/QTest>

#include <ranges>

class FakeAccessDispatcher final : public kodosi::CommandDispatcher {
public:
    QVector<QJsonObject> commands;
    QString rejectType;
    int rejectionsRemaining = 0;

    Result send(
        const kodosi::CommandLane lane,
        const QByteArrayView json) override
    {
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        const auto type = document.isObject()
            ? document.object().value(QStringLiteral("type")).toString()
            : QString {};
        if (lane != kodosi::CommandLane::Sessions || !document.isObject()
            || (type == rejectType && rejectionsRemaining-- > 0)) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("Rejected"),
            });
        }
        commands.push_back(document.object());
        return {};
    }

    [[nodiscard]] int count(const QString& type) const
    {
        return static_cast<int>(std::ranges::count(
            commands,
            type,
            [](const QJsonObject& command) {
                return command.value(QStringLiteral("type")).toString();
            }));
    }

    [[nodiscard]] QJsonObject last(const QString& type) const
    {
        for (auto command = commands.crbegin();
             command != commands.crend();
             ++command) {
            if (command->value(QStringLiteral("type")).toString() == type) {
                return *command;
            }
        }
        return {};
    }
};

class SessionAccessTest final : public QObject {
    Q_OBJECT

private slots:
    void recoversOncePerAuthenticatedContextAndResets();
    void projectsExactFencedGrantRowsWithoutAuthorityRoles();
    void rejectsMalformedDuplicateAndStaleProjectionAtomically();
    void sendsExactGrantAndRevokeCommandsAfterNativeFriendResolution();
    void rejectsReplacementUnsharedSelfUnknownAndConflictingTargets();
    void terminalGrantSurvivesReplacementBeforeRecovery();
    void reconcilesGrantAndRevokeThroughProjectionAndExactAcknowledgment();
    void preservesDurableLeaveAcrossReceiptOrderingAndAckFailures();
    void verifiesDroppedAcknowledgmentBeforeSettling();
    void suppressesMarkerPayloadLoopsAndRetriesSameMutationId();
};

namespace {

QByteArray authEvent(
    const QString& userId = QStringLiteral("me"),
    const quint64 epoch = 1)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("type"),
         userId.isEmpty()
             ? QStringLiteral("auth.required")
             : QStringLiteral("auth.ready")},
        {QStringLiteral("userId"), userId},
        {QStringLiteral("accountEpoch"), static_cast<qint64>(epoch)},
    }).toJson(QJsonDocument::Compact);
}

QByteArray fenced(
    QJsonObject event,
    const QString& userId = QStringLiteral("me"),
    const quint64 epoch = 1)
{
    event.insert(QStringLiteral("authority"), QStringLiteral("accountContext"));
    event.insert(QStringLiteral("accountUserId"), userId);
    event.insert(
        QStringLiteral("accountEpoch"),
        static_cast<qint64>(epoch));
    return QJsonDocument(event).toJson(QJsonDocument::Compact);
}

QJsonObject sessionObject(
    const QString& id,
    const QString& incarnation,
    const QString& kind = QStringLiteral("local"),
    const QString& scope = QStringLiteral("room"),
    const QString& status = QStringLiteral("active"))
{
    QJsonObject session {
        {QStringLiteral("kind"), kind},
        {QStringLiteral("id"), id},
        {QStringLiteral("incarnationId"), incarnation},
        {QStringLiteral("name"), QStringLiteral("Agent")},
        {QStringLiteral("project"), QStringLiteral("/repo")},
        {QStringLiteral("mode"), QStringLiteral("normal")},
        {QStringLiteral("status"), status},
        {QStringLiteral("scope"), scope},
        {QStringLiteral("access"), QStringLiteral("approve")},
    };
    if (kind == QStringLiteral("local")) {
        session.insert(QStringLiteral("recovery"), QStringLiteral("live"));
        if (scope == QStringLiteral("room")) {
            session.insert(QStringLiteral("roomId"), QStringLiteral("mission"));
            session.insert(QStringLiteral("roomName"), QStringLiteral("Mission"));
        }
    } else {
        session.insert(QStringLiteral("owner"), QStringLiteral("Alice"));
        session.insert(QStringLiteral("ownerUserId"), QStringLiteral("alice-id"));
        session.insert(QStringLiteral("permissions"), 1);
        session.insert(QStringLiteral("connectionState"), QStringLiteral("offline"));
        session.insert(QStringLiteral("accessState"), QStringLiteral("ready"));
    }
    return session;
}

void authenticate(
    kodosi::SessionCatalogModel& sessions,
    kodosi::PeopleModel& people,
    kodosi::SessionAccess& access,
    const QString& userId = QStringLiteral("me"),
    const quint64 epoch = 1)
{
    const auto auth = authEvent(userId, epoch);
    sessions.ingestAuthEvent(auth);
    people.ingestAuthEvent(auth);
    access.ingestAuthEvent(auth);
}

void installFriends(
    kodosi::PeopleModel& people,
    const QString& userId = QStringLiteral("me"),
    const quint64 epoch = 1,
    const bool includeSelf = false)
{
    QJsonArray friends {
        QJsonObject {
            {QStringLiteral("userId"), QStringLiteral("alice-id")},
            {QStringLiteral("handle"), QStringLiteral("Alice")},
            {QStringLiteral("displayName"), QStringLiteral("Alice A")},
        },
        QJsonObject {
            {QStringLiteral("userId"), QStringLiteral("bob-id")},
            {QStringLiteral("handle"), QStringLiteral("bob")},
            {QStringLiteral("displayName"), QStringLiteral("Bob B")},
        },
    };
    if (includeSelf) {
        friends.push_back(QJsonObject {
            {QStringLiteral("userId"), userId},
            {QStringLiteral("handle"), QStringLiteral("self")},
            {QStringLiteral("displayName"), QStringLiteral("Me")},
        });
    }
    people.ingestFriendsEvent(fenced(
        {
            {QStringLiteral("type"), QStringLiteral("friends.snapshot")},
            {QStringLiteral("friends"), friends},
            {QStringLiteral("incoming"), QJsonArray {}},
            {QStringLiteral("outgoing"), QJsonArray {}},
        },
        userId,
        epoch));
}

void upsert(
    kodosi::SessionCatalogModel& sessions,
    QJsonObject session,
    const QString& userId = QStringLiteral("me"),
    const quint64 epoch = 1)
{
    sessions.ingestSessionEvent(fenced(
        {
            {QStringLiteral("type"), QStringLiteral("session.upsert")},
            {QStringLiteral("session"), std::move(session)},
        },
        userId,
        epoch));
}

void snapshot(
    kodosi::SessionCatalogModel& sessions,
    QJsonArray rows,
    const QString& userId = QStringLiteral("me"),
    const quint64 epoch = 1)
{
    sessions.ingestSessionEvent(fenced(
        {
            {QStringLiteral("type"), QStringLiteral("session.list")},
            {QStringLiteral("sessions"), std::move(rows)},
        },
        userId,
        epoch));
}

QJsonObject grantRow(
    const QString& actor = QStringLiteral("alice-id"),
    const QString& handle = QStringLiteral("alice"),
    const QString& level = QStringLiteral("view"),
    const QString& expiresAt = QStringLiteral("2026-09-01T20:25:29.595Z"))
{
    QJsonObject row {
        {QStringLiteral("actorUserId"), actor},
        {QStringLiteral("handle"), handle},
        {QStringLiteral("displayName"), QStringLiteral("Alice A")},
        {QStringLiteral("accessLevel"), level},
        {QStringLiteral("grantedAt"), QStringLiteral("2026-08-31T20:25:29Z")},
    };
    if (!expiresAt.isNull()) {
        row.insert(QStringLiteral("expiresAt"), expiresAt);
    }
    return row;
}

QString minimallyFractionalRfc3339(QString value)
{
    while (value.endsWith(QStringLiteral("0Z"))) {
        value.remove(value.size() - 2, 1);
    }
    if (value.endsWith(QStringLiteral(".Z"))) {
        value.remove(value.size() - 2, 1);
    }
    return value;
}

QByteArray grantsEvent(
    const QString& sessionId,
    const QString& incarnation,
    QJsonArray grants,
    const QString& account = QStringLiteral("me"),
    const quint64 epoch = 1)
{
    return fenced(
        {
            {QStringLiteral("type"), QStringLiteral("session.accessGrants")},
            {QStringLiteral("sessionId"), sessionId},
            {QStringLiteral("runtimeIncarnationId"), incarnation},
            {QStringLiteral("accountUserId"), account},
            {QStringLiteral("grants"), std::move(grants)},
        },
        account,
        epoch);
}

QByteArray mutationEvent(
    const QString& type,
    const QString& mutationId,
    const QString& sessionId,
    const QString& incarnation,
    const QString& kind,
    const QString& actor = {},
    const QString& level = {},
    const QString& expiresAt = {},
    const QString& outcome = {},
    const QString& fingerprint = {},
    const QString& message = {})
{
    QJsonObject event {
        {QStringLiteral("type"), type},
        {QStringLiteral("mutationId"), mutationId},
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("expectedRuntimeIncarnationId"), incarnation},
        {QStringLiteral("kind"), kind},
    };
    if (!actor.isEmpty()) {
        event.insert(QStringLiteral("actorUserId"), actor);
    }
    if (!level.isEmpty()) {
        event.insert(QStringLiteral("accessLevel"), level);
    }
    if (!expiresAt.isEmpty()) {
        event.insert(QStringLiteral("expiresAt"), expiresAt);
    }
    if (!outcome.isEmpty()) {
        event.insert(QStringLiteral("outcome"), outcome);
    }
    if (!fingerprint.isEmpty()) {
        event.insert(QStringLiteral("originatingAccountEpoch"), 1);
        event.insert(QStringLiteral("fingerprint"), fingerprint);
    }
    if (!message.isEmpty()) {
        event.insert(QStringLiteral("message"), message);
    }
    return fenced(std::move(event));
}

} // namespace

void SessionAccessTest::recoversOncePerAuthenticatedContextAndResets()
{
    FakeAccessDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::PeopleModel people;
    kodosi::SessionAccess access(dispatcher, sessions, people);

    authenticate(sessions, people, access);
    QCOMPARE(
        dispatcher.count(QStringLiteral("session.accessMutationsRecover")),
        1);
    access.ingestAuthEvent(authEvent());
    QCOMPARE(
        dispatcher.count(QStringLiteral("session.accessMutationsRecover")),
        1);

    authenticate(sessions, people, access, QStringLiteral("other"), 2);
    QCOMPARE(
        dispatcher.count(QStringLiteral("session.accessMutationsRecover")),
        2);
    access.resetRuntimeAuthority();
    QVERIFY(access.pendingLeaveSessionIds().isEmpty());
    QCOMPARE(access.grants()->rowCount(), 0);
    QVERIFY(!access.canManage(QStringLiteral("session")));
}

void SessionAccessTest::projectsExactFencedGrantRowsWithoutAuthorityRoles()
{
    FakeAccessDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::PeopleModel people;
    kodosi::SessionAccess access(dispatcher, sessions, people);
    authenticate(sessions, people, access);
    installFriends(people);
    upsert(
        sessions,
        sessionObject(QStringLiteral("local"), QStringLiteral("inc-1")));

    QVERIFY(access.inspect(QStringLiteral("local")));
    QCOMPARE(
        dispatcher.last(QStringLiteral("session.listAccess"))
            .value(QStringLiteral("expectedRuntimeIncarnationId"))
            .toString(),
        QStringLiteral("inc-1"));
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QJsonArray {grantRow()}));
    QCOMPARE(access.grants()->rowCount(), 1);
    QCOMPARE(
        access.grants()
            ->data(
                access.grants()->index(0),
                kodosi::SessionAccessGrantsModel::HandleRole)
            .toString(),
        QStringLiteral("alice"));
    QCOMPARE(
        access.grants()
            ->data(
                access.grants()->index(0),
                kodosi::SessionAccessGrantsModel::AccessLevelRole)
            .toString(),
        QStringLiteral("view"));
    const auto roles = access.grants()->roleNames().values();
    QVERIFY(!roles.contains(QByteArrayLiteral("actorUserId")));
    QVERIFY(!roles.contains(QByteArrayLiteral("mutationId")));
    QVERIFY(!roles.contains(QByteArrayLiteral("runtimeIncarnationId")));
    QVERIFY(!roles.contains(QByteArrayLiteral("fingerprint")));
    QVERIFY(!access.loading());
    QVERIFY(!access.stale());
    QSignalSpy presentationChanged(
        &access,
        &kodosi::SessionAccess::presentationContextChanged);
    upsert(
        sessions,
        sessionObject(QStringLiteral("local"), QStringLiteral("inc-2")));
    QVERIFY(presentationChanged.count() > 0);
}

void SessionAccessTest::rejectsMalformedDuplicateAndStaleProjectionAtomically()
{
    FakeAccessDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::PeopleModel people;
    kodosi::SessionAccess access(dispatcher, sessions, people);
    authenticate(sessions, people, access);
    upsert(
        sessions,
        sessionObject(QStringLiteral("local"), QStringLiteral("inc-1")));
    QVERIFY(access.inspect(QStringLiteral("local")));
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QJsonArray {grantRow()}));
    QCOMPARE(access.grants()->rowCount(), 1);

    QVERIFY(access.refresh(QStringLiteral("local")));
    auto malformed = grantRow();
    malformed.insert(
        QStringLiteral("grantedAt"),
        QStringLiteral("2026-08-31"));
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QJsonArray {malformed}));
    QCOMPARE(access.grants()->rowCount(), 1);
    QVERIFY(access.stale());
    QVERIFY(!access.error().isEmpty());

    QVERIFY(access.refresh(QStringLiteral("local")));
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QJsonArray {grantRow(), grantRow()}));
    QCOMPARE(access.grants()->rowCount(), 1);
    QVERIFY(access.stale());

    QVERIFY(access.refresh(QStringLiteral("local")));
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QJsonArray {grantRow(
            QStringLiteral("alice-id"),
            QStringLiteral("alice"),
            QStringLiteral("future"))}));
    QCOMPARE(access.grants()->rowCount(), 1);

    QVERIFY(access.refresh(QStringLiteral("local")));
    auto invalidExpiry = grantRow();
    invalidExpiry.insert(
        QStringLiteral("expiresAt"),
        QStringLiteral("2026-09-01 20:25:29"));
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QJsonArray {invalidExpiry}));
    QCOMPARE(access.grants()->rowCount(), 1);

    QVERIFY(access.refresh(QStringLiteral("local")));
    QJsonArray levels;
    const QStringList knownLevels {
        QStringLiteral("view"),
        QStringLiteral("suggest"),
        QStringLiteral("inject"),
        QStringLiteral("approve"),
    };
    for (auto index = 0; index < knownLevels.size(); ++index) {
        auto row = grantRow(
            QStringLiteral("actor-%1").arg(index),
            QStringLiteral("handle-%1").arg(index),
            knownLevels[index]);
        if (index == 0) {
            row.insert(QStringLiteral("displayName"), QString {});
        }
        levels.push_back(row);
    }
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        levels));
    QCOMPARE(access.grants()->rowCount(), 4);

    const auto listCount =
        dispatcher.count(QStringLiteral("session.listAccess"));
    QVERIFY(access.refresh(QStringLiteral("local")));
    QVERIFY(access.refresh(QStringLiteral("local")));
    QCOMPARE(
        dispatcher.count(QStringLiteral("session.listAccess")),
        listCount + 1);
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QJsonArray {grantRow()}));
    QCOMPARE(
        dispatcher.count(QStringLiteral("session.listAccess")),
        listCount + 2);
    QVERIFY(access.loading());
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QJsonArray {grantRow()}));
    QVERIFY(!access.loading());

    QVERIFY(access.refresh(QStringLiteral("local")));
    QJsonArray oversized;
    for (auto index = 0; index < 5'001; ++index) {
        oversized.push_back(grantRow(
            QStringLiteral("bounded-actor-%1").arg(index),
            QStringLiteral("bounded-handle-%1").arg(index)));
    }
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        oversized));
    QCOMPARE(access.grants()->rowCount(), 1);
    QVERIFY(access.stale());

    QVERIFY(access.refresh(QStringLiteral("local")));
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QJsonArray {grantRow()},
        QStringLiteral("other"),
        2));
    QVERIFY(access.loading());

    upsert(
        sessions,
        sessionObject(QStringLiteral("local"), QStringLiteral("inc-2")));
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QJsonArray {}));
    QCOMPARE(access.grants()->rowCount(), 0);
    QVERIFY(!access.loading());
    QVERIFY(!access.error().isEmpty());
}

void SessionAccessTest::sendsExactGrantAndRevokeCommandsAfterNativeFriendResolution()
{
    FakeAccessDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::PeopleModel people;
    kodosi::SessionAccess access(dispatcher, sessions, people);
    authenticate(sessions, people, access);
    installFriends(people);
    upsert(
        sessions,
        sessionObject(QStringLiteral("local"), QStringLiteral("inc-1")));

    QVERIFY(access.grant(
        QStringLiteral("local"),
        QStringLiteral(" @ALICE "),
        QStringLiteral("suggest")));
    const auto grant =
        dispatcher.last(QStringLiteral("session.grantAccess"));
    QCOMPARE(grant.size(), 7);
    QCOMPARE(
        grant.value(QStringLiteral("sessionId")).toString(),
        QStringLiteral("local"));
    QCOMPARE(
        grant.value(QStringLiteral("expectedRuntimeIncarnationId")).toString(),
        QStringLiteral("inc-1"));
    QCOMPARE(
        grant.value(QStringLiteral("actorUserId")).toString(),
        QStringLiteral("alice-id"));
    QCOMPARE(
        grant.value(QStringLiteral("accessLevel")).toString(),
        QStringLiteral("suggest"));
    QCOMPARE(
        QUuid(grant.value(QStringLiteral("mutationId")).toString()).version(),
        QUuid::Version::UnixEpoch);
    const auto expiry = grant.value(QStringLiteral("expiresAt")).toString();
    QVERIFY(QRegularExpression(
        QStringLiteral(
            R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$)"))
                .match(expiry)
                .hasMatch());

    access.resetRuntimeAuthority();
    authenticate(sessions, people, access);
    QVERIFY(access.inspect(QStringLiteral("local")));
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QJsonArray {grantRow()}));
    QVERIFY(access.requestRevokeConfirmation(
        QStringLiteral("local"),
        QStringLiteral("@alice")));
    QVERIFY(access.confirmRevoke(
        QStringLiteral("local"),
        QStringLiteral("alice")));
    const auto revoke =
        dispatcher.last(QStringLiteral("session.revokeAccess"));
    QCOMPARE(revoke.size(), 5);
    QCOMPARE(
        revoke.value(QStringLiteral("actorUserId")).toString(),
        QStringLiteral("alice-id"));
    QCOMPARE(
        revoke.value(QStringLiteral("expectedRuntimeIncarnationId")).toString(),
        QStringLiteral("inc-1"));
}

void SessionAccessTest::rejectsReplacementUnsharedSelfUnknownAndConflictingTargets()
{
    FakeAccessDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::PeopleModel people;
    kodosi::SessionAccess access(dispatcher, sessions, people);
    authenticate(sessions, people, access);
    installFriends(people, QStringLiteral("me"), 1, true);
    upsert(
        sessions,
        sessionObject(
            QStringLiteral("local"),
            QStringLiteral("inc-private"),
            QStringLiteral("local"),
            QStringLiteral("justMe")));

    QVERIFY(!access.grant(
        QStringLiteral("local"),
        QStringLiteral("alice"),
        QStringLiteral("view")));
    upsert(
        sessions,
        sessionObject(QStringLiteral("local"), QStringLiteral("inc-1")));
    QVERIFY(!access.grant(
        QStringLiteral("local"),
        QStringLiteral("missing"),
        QStringLiteral("view")));
    QVERIFY(!access.grant(
        QStringLiteral("local"),
        QStringLiteral("self"),
        QStringLiteral("view")));
    QVERIFY(!access.grant(
        QStringLiteral("local"),
        QStringLiteral("alice"),
        QStringLiteral("future")));

    QVERIFY(access.grant(
        QStringLiteral("local"),
        QStringLiteral("alice"),
        QStringLiteral("view")));
    const auto mutationId =
        dispatcher.last(QStringLiteral("session.grantAccess"))
            .value(QStringLiteral("mutationId"))
            .toString();
    QVERIFY(!access.grant(
        QStringLiteral("local"),
        QStringLiteral("alice"),
        QStringLiteral("approve")));
    QCOMPARE(
        dispatcher.count(QStringLiteral("session.grantAccess")),
        1);

    upsert(
        sessions,
        sessionObject(QStringLiteral("local"), QStringLiteral("inc-2")));
    access.ingestSessionEvent(mutationEvent(
        QStringLiteral("session.accessMutationAccepted"),
        mutationId,
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QStringLiteral("grant"),
        QStringLiteral("alice-id"),
        QStringLiteral("view"),
        dispatcher.last(QStringLiteral("session.grantAccess"))
            .value(QStringLiteral("expiresAt"))
            .toString()));
    QCOMPARE(
        access.actorMutationPhase(
            QStringLiteral("local"),
            QStringLiteral("alice")),
        QStringLiteral("exhausted"));

    upsert(
        sessions,
        sessionObject(
            QStringLiteral("remote"),
            QStringLiteral("remote-inc"),
            QStringLiteral("remote")));
    QVERIFY(!access.grant(
        QStringLiteral("remote"),
        QStringLiteral("bob"),
        QStringLiteral("view")));
}

void SessionAccessTest::terminalGrantSurvivesReplacementBeforeRecovery()
{
    FakeAccessDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::PeopleModel people;
    kodosi::SessionAccess access(dispatcher, sessions, people);
    authenticate(sessions, people, access);
    installFriends(people);
    upsert(
        sessions,
        sessionObject(QStringLiteral("local"), QStringLiteral("inc-1")));
    QVERIFY(access.grant(
        QStringLiteral("local"),
        QStringLiteral("alice"),
        QStringLiteral("view")));
    const auto command =
        dispatcher.last(QStringLiteral("session.grantAccess"));
    const auto mutationId =
        command.value(QStringLiteral("mutationId")).toString();
    const auto expiry =
        command.value(QStringLiteral("expiresAt")).toString();
    access.ingestSessionEvent(mutationEvent(
        QStringLiteral("session.accessMutationResult"),
        mutationId,
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QStringLiteral("grant"),
        QStringLiteral("alice-id"),
        QStringLiteral("view"),
        expiry,
        QStringLiteral("applied")));

    upsert(
        sessions,
        sessionObject(QStringLiteral("local"), QStringLiteral("inc-2")));
    QCOMPARE(
        dispatcher.last(QStringLiteral("session.accessMutationReconcile"))
            .value(QStringLiteral("mutationId"))
            .toString(),
        mutationId);
    access.ingestSessionEvent(fenced({
        {QStringLiteral("type"),
         QStringLiteral("session.accessMutationReconciled")},
        {QStringLiteral("mutationId"), mutationId},
        {QStringLiteral("present"), true},
    }));
    access.ingestSessionEvent(mutationEvent(
        QStringLiteral("session.accessMutationRecovered"),
        mutationId,
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QStringLiteral("grant"),
        QStringLiteral("alice-id"),
        QStringLiteral("view"),
        minimallyFractionalRfc3339(expiry),
        QStringLiteral("applied"),
        QString(64, u'e')));
    QCOMPARE(
        dispatcher.last(QStringLiteral("session.accessMutationAck"))
            .value(QStringLiteral("mutationId"))
            .toString(),
        mutationId);
}

void SessionAccessTest::reconcilesGrantAndRevokeThroughProjectionAndExactAcknowledgment()
{
    FakeAccessDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::PeopleModel people;
    kodosi::SessionAccess access(dispatcher, sessions, people);
    authenticate(sessions, people, access);
    installFriends(people);
    upsert(
        sessions,
        sessionObject(QStringLiteral("local"), QStringLiteral("inc-1")));

    QVERIFY(access.grant(
        QStringLiteral("local"),
        QStringLiteral("alice"),
        QStringLiteral("inject")));
    const auto grant =
        dispatcher.last(QStringLiteral("session.grantAccess"));
    const auto mutationId =
        grant.value(QStringLiteral("mutationId")).toString();
    const auto expiry =
        grant.value(QStringLiteral("expiresAt")).toString();
    access.ingestSessionEvent(mutationEvent(
        QStringLiteral("session.accessMutationResult"),
        mutationId,
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QStringLiteral("grant"),
        QStringLiteral("alice-id"),
        QStringLiteral("inject"),
        expiry,
        QStringLiteral("applied")));
    QCOMPARE(
        dispatcher.last(QStringLiteral("session.listAccess"))
            .value(QStringLiteral("sessionId"))
            .toString(),
        QStringLiteral("local"));
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QJsonArray {grantRow(
            QStringLiteral("alice-id"),
            QStringLiteral("alice"),
            QStringLiteral("inject"),
            expiry)}));
    QCOMPARE(
        dispatcher.last(QStringLiteral("session.accessMutationReconcile"))
            .value(QStringLiteral("mutationId"))
            .toString(),
        mutationId);
    access.ingestSessionEvent(fenced({
        {QStringLiteral("type"),
         QStringLiteral("session.accessMutationReconciled")},
        {QStringLiteral("mutationId"), mutationId},
        {QStringLiteral("present"), true},
    }));
    const auto reconcileCount =
        dispatcher.count(QStringLiteral("session.accessMutationReconcile"));
    const auto fingerprint = QString(64, u'a');
    access.ingestSessionEvent(mutationEvent(
        QStringLiteral("session.accessMutationRecovered"),
        mutationId,
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QStringLiteral("grant"),
        QStringLiteral("alice-id"),
        QStringLiteral("inject"),
        minimallyFractionalRfc3339(expiry),
        QStringLiteral("applied"),
        fingerprint));
    QCOMPARE(
        dispatcher.count(QStringLiteral("session.accessMutationReconcile")),
        reconcileCount);
    QCOMPARE(
        dispatcher.last(QStringLiteral("session.accessMutationAck"))
            .value(QStringLiteral("fingerprint"))
            .toString(),
        fingerprint);
    access.ingestSessionEvent(fenced({
        {QStringLiteral("type"),
         QStringLiteral("session.accessMutationAcknowledged")},
        {QStringLiteral("mutationId"), mutationId},
        {QStringLiteral("fingerprint"), QString(64, u'b')},
    }));
    QVERIFY(access.mutationPhase(QStringLiteral("local"))
        != QStringLiteral("idle"));
    access.ingestSessionEvent(fenced({
        {QStringLiteral("type"),
         QStringLiteral("session.accessMutationAcknowledged")},
        {QStringLiteral("mutationId"), mutationId},
        {QStringLiteral("fingerprint"), fingerprint},
    }));
    QCOMPARE(
        access.mutationPhase(QStringLiteral("local")),
        QStringLiteral("idle"));

    QVERIFY(access.requestRevokeConfirmation(
        QStringLiteral("local"),
        QStringLiteral("alice")));
    QVERIFY(access.confirmRevoke(
        QStringLiteral("local"),
        QStringLiteral("alice")));
    const auto revoke =
        dispatcher.last(QStringLiteral("session.revokeAccess"));
    const auto revokeId =
        revoke.value(QStringLiteral("mutationId")).toString();
    access.ingestSessionEvent(mutationEvent(
        QStringLiteral("session.accessMutationResult"),
        revokeId,
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QStringLiteral("revoke"),
        QStringLiteral("alice-id"),
        {},
        {},
        QStringLiteral("applied")));
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QJsonArray {}));
    access.ingestSessionEvent(fenced({
        {QStringLiteral("type"),
         QStringLiteral("session.accessMutationReconciled")},
        {QStringLiteral("mutationId"), revokeId},
        {QStringLiteral("present"), true},
    }));
    access.ingestSessionEvent(mutationEvent(
        QStringLiteral("session.accessMutationRecovered"),
        revokeId,
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QStringLiteral("revoke"),
        QStringLiteral("alice-id"),
        {},
        {},
        QStringLiteral("applied"),
        QString(64, u'c')));
    QCOMPARE(
        dispatcher.last(QStringLiteral("session.accessMutationAck"))
            .value(QStringLiteral("mutationId"))
            .toString(),
        revokeId);
}

void SessionAccessTest::preservesDurableLeaveAcrossReceiptOrderingAndAckFailures()
{
    FakeAccessDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::PeopleModel people;
    kodosi::SessionAccess access(dispatcher, sessions, people);
    kodosi::SessionActions actions(dispatcher, sessions, access);
    authenticate(sessions, people, access);
    upsert(
        sessions,
        sessionObject(
            QStringLiteral("remote"),
            QStringLiteral("remote-inc"),
            QStringLiteral("remote"),
            QStringLiteral("friends")));

    QVERIFY(access.canLeave(QStringLiteral("remote")));
    QVERIFY(actions.canHide(QStringLiteral("remote")));
    QVERIFY(access.requestLeaveConfirmation(QStringLiteral("remote")));
    QVERIFY(access.confirmLeave(QStringLiteral("remote")));
    QVERIFY(!actions.canHide(QStringLiteral("remote")));
    QVERIFY(!actions.canOpenRemote(QStringLiteral("remote")));
    const auto rejectedId =
        dispatcher.last(QStringLiteral("session.leave"))
            .value(QStringLiteral("mutationId"))
            .toString();
    access.ingestSessionEvent(mutationEvent(
        QStringLiteral("session.accessMutationAccepted"),
        rejectedId,
        QStringLiteral("remote"),
        QStringLiteral("remote-inc"),
        QStringLiteral("leave")));
    QCOMPARE(
        access.mutationPhase(QStringLiteral("remote")),
        QStringLiteral("accepted"));
    access.ingestSessionEvent(mutationEvent(
        QStringLiteral("session.accessMutationResult"),
        rejectedId,
        QStringLiteral("remote"),
        QStringLiteral("remote-inc"),
        QStringLiteral("leave"),
        {},
        {},
        {},
        QStringLiteral("rejected"),
        {},
        QStringLiteral("leave denied")));
    QVERIFY(access.pendingLeaveSessionIds().contains(
        QStringLiteral("remote")));
    QCOMPARE(
        dispatcher.last(QStringLiteral("session.accessMutationReconcile"))
            .value(QStringLiteral("mutationId"))
            .toString(),
        rejectedId);
    access.ingestSessionEvent(fenced({
        {QStringLiteral("type"),
         QStringLiteral("session.accessMutationReconciled")},
        {QStringLiteral("mutationId"), rejectedId},
        {QStringLiteral("present"), false},
    }));
    QVERIFY(access.pendingLeaveSessionIds().isEmpty());
    QCOMPARE(access.error(), QStringLiteral("leave denied"));

    upsert(
        sessions,
        sessionObject(
            QStringLiteral("remote"),
            QStringLiteral("remote-inc"),
            QStringLiteral("remote"),
            QStringLiteral("friends")));
    QVERIFY(access.requestLeaveConfirmation(QStringLiteral("remote")));
    QVERIFY(access.confirmLeave(QStringLiteral("remote")));
    const auto appliedId =
        dispatcher.last(QStringLiteral("session.leave"))
            .value(QStringLiteral("mutationId"))
            .toString();
    access.ingestSessionEvent(mutationEvent(
        QStringLiteral("session.accessMutationResult"),
        appliedId,
        QStringLiteral("remote"),
        QStringLiteral("remote-inc"),
        QStringLiteral("leave"),
        {},
        {},
        {},
        QStringLiteral("applied")));
    snapshot(sessions, QJsonArray {});
    const auto fingerprint = QString(64, u'd');
    access.ingestSessionEvent(mutationEvent(
        QStringLiteral("session.accessMutationRecovered"),
        appliedId,
        QStringLiteral("remote"),
        QStringLiteral("remote-inc"),
        QStringLiteral("leave"),
        {},
        {},
        {},
        QStringLiteral("applied"),
        fingerprint));
    QCOMPARE(
        dispatcher.last(QStringLiteral("session.accessMutationAck"))
            .value(QStringLiteral("mutationId"))
            .toString(),
        appliedId);
    access.ingestSessionEvent(fenced({
        {QStringLiteral("type"), QStringLiteral("session.error")},
        {QStringLiteral("operation"),
         QStringLiteral("session.accessMutationAck")},
        {QStringLiteral("requestId"), appliedId},
        {QStringLiteral("message"), QStringLiteral("ack busy")},
    }));
    QVERIFY(access.pendingLeaveSessionIds().contains(
        QStringLiteral("remote")));
    QCOMPARE(
        dispatcher.last(QStringLiteral("session.accessMutationReconcile"))
            .value(QStringLiteral("mutationId"))
            .toString(),
        appliedId);
    access.ingestSessionEvent(fenced({
        {QStringLiteral("type"),
         QStringLiteral("session.accessMutationReconciled")},
        {QStringLiteral("mutationId"), appliedId},
        {QStringLiteral("present"), false},
    }));
    QVERIFY(access.pendingLeaveSessionIds().isEmpty());
}

void SessionAccessTest::verifiesDroppedAcknowledgmentBeforeSettling()
{
    FakeAccessDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::PeopleModel people;
    kodosi::SessionAccess access(
        dispatcher,
        sessions,
        people,
        {
            .resultTimeoutMs = 20,
            .projectionTimeoutMs = 20,
            .queryTimeoutMs = 20,
            .acknowledgmentTimeoutMs = 5,
            .maximumAttempts = 2,
        });
    authenticate(sessions, people, access);
    installFriends(people);
    upsert(
        sessions,
        sessionObject(QStringLiteral("local"), QStringLiteral("inc-1")));
    QVERIFY(access.grant(
        QStringLiteral("local"),
        QStringLiteral("alice"),
        QStringLiteral("approve")));
    const auto grant =
        dispatcher.last(QStringLiteral("session.grantAccess"));
    const auto mutationId =
        grant.value(QStringLiteral("mutationId")).toString();
    const auto expiry =
        grant.value(QStringLiteral("expiresAt")).toString();
    const auto fingerprint = QString(64, u'f');
    access.ingestSessionEvent(mutationEvent(
        QStringLiteral("session.accessMutationRecovered"),
        mutationId,
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QStringLiteral("grant"),
        QStringLiteral("alice-id"),
        QStringLiteral("approve"),
        expiry,
        QStringLiteral("applied"),
        fingerprint));
    access.ingestSessionEvent(grantsEvent(
        QStringLiteral("local"),
        QStringLiteral("inc-1"),
        QJsonArray {grantRow(
            QStringLiteral("alice-id"),
            QStringLiteral("alice"),
            QStringLiteral("approve"),
            expiry)}));
    QCOMPARE(
        dispatcher.last(QStringLiteral("session.accessMutationAck"))
            .value(QStringLiteral("mutationId"))
            .toString(),
        mutationId);
    const auto queriesBefore =
        dispatcher.count(QStringLiteral("session.accessMutationReconcile"));
    QTRY_VERIFY_WITH_TIMEOUT(
        dispatcher.count(QStringLiteral("session.accessMutationReconcile"))
            > queriesBefore,
        100);
    QVERIFY(access.mutationPhase(QStringLiteral("local"))
        != QStringLiteral("idle"));
    access.ingestSessionEvent(fenced({
        {QStringLiteral("type"),
         QStringLiteral("session.accessMutationReconciled")},
        {QStringLiteral("mutationId"), mutationId},
        {QStringLiteral("present"), false},
    }));
    QCOMPARE(
        access.mutationPhase(QStringLiteral("local")),
        QStringLiteral("idle"));
}

void SessionAccessTest::suppressesMarkerPayloadLoopsAndRetriesSameMutationId()
{
    FakeAccessDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::PeopleModel people;
    kodosi::SessionAccess access(
        dispatcher,
        sessions,
        people,
        {
            .resultTimeoutMs = 5,
            .projectionTimeoutMs = 5,
            .queryTimeoutMs = 5,
            .acknowledgmentTimeoutMs = 5,
            .maximumAttempts = 1,
        });
    authenticate(sessions, people, access);

    const auto recoveredId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    const auto unresolved = mutationEvent(
        QStringLiteral("session.accessMutationRecovered"),
        recoveredId,
        QStringLiteral("remote"),
        QStringLiteral("remote-inc"),
        QStringLiteral("leave"),
        {},
        {},
        {},
        {},
        QString(64, u'e'));
    access.ingestSessionEvent(unresolved);
    QCOMPARE(
        dispatcher.last(QStringLiteral("session.accessMutationReconcile"))
            .value(QStringLiteral("mutationId"))
            .toString(),
        recoveredId);
    access.ingestSessionEvent(fenced({
        {QStringLiteral("type"),
         QStringLiteral("session.accessMutationReconciled")},
        {QStringLiteral("mutationId"), recoveredId},
        {QStringLiteral("present"), true},
    }));
    const auto beforePayload =
        dispatcher.count(QStringLiteral("session.accessMutationReconcile"));
    access.ingestSessionEvent(unresolved);
    QCOMPARE(
        dispatcher.count(QStringLiteral("session.accessMutationReconcile")),
        beforePayload);

    access.resetRuntimeAuthority();
    authenticate(sessions, people, access);
    installFriends(people);
    upsert(
        sessions,
        sessionObject(QStringLiteral("local"), QStringLiteral("inc-1")));
    const auto commandStart = dispatcher.commands.size();
    QVERIFY(access.grant(
        QStringLiteral("local"),
        QStringLiteral("alice"),
        QStringLiteral("view")));
    const auto mutationId =
        dispatcher.last(QStringLiteral("session.grantAccess"))
            .value(QStringLiteral("mutationId"))
            .toString();
    QTRY_COMPARE_WITH_TIMEOUT(
        access.actorMutationPhase(
            QStringLiteral("local"),
            QStringLiteral("alice")),
        QStringLiteral("exhausted"),
        100);
    QCOMPARE(
        dispatcher.count(QStringLiteral("session.grantAccess")),
        1);
    for (auto index = commandStart;
         index < dispatcher.commands.size();
         ++index) {
        const auto& command = dispatcher.commands[index];
        if (command.value(QStringLiteral("type")).toString()
            == QStringLiteral("session.accessMutationReconcile")) {
            QCOMPARE(
                command.value(QStringLiteral("mutationId")).toString(),
                mutationId);
        }
    }
    QVERIFY(access.retryMutation(
        QStringLiteral("local"),
        QStringLiteral("alice")));
    QCOMPARE(
        dispatcher.last(QStringLiteral("session.accessMutationReconcile"))
            .value(QStringLiteral("mutationId"))
            .toString(),
        mutationId);
}

QTEST_GUILESS_MAIN(SessionAccessTest)

#include "tst_session_access.moc"
