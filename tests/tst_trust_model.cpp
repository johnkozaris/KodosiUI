#include "models/TrustModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QTest>

#include <utility>

class FakeCommandDispatcher final : public kodosi::CommandDispatcher {
public:
    QVector<QJsonObject> commands;
    bool rejectNext = false;

    Result send(const kodosi::CommandLane lane, const QByteArrayView json) override
    {
        if (lane != kodosi::CommandLane::Trust) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::InvalidArgument,
                .ffiResult = -1,
                .message = QStringLiteral("Wrong command lane"),
            });
        }
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        if (!document.isObject()) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::InvalidArgument,
                .ffiResult = -1,
                .message = QStringLiteral("Invalid command JSON"),
            });
        }
        commands.push_back(document.object());
        if (std::exchange(rejectNext, false)) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("Rejected"),
            });
        }
        return {};
    }
};

class TrustModelTest final : public QObject {
    Q_OBJECT

private slots:
    void correlatesAuthoritativeSnapshots();
    void ignoresStaleRefreshResults();
    void confirmsAndCorrelatesReset();
    void fencesResetAcrossAccounts();
    void preservesPinsOnMalformedSnapshot();
    void surfacesImmediateDispatchFailure();
    void subjectlessAccountCannotResetPins();
};

namespace {

QString lastRequestId(const FakeCommandDispatcher& dispatcher)
{
    return dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
}

QByteArray snapshot(
    const QString& requestId,
    const QString& userId,
    const quint64 accountEpoch,
    const QString& pinUserId)
{
    const QJsonObject pin {
        {QStringLiteral("userId"), pinUserId},
        {QStringLiteral("generation"), 3},
        {QStringLiteral("signerDeviceId"), QStringLiteral("device-a")},
        {QStringLiteral("deviceCount"), 2},
        {QStringLiteral("pinnedAtMs"), 1'700'000'000'000.0},
    };
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), userId},
        {QStringLiteral("accountEpoch"), static_cast<qint64>(accountEpoch)},
        {QStringLiteral("type"), QStringLiteral("trust.snapshot")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("pins"), QJsonArray {pin}},
    }).toJson(QJsonDocument::Compact);
}

void authenticate(kodosi::TrustModel& model, const QString& userId, const quint64 epoch)
{
    const auto event = QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("auth.ready")},
        {QStringLiteral("userId"), userId},
        {QStringLiteral("accountEpoch"), static_cast<qint64>(epoch)},
    }).toJson(QJsonDocument::Compact);
    model.ingestAuthEvent(event);
}

} // namespace

void TrustModelTest::correlatesAuthoritativeSnapshots()
{
    FakeCommandDispatcher dispatcher;
    kodosi::TrustModel model(dispatcher);
    authenticate(model, QStringLiteral("account"), 1);

    QCOMPARE(dispatcher.commands.size(), 1);
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("trust.refresh"));
    QVERIFY(model.loading());
    model.ingestTrustEvent(snapshot(
        lastRequestId(dispatcher),
        QStringLiteral("account"),
        1,
        QStringLiteral("friend")));

    QCOMPARE(model.rowCount(), 1);
    QVERIFY(!model.loading());
    QCOMPARE(
        model.data(model.index(0), kodosi::TrustModel::UserIdRole).toString(),
        QStringLiteral("friend"));
    QCOMPARE(
        model.data(model.index(0), kodosi::TrustModel::GenerationRole).toULongLong(),
        3ULL);
    QCOMPARE(
        model.resetAvailability(QStringLiteral("friend")),
        kodosi::TrustModel::ResetAvailability::Available);
}

void TrustModelTest::ignoresStaleRefreshResults()
{
    FakeCommandDispatcher dispatcher;
    kodosi::TrustModel model(dispatcher);
    authenticate(model, QStringLiteral("account"), 1);
    model.ingestTrustEvent(snapshot(
        lastRequestId(dispatcher),
        QStringLiteral("account"),
        1,
        QStringLiteral("current")));

    QVERIFY(model.refresh());
    const auto staleRequestId = lastRequestId(dispatcher);
    QVERIFY(model.refresh());
    const auto currentRequestId = lastRequestId(dispatcher);
    model.ingestTrustEvent(snapshot(
        staleRequestId,
        QStringLiteral("account"),
        1,
        QStringLiteral("stale")));
    QCOMPARE(
        model.data(model.index(0), kodosi::TrustModel::UserIdRole).toString(),
        QStringLiteral("current"));
    QVERIFY(model.loading());

    model.ingestTrustEvent(snapshot(
        currentRequestId,
        QStringLiteral("account"),
        1,
        QStringLiteral("new")));
    QCOMPARE(
        model.data(model.index(0), kodosi::TrustModel::UserIdRole).toString(),
        QStringLiteral("new"));
    QVERIFY(!model.loading());
}

