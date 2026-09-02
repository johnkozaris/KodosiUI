#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <map>
#include <optional>
#include <set>

namespace kodosi {

class SteeringModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(quint64 stateRevision READ stateRevision NOTIFY stateChanged)
    Q_PROPERTY(QString draftText READ draftText NOTIFY stateChanged)
    Q_PROPERTY(QString draftMode READ draftMode NOTIFY stateChanged)
    Q_PROPERTY(QStringList availableModes READ availableModes NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool canSend READ canSend NOTIFY stateChanged)
    Q_PROPERTY(bool canCancel READ canCancel NOTIFY stateChanged)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY stateChanged)
    Q_PROPERTY(int retainedCount READ retainedCount NOTIFY stateChanged)

public:
    struct Timing {
        qint64 replyTimeoutMs = 30'000;
    };

    SteeringModel(
        CommandDispatcher& dispatcher,
        SessionCatalogModel& sessions,
        QObject* parent = nullptr);
    SteeringModel(
        CommandDispatcher& dispatcher,
        SessionCatalogModel& sessions,
        Timing timing,
        QObject* parent = nullptr);

    [[nodiscard]] quint64 stateRevision() const noexcept;
    [[nodiscard]] QString draftText() const;
    [[nodiscard]] QString draftMode() const;
    [[nodiscard]] QStringList availableModes() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString error() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool canSend() const;
    [[nodiscard]] bool canCancel() const;
    [[nodiscard]] bool canRetry() const;
    [[nodiscard]] int retainedCount() const;

    Q_INVOKABLE [[nodiscard]] bool inspect(const QString& sessionId);
    Q_INVOKABLE void clearInspection();
    Q_INVOKABLE [[nodiscard]] bool rehydrate(const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool saveDraft(
        const QString& sessionId,
        const QString& text,
        const QString& mode);
    Q_INVOKABLE [[nodiscard]] bool send(const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool cancel(const QString& sessionId);
    Q_INVOKABLE [[nodiscard]] bool retry(const QString& sessionId);
    Q_INVOKABLE void clearError(const QString& sessionId);

    [[nodiscard]] int retainedRequestCount(const QString& sessionId) const;
    [[nodiscard]] int blockingRequestCount(const QString& sessionId) const;

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestAgentIntelEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void stateChanged();
    void decodeError(QString message);

public:
    enum class Mode {
        Queue,
        Steer,
        StopAndSend,
    };

    enum class State {
        Sending,
        Reconciling,
        Preparing,
        Queued,
        Injected,
        Cancelled,
        DeliveryUnknown,
        Failed,
    };

    enum class OperationKind {
        Send,
        Cancel,
        QueryExact,
        QueryFull,
    };

private:
    struct RequestKey {
        QString accountUserId;
        QString requestId;
        QString sessionId;
        QString incarnationId;

        bool operator==(const RequestKey&) const = default;
        [[nodiscard]] bool operator<(const RequestKey& other) const;
    };

    struct HydrationKey {
        QString accountUserId;
        QString sessionId;
        QString incarnationId;
        bool canQueue = false;
        bool canSteer = false;
        bool canStopAndSend = false;

        bool operator==(const HydrationKey&) const = default;
        [[nodiscard]] bool operator<(const HydrationKey& other) const;
    };

    struct Draft {
        QString text;
        Mode mode = Mode::Queue;
        quint64 revision = 0;
    };

    struct RuntimeEntry {
        QString steerId;
        QString text;
        QString atToolUseId;
        quint64 queuedAtMs = 0;
        State deliveryState = State::Preparing;
    };

    struct Request {
        RequestKey key;
        Mode mode = Mode::Queue;
        QString payload;
        QString submittedDraft;
        quint64 draftRevision = 0;
        quint64 version = 0;
        quint64 order = 0;
        State state = State::Sending;
        QString message;
        std::optional<RuntimeEntry> runtime;
        bool locallyOwned = false;
    };

    struct DecodedEntry {
        RequestKey key;
        Mode mode = Mode::Queue;
        QString text;
        QString steerId;
        QString atToolUseId;
        quint64 queuedAtMs = 0;
        State deliveryState = State::Preparing;
    };

    struct Operation {
        QString requestId;
        OperationKind kind = OperationKind::QueryFull;
        QString sessionId;
        QString incarnationId;
        std::optional<RequestKey> target;
        std::map<RequestKey, quint64> snapshot;
        std::optional<HydrationKey> hydration;
        qint64 deadlineMs = 0;
    };

    static constexpr qsizetype maximumBlockingRequests = 128;
    static constexpr qsizetype maximumTerminalHistory = 32;
    static constexpr qsizetype maximumRuntimePendingPerSession = 32;
    static constexpr qsizetype maximumQueryEntries = 544;
    static constexpr qsizetype maximumIdentityScalars = 1'024;
    static constexpr qsizetype maximumStatusScalars = 1'024;
    static constexpr qsizetype maximumSteerTextBytes = 16 * 1'024;
    static constexpr qsizetype maximumDraftBytes = 64 * 1'024;

    CommandDispatcher& m_dispatcher;
    SessionCatalogModel& m_sessions;
    Timing m_timing;
    AccountContextFence m_accountFence {256};
    std::map<RequestKey, Request> m_requests;
    QHash<QString, Draft> m_drafts;
    QHash<QString, QString> m_errors;
    QHash<QString, Operation> m_operations;
    std::set<HydrationKey> m_hydratedAuthorities;
    std::set<HydrationKey> m_fullQueryRetryAuthorities;
    QTimer m_timer;
    QString m_selectedSessionId;
    QString m_selectedIncarnationId;
    QString m_accountUserId;
    quint64 m_stateRevision = 0;
    quint64 m_nextVersion = 0;
    quint64 m_nextOrder = 0;
    bool m_authenticated = false;
    bool m_hasAccountActivation = false;

    [[nodiscard]] std::optional<SessionCatalogModel::ActionContext>
    currentContext(const QString& sessionId, QString& message) const;
    [[nodiscard]] std::optional<HydrationKey> currentHydrationKey(
        const QString& sessionId,
        QString& message) const;
    [[nodiscard]] QString semanticAccountUserId() const;
    [[nodiscard]] const Request* selectedRequest(const QString& sessionId) const;
    [[nodiscard]] Request* mutableRequest(const RequestKey& key);
    [[nodiscard]] bool dispatch(Operation operation, QJsonObject command);
    [[nodiscard]] bool dispatchFullQuery(const HydrationKey& hydration);
    [[nodiscard]] bool dispatchExactQuery(
        const RequestKey& key,
        QString failureMessage);
    [[nodiscard]] bool applyEntry(
        const DecodedEntry& entry,
        std::optional<State> transition,
        QString message,
        bool prunePresentation = true);
    [[nodiscard]] bool applyQuery(
        const Operation& operation,
        const QVector<DecodedEntry>& entries);
    [[nodiscard]] bool decodeReplyEntries(
        const QByteArray& json,
        const QJsonValue& payload,
        QVector<DecodedEntry>& entries,
        QString& message) const;
    [[nodiscard]] std::optional<DecodedEntry> decodeEntry(
        const QJsonObject& object,
        quint64 queuedAtMs) const;
    [[nodiscard]] bool ensureBlockingCapacity(qsizetype additional);
    [[nodiscard]] bool blocks(const Request& request) const;
    [[nodiscard]] bool terminal(const Request& request) const;
    [[nodiscard]] bool cancellable(const Request& request) const;
    void applyReply(const QByteArray& json, const QJsonObject& object);
    void applyRuntimeEvent(const QByteArray& json, const QJsonObject& object);
    void applyError(const QJsonObject& object);
    void handleOperationFailure(Operation operation, QString message);
    void markFailed(Request& request, QString message);
    void markReconciling(Request& request, QString message);
    void clearMatchingDraft(const Request& request);
    void pruneTerminalHistory();
    void reconcileCatalog();
    void activateAccount(QString userId, quint64 epoch, bool authenticated);
    void scheduleTimer();
    void expireOperations();
    void bumpState();

    [[nodiscard]] static QString modeName(Mode mode);
    [[nodiscard]] static std::optional<Mode> decodeMode(const QString& mode);
    [[nodiscard]] static bool supportsMode(
        Mode mode,
        const SessionCatalogModel::ActionContext& context);
    [[nodiscard]] static std::optional<Mode> effectiveMode(
        Mode preferred,
        const SessionCatalogModel::ActionContext& context);
    [[nodiscard]] static QString stateMessage(const Request& request);
    [[nodiscard]] static bool isUuidV7(const QString& value);
};

} // namespace kodosi