void TrustModelTest::confirmsAndCorrelatesReset()
{
    FakeCommandDispatcher dispatcher;
    kodosi::TrustModel model(dispatcher);
    authenticate(model, QStringLiteral("account"), 1);
    model.ingestTrustEvent(snapshot(
        lastRequestId(dispatcher),
        QStringLiteral("account"),
        1,
        QStringLiteral("friend")));

    QVERIFY(model.requestResetConfirmation(QStringLiteral("friend")));
    QCOMPARE(
        model.resetAvailability(QStringLiteral("friend")),
        kodosi::TrustModel::ResetAvailability::Confirming);
    QVERIFY(model.confirmReset());
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("trust.reset"));
    const auto requestId = lastRequestId(dispatcher);
    QCOMPARE(model.pendingResetUserId(), QStringLiteral("friend"));

    model.ingestTrustEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("trust.reset")},
        {QStringLiteral("requestId"), QStringLiteral("unrelated")},
        {QStringLiteral("userId"), QStringLiteral("friend")},
        {QStringLiteral("cleared"), true},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.pendingResetUserId(), QStringLiteral("friend"));

    model.ingestTrustEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("trust.reset")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("userId"), QStringLiteral("friend")},
        {QStringLiteral("cleared"), true},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.pendingResetUserId().isEmpty());
}

void TrustModelTest::fencesResetAcrossAccounts()
{
    FakeCommandDispatcher dispatcher;
    kodosi::TrustModel model(dispatcher);
    authenticate(model, QStringLiteral("account-a"), 1);
    model.ingestTrustEvent(snapshot(
        lastRequestId(dispatcher),
        QStringLiteral("account-a"),
        1,
        QStringLiteral("friend")));
    QVERIFY(model.requestResetConfirmation(QStringLiteral("friend")));

    authenticate(model, QStringLiteral("account-b"), 2);
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.confirmationUserId().isEmpty());
    QVERIFY(!model.confirmReset());
    QCOMPARE(dispatcher.commands.size(), 2);
}

void TrustModelTest::preservesPinsOnMalformedSnapshot()
{
    FakeCommandDispatcher dispatcher;
    kodosi::TrustModel model(dispatcher);
    QSignalSpy errors(&model, &kodosi::TrustModel::decodeError);
    authenticate(model, QStringLiteral("account"), 1);
    model.ingestTrustEvent(snapshot(
        lastRequestId(dispatcher),
        QStringLiteral("account"),
        1,
        QStringLiteral("safe")));
    QVERIFY(model.refresh());

    model.ingestTrustEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("trust.snapshot")},
        {QStringLiteral("requestId"), lastRequestId(dispatcher)},
        {QStringLiteral("pins"), QJsonArray {
             QJsonObject {
                 {QStringLiteral("userId"), QStringLiteral("bad")},
                 {QStringLiteral("generation"), 1.5},
                 {QStringLiteral("signerDeviceId"), QStringLiteral("device")},
                 {QStringLiteral("deviceCount"), 1},
                 {QStringLiteral("pinnedAtMs"), 1},
             },
         }},
    }).toJson(QJsonDocument::Compact));

    QCOMPARE(errors.count(), 1);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(
        model.data(model.index(0), kodosi::TrustModel::UserIdRole).toString(),
        QStringLiteral("safe"));
    QVERIFY(model.loading());
}

void TrustModelTest::surfacesImmediateDispatchFailure()
{
    FakeCommandDispatcher dispatcher;
    dispatcher.rejectNext = true;
    kodosi::TrustModel model(dispatcher);
    authenticate(model, QStringLiteral("account"), 1);

    QVERIFY(!model.loading());
    QCOMPARE(model.retryAction(), kodosi::TrustModel::RetryAction::Refresh);
    QVERIFY(!model.lastError().isEmpty());
}

void TrustModelTest::subjectlessAccountCannotResetPins()
{
    FakeCommandDispatcher dispatcher;
    kodosi::TrustModel model(dispatcher);
    authenticate(model, QString {}, 1);
    model.ingestTrustEvent(snapshot(
        lastRequestId(dispatcher),
        QString {},
        1,
        QStringLiteral("friend")));

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(
        model.resetAvailability(QStringLiteral("friend")),
        kodosi::TrustModel::ResetAvailability::Unavailable);
    QVERIFY(!model.requestResetConfirmation(QStringLiteral("friend")));
    QVERIFY(!model.confirmReset());
    QCOMPARE(dispatcher.commands.size(), 1);
}

QTEST_APPLESS_MAIN(TrustModelTest)

#include "tst_trust_model.moc"
