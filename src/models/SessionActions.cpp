#include "models/SessionActions.hpp"
#include "models/DesktopSettings.hpp"
#include "models/ProviderConversationResumeResolver.hpp"
#include "models/SessionAccess.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <ranges>
#include <utility>

namespace kodosi {

HiddenSessionsModel::HiddenSessionsModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int HiddenSessionsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant HiddenSessionsModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const auto& entry = m_entries[index.row()];
    switch (role) {
    case SessionIdRole:
        return entry.id;
    case NameRole:
        return entry.name;
    case ProjectRole:
        return entry.project;
    case OwnerRole:
        return entry.owner;
    default:
        return {};
    }
}

QHash<int, QByteArray> HiddenSessionsModel::roleNames() const
{
    return {
        {SessionIdRole, QByteArrayLiteral("sessionId")},
        {NameRole, QByteArrayLiteral("name")},
        {ProjectRole, QByteArrayLiteral("project")},
        {OwnerRole, QByteArrayLiteral("owner")},
    };
}

bool HiddenSessionsModel::contains(const QString& sessionId) const
{
    return std::ranges::find(m_entries, sessionId, &Entry::id)
        != m_entries.end();
}

void HiddenSessionsModel::replace(QVector<Entry> entries)
{
    std::ranges::sort(entries, {}, &Entry::name);
    beginResetModel();
    const auto changed = m_entries.size() != entries.size();
    m_entries = std::move(entries);
    endResetModel();
    if (changed) {
        emit countChanged();
    }
}

SessionActions::SessionActions(
    CommandDispatcher& dispatcher,
    SessionCatalogModel& sessions,
    QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_sessions(sessions)
    , m_hiddenSessions(this)
{
    m_hiddenRetryTimer.setSingleShot(true);
    connect(&m_hiddenRetryTimer, &QTimer::timeout, this, [this] {
        if (m_hiddenRefreshQueued) {
            (void)dispatchHiddenRefresh();
        }
    });
    const auto availabilityChanged = [this] { bumpAvailability(); };
    connect(&sessions, &QAbstractItemModel::modelReset, this, availabilityChanged);
    connect(&sessions, &QAbstractItemModel::rowsInserted, this, availabilityChanged);
    connect(&sessions, &QAbstractItemModel::rowsRemoved, this, availabilityChanged);
    connect(&sessions, &QAbstractItemModel::dataChanged, this, availabilityChanged);
    connect(
        &sessions,
        &SessionCatalogModel::authoritativeSnapshotApplied,
        this,
        &SessionActions::reconcilePendingFromCatalog);
}

SessionActions::SessionActions(
    CommandDispatcher& dispatcher,
    SessionCatalogModel& sessions,
    DesktopSettings& settings,
    QObject* parent)
    : SessionActions(dispatcher, sessions, parent)
{
    m_settings = &settings;
    connect(
        &settings,
        &DesktopSettings::settingsChanged,
        this,
        &SessionActions::defaultWorkingDirectoryChanged);
}

SessionActions::SessionActions(
    CommandDispatcher& dispatcher,
    SessionCatalogModel& sessions,
    SessionAccess& access,
    QObject* parent)
    : SessionActions(dispatcher, sessions, parent)
{
    m_access = &access;
    connect(
        &access,
        &SessionAccess::stateChanged,
        this,
        &SessionActions::bumpAvailability);
}

SessionActions::SessionActions(
    CommandDispatcher& dispatcher,
    SessionCatalogModel& sessions,
    SessionAccess& access,
    DesktopSettings& settings,
    QObject* parent)
    : SessionActions(dispatcher, sessions, settings, parent)
{
    m_access = &access;
    connect(
        &access,
        &SessionAccess::stateChanged,
        this,
        &SessionActions::bumpAvailability);
}

QString SessionActions::closeConfirmationSessionId() const
{
    return m_closeConfirmationSessionId;
}

QString SessionActions::deleteConfirmationSessionId() const
{
    return m_deleteConfirmationSessionId;
}

bool SessionActions::creating() const noexcept
{
    return !m_createRequestId.isEmpty();
}

int SessionActions::inactiveCleanupCount() const noexcept
{
    return static_cast<int>(std::ranges::count_if(
        m_pendingReceipts,
        [](const PendingReceipt& pending) {
            return pending.operation == QStringLiteral("session.delete");
        }));
}

QString SessionActions::defaultWorkingDirectory() const
{
    return m_settings != nullptr
        ? m_settings->effectiveWorkingDirectory()
        : QDir::homePath();
}

HiddenSessionsModel* SessionActions::hiddenSessions() noexcept
{
    return &m_hiddenSessions;
}

bool SessionActions::loadingHidden() const noexcept
{
    return !m_hiddenRequestId.isEmpty();
}

QString SessionActions::lastError() const
{
    return m_lastError;
}

quint64 SessionActions::availabilityRevision() const noexcept
{
    return m_availabilityRevision;
}

bool SessionActions::refresh()
{
    if (!m_sessions.beginRefresh()) {
        return true;
    }
    const auto accepted = send({
        {QStringLiteral("type"), QStringLiteral("session.list")},
    });
    if (accepted) {
        m_sessions.refreshDispatched();
    } else {
        m_sessions.refreshDispatchFailed();
    }
    return accepted;
}

bool SessionActions::create(
    const QString& name,
    const QString& workingDirectory)
{
    return dispatchCreate(name, workingDirectory, std::nullopt);
}

bool SessionActions::createResumed(
    const QString& name,
    const QString& conversationPresentationId)
{
    if (m_resumeResolver == nullptr
        || conversationPresentationId.isEmpty()) {
        m_lastError =
            QStringLiteral("Select a current provider conversation to resume.");
        emit stateChanged();
        return false;
    }
    const auto target =
        m_resumeResolver->resolveResumeTarget(conversationPresentationId);
    const QUuid nativeConversationId(
        target ? target->nativeConversationId : QString {});
    if (!target || target->accountEpoch != m_accountEpoch
        || target->accountUserId != m_accountUserId
        || (target->provider != QStringLiteral("claude")
            && target->provider != QStringLiteral("copilot"))
        || nativeConversationId.isNull()
        || nativeConversationId.toString(QUuid::WithoutBraces)
            != target->nativeConversationId) {
        m_lastError = QStringLiteral(
            "The selected provider conversation is no longer current.");
        emit stateChanged();
        return false;
    }
    return dispatchCreate(
        name,
        target->workingDirectory,
        QJsonObject {
            {QStringLiteral("provider"), target->provider},
            {QStringLiteral("nativeConversationId"),
             target->nativeConversationId},
        });
}

void SessionActions::setProviderConversationResumeResolver(
    ProviderConversationResumeResolver* resolver)
{
    m_resumeResolver = resolver;
}

bool SessionActions::dispatchCreate(
    const QString& name,
    const QString& workingDirectory,
    std::optional<QJsonObject> resume)
{
    const auto trimmedName = name.trimmed();
    const auto nameBytes = trimmedName.toUtf8();
    const auto trimmedDirectory = workingDirectory.trimmed();
    const QFileInfo directory(trimmedDirectory);
    if (!m_createRequestId.isEmpty()) {
        m_lastError = QStringLiteral("A session is already being created.");
        emit stateChanged();
        return false;
    }
    if (trimmedName.isEmpty() || nameBytes.size() > 128
        || trimmedDirectory.isEmpty() || trimmedDirectory.size() > 4'096
        || !directory.exists() || !directory.isDir() || !directory.isReadable()
        || !directory.isExecutable()) {
        m_lastError =
            QStringLiteral("Enter a session name and a readable working directory.");
        emit stateChanged();
        return false;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command {
        {QStringLiteral("type"), QStringLiteral("session.create")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("name"), trimmedName},
        {QStringLiteral("workingDir"), QDir::cleanPath(trimmedDirectory)},
    };
    if (resume) {
        command.insert(QStringLiteral("resume"), std::move(*resume));
    }
    const auto accepted = send(std::move(command));
    if (accepted) {
        m_createRequestId = requestId;
        m_pendingReceipts.insert(requestId, {
            .operation = QStringLiteral("session.create"),
            .sessionId = {},
            .incarnationId = {},
        });
        emit stateChanged();
    }
    return accepted;
}

bool SessionActions::createDefault()
{
    return create(generatedSessionName(), defaultWorkingDirectory());
}

bool SessionActions::canInterrupt(const QString& sessionId) const
{
    const auto context = m_sessions.actionContext(sessionId);
    if (!context || !context->commandable || !interruptStatus(context->status)) {
        return false;
    }
    const auto ownerControlled =
        context->kind == QStringLiteral("local") || context->owner.isEmpty();
    return ownerControlled
        && (context->kind == QStringLiteral("local") || stageReady(*context));
}

bool SessionActions::canClose(const QString& sessionId) const
{
    const auto context = m_sessions.actionContext(sessionId);
    return context && context->commandable
        && context->kind == QStringLiteral("local")
        && closeStatus(context->status);
}

bool SessionActions::canSetMode(const QString& sessionId) const
{
    constexpr quint32 setModePermission = 1U << 7;
    const auto context = m_sessions.actionContext(sessionId);
    if (!context || !context->commandable) {
        return false;
    }
    const auto ownerControlled =
        context->kind == QStringLiteral("local") || context->owner.isEmpty();
    return ownerControlled
        && (context->kind == QStringLiteral("local")
            || (context->permissions & setModePermission) != 0);
}

bool SessionActions::canRename(const QString& sessionId) const
{
    constexpr quint32 renamePermission = 1U << 5;
    const auto context = m_sessions.actionContext(sessionId);
    return context && !hasPendingLifecycle(sessionId)
        && !m_pendingRenames.contains(sessionId)
        && (context->kind == QStringLiteral("local")
            || (context->permissions & renamePermission) != 0);
}

bool SessionActions::canReopen(const QString& sessionId) const
{
    const auto context = m_sessions.actionContext(sessionId);
    return context && !hasPendingLifecycle(sessionId)
        && !m_pendingRenames.contains(sessionId)
        && context->kind == QStringLiteral("local")
        && ((context->status == QStringLiteral("stopped")
                && context->recovery == QStringLiteral("recoverable"))
            || (context->status == QStringLiteral("blocked")
                && (context->recovery == QStringLiteral("recoverable")
                    || context->recovery == QStringLiteral("crashed"))));
}

bool SessionActions::canDelete(const QString& sessionId) const
{
    const auto context = m_sessions.actionContext(sessionId);
    return context && !hasPendingLifecycle(sessionId)
        && !m_pendingRenames.contains(sessionId)
        && context->kind == QStringLiteral("local")
        && (!context->commandable
            || context->status == QStringLiteral("stopped"));
}

bool SessionActions::canOpenRemote(const QString& sessionId) const
{
    constexpr quint32 viewPermission = 1U;
    const auto context = m_sessions.actionContext(sessionId);
    const auto liveStatus = context
        && (interruptStatus(context->status)
            || context->status == QStringLiteral("reconnecting"));
    const auto accessBlocked = context
        && (context->accessState == QStringLiteral("accessDenied")
            || context->accessState == QStringLiteral("failed"));
    const auto stageReady = context
        && context->connectionState == QStringLiteral("connected")
        && context->accessState == QStringLiteral("ready");
    return context && context->kind == QStringLiteral("remote")
        && context->commandable && liveStatus && !accessBlocked && !stageReady
        && (context->permissions & viewPermission) != 0
        && !m_pendingRemoteOpens.contains(sessionId)
        && (!m_access || !m_access->hasPendingLeave(sessionId));
}

bool SessionActions::canHide(const QString& sessionId) const
{
    const auto context = m_sessions.actionContext(sessionId);
    return context && context->kind == QStringLiteral("remote")
        && !m_pendingHides.contains(sessionId)
        && (!m_access || !m_access->hasPendingLeave(sessionId));
}

bool SessionActions::interrupt(const QString& sessionId)
{
    const auto context = m_sessions.actionContext(sessionId);
    if (!context || !canInterrupt(sessionId)) {
        m_lastError = QStringLiteral("This session can no longer be interrupted.");
        emit stateChanged();
        return false;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    const auto accepted = send({
        {QStringLiteral("type"), QStringLiteral("session.interrupt")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("expectedRuntimeIncarnationId"), context->incarnationId},
    });
    if (accepted) {
        m_pendingReceipts.insert(requestId, {
            .operation = QStringLiteral("session.interrupt"),
            .sessionId = sessionId,
            .incarnationId = context->incarnationId,
        });
        bumpAvailability();
    }
    return accepted;
}

bool SessionActions::requestCloseConfirmation(const QString& sessionId)
{
    const auto context = m_sessions.actionContext(sessionId);
    if (!context || !canClose(sessionId)) {
        m_lastError = QStringLiteral("This session can no longer be closed.");
        emit stateChanged();
        return false;
    }
    m_closeConfirmationSessionId = sessionId;
    m_closeConfirmationIncarnationId = context->incarnationId;
    emit stateChanged();
    return true;
}

bool SessionActions::confirmClose(const QString& sessionId)
{
    const auto context = m_sessions.actionContext(sessionId);
    if (!context || sessionId != m_closeConfirmationSessionId
        || context->incarnationId != m_closeConfirmationIncarnationId
        || !canClose(sessionId)) {
        cancelCloseConfirmation();
        m_lastError =
            QStringLiteral("The session changed before close was confirmed.");
        emit stateChanged();
        return false;
    }
    const auto incarnationId = m_closeConfirmationIncarnationId;
    cancelCloseConfirmation();
    return dispatchClose(sessionId, incarnationId);
}

void SessionActions::cancelCloseConfirmation()
{
    if (m_closeConfirmationSessionId.isEmpty()
        && m_closeConfirmationIncarnationId.isEmpty()) {
        return;
    }
    m_closeConfirmationSessionId.clear();
    m_closeConfirmationIncarnationId.clear();
    emit stateChanged();
}

bool SessionActions::rename(
    const QString& sessionId,
    const QString& name)
{
    const auto context = m_sessions.actionContext(sessionId);
    const auto trimmed = name.trimmed();
    const auto nameBytes = trimmed.toUtf8();
    const auto presentation = m_sessions.presentationForSession(sessionId);
    if (!context || !canRename(sessionId) || trimmed.isEmpty()
        || nameBytes.size() > 128
        || presentation.value(QStringLiteral("name")).toString() == trimmed) {
        m_lastError = QStringLiteral("That session rename is no longer available.");
        emit stateChanged();
        return false;
    }
    if (!send({
            {QStringLiteral("type"), QStringLiteral("session.rename")},
            {QStringLiteral("sessionId"), sessionId},
            {QStringLiteral("name"), trimmed},
        })) {
        return false;
    }
    m_pendingRenames.insert(sessionId, {
        .incarnationId = context->incarnationId,
        .submittedName = trimmed,
    });
    bumpAvailability();
    emit stateChanged();
    return true;
}

bool SessionActions::reopen(const QString& sessionId)
{
    const auto context = m_sessions.actionContext(sessionId);
    if (!context || !canReopen(sessionId)) {
        m_lastError = QStringLiteral("That local session can no longer be reopened.");
        emit stateChanged();
        return false;
    }
    return dispatchLifecycle(
        QStringLiteral("session.reopen"),
        QStringLiteral("session.reopen"),
        sessionId,
        context->incarnationId);
}

bool SessionActions::requestDeleteConfirmation(const QString& sessionId)
{
    const auto context = m_sessions.actionContext(sessionId);
    if (!context || !canDelete(sessionId)) {
        m_lastError = QStringLiteral("That local session can no longer be deleted.");
        emit stateChanged();
        return false;
    }
    m_deleteConfirmationSessionId = sessionId;
    m_deleteConfirmationIncarnationId = context->incarnationId;
    emit stateChanged();
    return true;
}

bool SessionActions::confirmDelete(const QString& sessionId)
{
    const auto context = m_sessions.actionContext(sessionId);
    if (!context || sessionId != m_deleteConfirmationSessionId
        || context->incarnationId != m_deleteConfirmationIncarnationId
        || !canDelete(sessionId)) {
        cancelDeleteConfirmation();
        m_lastError =
            QStringLiteral("The session changed before delete was confirmed.");
        emit stateChanged();
        return false;
    }
    const auto incarnationId = m_deleteConfirmationIncarnationId;
    cancelDeleteConfirmation();
    return dispatchLifecycle(
        QStringLiteral("session.delete"),
        QStringLiteral("session.delete"),
        sessionId,
        incarnationId);
}

void SessionActions::cancelDeleteConfirmation()
{
    if (m_deleteConfirmationSessionId.isEmpty()
        && m_deleteConfirmationIncarnationId.isEmpty()) {
        return;
    }
    m_deleteConfirmationSessionId.clear();
    m_deleteConfirmationIncarnationId.clear();
    emit stateChanged();
}

bool SessionActions::openRemote(const QString& sessionId)
{
    const auto context = m_sessions.actionContext(sessionId);
    if (!context || !canOpenRemote(sessionId)) {
        m_lastError =
            QStringLiteral("That remote session can no longer be opened.");
        emit stateChanged();
        if (context) {
            emit remoteOpenFailed(sessionId, context->incarnationId);
        }
        return false;
    }
    if (!send({
            {QStringLiteral("type"), QStringLiteral("session.openRemote")},
            {QStringLiteral("sessionId"), sessionId},
        })) {
        emit remoteOpenFailed(sessionId, context->incarnationId);
        return false;
    }
    m_pendingRemoteOpens.insert(sessionId, context->incarnationId);
    bumpAvailability();
    return true;
}

bool SessionActions::restoreRemote(const QString& sessionId)
{
    const auto projectedContext = m_sessions.actionContext(sessionId);
    const auto projectedPending =
        m_pendingRemoteOpens.constFind(sessionId);
    if (projectedPending != m_pendingRemoteOpens.cend()
        && (!projectedContext
            || projectedContext->kind != QStringLiteral("remote")
            || projectedContext->incarnationId != projectedPending.value())) {
        reconcilePendingFromCatalog();
    }
    const auto context = m_sessions.actionContext(sessionId);
    const auto presentation =
        m_sessions.presentationSession(sessionId);
    const auto pending = m_pendingRemoteOpens.constFind(sessionId);
    if (context && pending != m_pendingRemoteOpens.cend()
        && pending.value() == context->incarnationId
        && presentation && presentation->isRemoteConnectable
        && (!m_access || !m_access->hasPendingLeave(sessionId))) {
        return true;
    }
    if (!context || !presentation || !presentation->isRemoteConnectable
        || pending != m_pendingRemoteOpens.cend()
        || (m_access && m_access->hasPendingLeave(sessionId))) {
        m_lastError =
            QStringLiteral("That remote session could not be restored.");
        emit stateChanged();
        if (context) {
            emit remoteOpenFailed(sessionId, context->incarnationId);
        }
        return false;
    }
    if (!send({
            {QStringLiteral("type"), QStringLiteral("session.openRemote")},
            {QStringLiteral("sessionId"), sessionId},
        })) {
        emit remoteOpenFailed(sessionId, context->incarnationId);
        return false;
    }
    m_pendingRemoteOpens.insert(sessionId, context->incarnationId);
    bumpAvailability();
    return true;
}

bool SessionActions::hide(const QString& sessionId)
{
    if (!canHide(sessionId)
        || !send({
            {QStringLiteral("type"), QStringLiteral("session.hide")},
            {QStringLiteral("sessionId"), sessionId},
        })) {
        if (m_lastError.isEmpty()) {
            m_lastError =
                QStringLiteral("That remote session can no longer be hidden.");
            emit stateChanged();
        }
        return false;
    }
    m_pendingHides.insert(sessionId);
    bumpAvailability();
    (void)refreshHidden();
    return true;
}

bool SessionActions::unhide(const QString& sessionId)
{
    if (!m_hiddenSessions.contains(sessionId)
        || m_pendingUnhides.contains(sessionId)
        || !send({
            {QStringLiteral("type"), QStringLiteral("session.unhide")},
            {QStringLiteral("sessionId"), sessionId},
        })) {
        if (m_lastError.isEmpty()) {
            m_lastError =
                QStringLiteral("That hidden session can no longer be restored.");
            emit stateChanged();
        }
        return false;
    }
    m_pendingUnhides.insert(sessionId);
    (void)refreshHidden();
    return true;
}

bool SessionActions::refreshHidden()
{
    if (m_accountUserId.isEmpty()) {
        return false;
    }
    m_hiddenRefreshQueued = true;
    m_hiddenRetryAttempts = 0;
    return dispatchHiddenRefresh();
}

bool SessionActions::dispatchHiddenRefresh()
{
    if (!m_hiddenRequestId.isEmpty()) {
        m_hiddenRefreshQueued = true;
        return true;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    if (!send({
            {QStringLiteral("type"), QStringLiteral("session.listHidden")},
            {QStringLiteral("requestId"), requestId},
        })) {
        m_hiddenRefreshQueued = true;
        if (m_hiddenRetryAttempts < 3) {
            ++m_hiddenRetryAttempts;
            m_hiddenRetryTimer.start(250);
        } else {
            m_lastError = QStringLiteral(
                "Could not refresh hidden sessions. Retry from Hidden.");
            emit stateChanged();
        }
        return false;
    }
    m_hiddenRetryTimer.stop();
    m_hiddenRefreshQueued = false;
    m_hiddenRetryAttempts = 0;
    m_hiddenRequestId = requestId;
    emit stateChanged();
    return true;
}

bool SessionActions::dispatchClose(
    const QString& sessionId,
    const QString& incarnationId)
{
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    const auto accepted = send({
        {QStringLiteral("type"), QStringLiteral("session.close")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("expectedRuntimeIncarnationId"), incarnationId},
    });
    if (accepted) {
        m_pendingReceipts.insert(requestId, {
            .operation = QStringLiteral("session.close"),
            .sessionId = sessionId,
            .incarnationId = incarnationId,
        });
        bumpAvailability();
    }
    return accepted;
}

bool SessionActions::dispatchLifecycle(
    const QString& operation,
    const QString& commandType,
    const QString& sessionId,
    const QString& incarnationId)
{
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    const auto accepted = send({
        {QStringLiteral("type"), commandType},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("expectedRuntimeIncarnationId"), incarnationId},
    });
    if (accepted) {
        m_pendingReceipts.insert(requestId, {
            .operation = operation,
            .sessionId = sessionId,
            .incarnationId = incarnationId,
        });
        bumpAvailability();
    }
    return accepted;
}

bool SessionActions::hasPendingLifecycle(const QString& sessionId) const
{
    return std::ranges::any_of(
        m_pendingReceipts,
        [&](const PendingReceipt& pending) {
            return pending.sessionId == sessionId
                && pending.operation != QStringLiteral("session.create");
        })
        || (m_access && m_access->hasPendingLeave(sessionId));
}

bool SessionActions::setMode(
    const QString& sessionId,
    const QString& mode)
{
    static const QSet<QString> modes {
        QStringLiteral("normal"),
        QStringLiteral("plan"),
        QStringLiteral("autopilot"),
    };
    const auto context = m_sessions.actionContext(sessionId);
    if (!context || !canSetMode(sessionId) || !modes.contains(mode)) {
        m_lastError = QStringLiteral("This session mode change is not available.");
        emit stateChanged();
        return false;
    }
    const auto accepted = send({
        {QStringLiteral("type"), QStringLiteral("session.mode")},
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("expectedRuntimeIncarnationId"), context->incarnationId},
        {QStringLiteral("mode"), mode},
    });
    if (accepted) {
        m_pendingModeIncarnations.insert(sessionId, context->incarnationId);
    }
    return accepted;
}

void SessionActions::clearError()
{
    if (m_lastError.isEmpty()) {
        return;
    }
    m_lastError.clear();
    emit stateChanged();
}

void SessionActions::ingestAuthEvent(QByteArray json)
{
    const auto document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return;
    }
    const auto object = document.object();
    const auto type = object.value(QStringLiteral("type"));
    if (!type.isString()
        || (type.toString() != QStringLiteral("auth.ready")
            && type.toString() != QStringLiteral("auth.required"))) {
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!epoch) {
        return;
    }
    activateAccount(
        type.toString() == QStringLiteral("auth.ready")
            ? object.value(QStringLiteral("userId")).toString()
            : QString {},
        *epoch);
}

void SessionActions::ingestSessionEvent(QByteArray json)
{
    const auto document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return;
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("authority")).toString()
        != QStringLiteral("accountContext")) {
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    if (!epoch) {
        return;
    }
    const auto admission = m_accountFence.admit(
        {
            .userId = object.value(QStringLiteral("accountUserId")).toString(),
            .epoch = *epoch,
        },
        std::move(json));
    if (admission == AccountEventAdmission::Current) {
        applySessionEvent(object);
    }
}

void SessionActions::resetRuntimeAuthority()
{
    m_accountFence.reset();
    m_pendingReceipts.clear();
    m_pendingModeIncarnations.clear();
    m_pendingRenames.clear();
    m_pendingRemoteOpens.clear();
    m_pendingHides.clear();
    m_pendingUnhides.clear();
    m_hiddenSessions.replace({});
    m_hiddenRetryTimer.stop();
    m_closeConfirmationSessionId.clear();
    m_closeConfirmationIncarnationId.clear();
    m_deleteConfirmationSessionId.clear();
    m_deleteConfirmationIncarnationId.clear();
    m_createRequestId.clear();
    m_hiddenRequestId.clear();
    m_hiddenRefreshQueued = false;
    m_hiddenRetryAttempts = 0;
    m_accountUserId.clear();
    m_accountEpoch = 0;
    m_lastError.clear();
    bumpAvailability();
    emit stateChanged();
}

void SessionActions::activateAccount(QString userId, const quint64 epoch)
{
    auto activation =
        m_accountFence.activate({.userId = userId, .epoch = epoch});
    if (!activation.accepted) {
        return;
    }
    if (activation.changed) {
        const auto createReceipt = m_createRequestId.isEmpty()
            ? std::optional<PendingReceipt> {}
            : std::optional<PendingReceipt> {
                  m_pendingReceipts.value(m_createRequestId)};
        m_pendingReceipts.clear();
        if (createReceipt && !createReceipt->operation.isEmpty()) {
            m_pendingReceipts.insert(m_createRequestId, *createReceipt);
        }
        m_pendingModeIncarnations.clear();
        m_pendingRenames.clear();
        m_pendingRemoteOpens.clear();
        m_pendingHides.clear();
        m_pendingUnhides.clear();
        m_hiddenSessions.replace({});
        m_hiddenRetryTimer.stop();
        m_closeConfirmationSessionId.clear();
        m_closeConfirmationIncarnationId.clear();
        m_deleteConfirmationSessionId.clear();
        m_deleteConfirmationIncarnationId.clear();
        m_hiddenRequestId.clear();
        m_hiddenRefreshQueued = false;
        m_hiddenRetryAttempts = 0;
        m_accountUserId = std::move(userId);
        m_accountEpoch = epoch;
        m_lastError.clear();
        bumpAvailability();
        emit stateChanged();
    }
    for (auto& pending : activation.pendingEvents) {
        ingestSessionEvent(std::move(pending));
    }
    if (activation.changed) {
        (void)refresh();
        if (!m_accountUserId.isEmpty()) {
            (void)refreshHidden();
        }
    }
}

void SessionActions::applySessionEvent(const QJsonObject& object)
{
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("session.opened")) {
        const auto sessionId =
            object.value(QStringLiteral("sessionId")).toString();
        const auto pending = m_pendingRemoteOpens.find(sessionId);
        const auto context = m_sessions.actionContext(sessionId);
        if (pending != m_pendingRemoteOpens.end() && context
            && context->incarnationId == pending.value()) {
            const auto incarnationId = pending.value();
            m_pendingRemoteOpens.erase(pending);
            m_lastError.clear();
            bumpAvailability();
            emit stateChanged();
            emit remoteOpenSucceeded(sessionId, incarnationId);
        }
        return;
    }
    if (type == QStringLiteral("session.hiddenList")) {
        applyHiddenList(object);
        return;
    }
    if (type == QStringLiteral("session.created")) {
        const auto requestId = object.value(QStringLiteral("requestId"));
        const auto sessionId = object.value(QStringLiteral("sessionId"));
        const auto incarnation =
            object.value(QStringLiteral("runtimeIncarnationId"));
        if (!requestId.isString() || requestId.toString() != m_createRequestId
            || !sessionId.isString() || sessionId.toString().isEmpty()
            || !incarnation.isString() || incarnation.toString().isEmpty()) {
            return;
        }
        m_pendingReceipts.remove(requestId.toString());
        m_createRequestId.clear();
        m_lastError.clear();
        emit stateChanged();
        emit sessionCreated(sessionId.toString());
        return;
    }
    if (type == QStringLiteral("session.interrupted")) {
        const auto requestId = object.value(QStringLiteral("requestId"));
        const auto sessionId = object.value(QStringLiteral("sessionId"));
        const auto incarnation =
            object.value(QStringLiteral("runtimeIncarnationId"));
        if (!requestId.isString() || !sessionId.isString()
            || !incarnation.isString()) {
            return;
        }
        const auto pending = m_pendingReceipts.find(requestId.toString());
        if (pending != m_pendingReceipts.end()
            && pending->operation == QStringLiteral("session.interrupt")
            && pending->sessionId == sessionId.toString()
            && pending->incarnationId == incarnation.toString()) {
            m_pendingReceipts.erase(pending);
            bumpAvailability();
        }
        return;
    }
    if (type == QStringLiteral("session.upsert")) {
        const auto session = object.value(QStringLiteral("session"));
        if (session.isObject()) {
            const auto value = session.toObject();
            const auto id = value.value(QStringLiteral("id")).toString();
            const auto context = m_sessions.actionContext(id);
            const auto incarnation =
                context ? context->incarnationId : QString {};
            if (m_pendingModeIncarnations.value(id) == incarnation) {
                m_pendingModeIncarnations.remove(id);
                bumpAvailability();
                emit stateChanged();
            }
            reconcilePendingFromCatalog();
        }
        return;
    }
    if (type == QStringLiteral("session.removed")) {
        const auto sessionId = object.value(QStringLiteral("sessionId")).toString();
        m_pendingModeIncarnations.remove(sessionId);
        m_pendingRenames.remove(sessionId);
        const auto remoteIncarnation =
            m_pendingRemoteOpens.take(sessionId);
        if (!remoteIncarnation.isEmpty()) {
            emit remoteOpenFailed(sessionId, remoteIncarnation);
        }
        m_pendingHides.remove(sessionId);
        m_pendingUnhides.remove(sessionId);
        bool changed = false;
        for (auto pending = m_pendingReceipts.begin();
             pending != m_pendingReceipts.end();) {
            if (pending->sessionId == sessionId) {
                pending = m_pendingReceipts.erase(pending);
                changed = true;
            } else {
                ++pending;
            }
        }
        if (m_deleteConfirmationSessionId == sessionId) {
            m_deleteConfirmationSessionId.clear();
            m_deleteConfirmationIncarnationId.clear();
            changed = true;
        }
        if (changed) {
            m_lastError.clear();
            bumpAvailability();
            emit stateChanged();
        }
        return;
    }
    if (type != QStringLiteral("session.error")) {
        return;
    }
    const auto operation = object.value(QStringLiteral("operation"));
    const auto message = object.value(QStringLiteral("message"));
    const auto requestId = object.value(QStringLiteral("requestId"));
    const auto sessionId = object.value(QStringLiteral("sessionId"));
    if (!operation.isString() || !message.isString()
        || message.toString().isEmpty()) {
        return;
    }
    bool correlated = false;
    if (requestId.isString()) {
        const auto pending = m_pendingReceipts.find(requestId.toString());
        if (pending != m_pendingReceipts.end()
            && pending->operation == operation.toString()
            && (!sessionId.isString()
                || pending->sessionId == sessionId.toString())) {
            m_pendingReceipts.erase(pending);
            correlated = true;
        }
        if (!correlated
            && operation.toString() == QStringLiteral("session.listHidden")
            && requestId.toString() == m_hiddenRequestId) {
            const auto refreshQueued = m_hiddenRefreshQueued;
            m_hiddenRequestId.clear();
            m_hiddenRefreshQueued = false;
            correlated = true;
            if (refreshQueued) {
                (void)refreshHidden();
            }
        }
    } else if (operation.toString() == QStringLiteral("session.mode")
        && sessionId.isString()
        && m_pendingModeIncarnations.contains(sessionId.toString())) {
        m_pendingModeIncarnations.remove(sessionId.toString());
        correlated = true;
    } else if (operation.toString() == QStringLiteral("session.rename")
        && sessionId.isString()
        && m_pendingRenames.contains(sessionId.toString())) {
        m_pendingRenames.remove(sessionId.toString());
        correlated = true;
    } else if (operation.toString() == QStringLiteral("session.openRemote")
        && sessionId.isString()
        && m_pendingRemoteOpens.contains(sessionId.toString())) {
        const auto failedSessionId = sessionId.toString();
        const auto incarnationId =
            m_pendingRemoteOpens.take(failedSessionId);
        correlated = true;
        emit remoteOpenFailed(failedSessionId, incarnationId);
    } else if (operation.toString() == QStringLiteral("session.hide")
        && sessionId.isString()
        && m_pendingHides.remove(sessionId.toString())) {
        correlated = true;
    } else if (operation.toString() == QStringLiteral("session.unhide")
        && sessionId.isString()
        && m_pendingUnhides.remove(sessionId.toString())) {
        correlated = true;
    }
    if (correlated) {
        if (requestId.isString() && requestId.toString() == m_createRequestId) {
            m_createRequestId.clear();
        }
        m_lastError = message.toString();
        bumpAvailability();
        emit stateChanged();
    }
}

void SessionActions::reconcilePendingFromCatalog()
{
    bool changed = false;
    QString lifecycleError;
    for (auto rename = m_pendingRenames.begin();
         rename != m_pendingRenames.end();) {
        const auto sessionId = rename.key();
        if (!m_sessions.containsSession(sessionId)) {
            rename = m_pendingRenames.erase(rename);
            lifecycleError =
                QStringLiteral("The renamed session no longer exists.");
            changed = true;
            continue;
        }
        const auto name = m_sessions.presentationForSession(sessionId)
                              .value(QStringLiteral("name"))
                              .toString();
        if (name == rename->submittedName) {
            rename = m_pendingRenames.erase(rename);
            changed = true;
        } else {
            ++rename;
        }
    }

    for (auto pending = m_pendingReceipts.begin();
         pending != m_pendingReceipts.end();) {
        if (pending->operation == QStringLiteral("session.reopen")) {
            const auto context = m_sessions.actionContext(pending->sessionId);
            if (!context) {
                lifecycleError =
                    QStringLiteral("The session disappeared while reopening.");
                pending = m_pendingReceipts.erase(pending);
                changed = true;
                continue;
            }
            if (context->incarnationId != pending->incarnationId) {
                const auto sessionId = pending->sessionId;
                const auto live =
                    context->recovery == QStringLiteral("live")
                    && (context->status == QStringLiteral("active")
                        || context->status == QStringLiteral("waiting")
                        || context->status == QStringLiteral("blocked")
                        || context->status == QStringLiteral("reconnecting"));
                pending = m_pendingReceipts.erase(pending);
                changed = true;
                if (live) {
                    emit sessionReopened(sessionId);
                } else {
                    lifecycleError = QStringLiteral(
                        "The session reopened but stopped before becoming ready.");
                }
                continue;
            }
        } else if ((pending->operation == QStringLiteral("session.delete")
                       || pending->operation == QStringLiteral("session.close"))
            && !m_sessions.containsSession(pending->sessionId)) {
            pending = m_pendingReceipts.erase(pending);
            changed = true;
            continue;
        }
        ++pending;
    }

    for (auto pending = m_pendingRemoteOpens.begin();
         pending != m_pendingRemoteOpens.end();) {
        const auto context = m_sessions.actionContext(pending.key());
        const auto invalidated =
            !context || context->kind != QStringLiteral("remote")
            || context->incarnationId != pending.value();
        if (invalidated) {
            const auto sessionId = pending.key();
            const auto incarnationId = pending.value();
            pending = m_pendingRemoteOpens.erase(pending);
            changed = true;
            emit remoteOpenFailed(sessionId, incarnationId);
        } else if (context->connectionState == QStringLiteral("connected")
            && context->accessState == QStringLiteral("ready")) {
            const auto sessionId = pending.key();
            const auto incarnationId = pending.value();
            pending = m_pendingRemoteOpens.erase(pending);
            changed = true;
            emit remoteOpenSucceeded(sessionId, incarnationId);
        } else {
            ++pending;
        }
    }

    if (changed) {
        m_lastError = lifecycleError;
        bumpAvailability();
        emit stateChanged();
    }
}

void SessionActions::applyHiddenList(const QJsonObject& object)
{
    const auto requestId =
        object.value(QStringLiteral("requestId")).toString();
    const auto entries = object.value(QStringLiteral("entries"));
    if (requestId != m_hiddenRequestId) {
        return;
    }
    const auto reject = [this] {
        const auto refreshQueued = m_hiddenRefreshQueued;
        m_hiddenRequestId.clear();
        m_hiddenRefreshQueued = false;
        m_lastError =
            QStringLiteral("The hidden-session inventory is invalid.");
        emit stateChanged();
        if (refreshQueued) {
            (void)refreshHidden();
        }
    };
    if (!entries.isArray() || entries.toArray().size() > 5'000) {
        reject();
        return;
    }
    QVector<HiddenSessionsModel::Entry> decoded;
    QSet<QString> ids;
    for (const auto& row : entries.toArray()) {
        if (!row.isObject()) {
            reject();
            return;
        }
        const auto value = row.toObject();
        const auto id = value.value(QStringLiteral("id"));
        const auto name = value.value(QStringLiteral("name"));
        const auto project = value.value(QStringLiteral("project"));
        const auto owner = value.value(QStringLiteral("owner"));
        if (!id.isString() || id.toString().isEmpty()
            || !name.isString() || name.toString().isEmpty()
            || !project.isString() || project.toString().isEmpty()
            || !owner.isString() || ids.contains(id.toString())) {
            reject();
            return;
        }
        ids.insert(id.toString());
        decoded.push_back({
            .id = id.toString(),
            .name = name.toString(),
            .project = project.toString(),
            .owner = owner.toString(),
        });
    }
    const auto refreshQueued = m_hiddenRefreshQueued;
    m_hiddenRequestId.clear();
    m_hiddenRefreshQueued = false;
    m_hiddenSessions.replace(std::move(decoded));
    QString mutationError;
    bool settledMutation = false;
    if (!refreshQueued) {
        settledMutation =
            !m_pendingHides.isEmpty() || !m_pendingUnhides.isEmpty();
        for (const auto& sessionId : std::as_const(m_pendingHides)) {
            if (!m_hiddenSessions.contains(sessionId)
                && m_sessions.containsSession(sessionId)) {
                mutationError =
                    QStringLiteral("The remote session was not hidden.");
            }
        }
        for (const auto& sessionId : std::as_const(m_pendingUnhides)) {
            if (m_hiddenSessions.contains(sessionId)) {
                mutationError =
                    QStringLiteral("The hidden session was not restored.");
            }
        }
        m_pendingHides.clear();
        m_pendingUnhides.clear();
    }
    if (settledMutation) {
        m_lastError = mutationError;
    }
    bumpAvailability();
    emit stateChanged();
    if (refreshQueued) {
        (void)refreshHidden();
    }
}

bool SessionActions::send(QJsonObject command)
{
    m_lastError.clear();
    const auto json = QJsonDocument(std::move(command)).toJson(QJsonDocument::Compact);
    if (m_dispatcher.send(CommandLane::Sessions, json)) {
        emit stateChanged();
        return true;
    }
    m_lastError = QStringLiteral("The runtime did not accept the session request.");
    emit stateChanged();
    return false;
}

void SessionActions::bumpAvailability()
{
    if (!m_closeConfirmationSessionId.isEmpty()) {
        const auto context =
            m_sessions.actionContext(m_closeConfirmationSessionId);
        if (!context
            || context->incarnationId != m_closeConfirmationIncarnationId
            || !canClose(m_closeConfirmationSessionId)) {
            m_closeConfirmationSessionId.clear();
            m_closeConfirmationIncarnationId.clear();
            emit stateChanged();
        }
    }
    if (!m_deleteConfirmationSessionId.isEmpty()) {
        const auto context =
            m_sessions.actionContext(m_deleteConfirmationSessionId);
        if (!context
            || context->incarnationId != m_deleteConfirmationIncarnationId
            || !canDelete(m_deleteConfirmationSessionId)) {
            m_deleteConfirmationSessionId.clear();
            m_deleteConfirmationIncarnationId.clear();
            emit stateChanged();
        }
    }
    ++m_availabilityRevision;
    emit availabilityChanged();
}

bool SessionActions::interruptStatus(const QString& status)
{
    return status == QStringLiteral("active")
        || status == QStringLiteral("waiting")
        || status == QStringLiteral("blocked");
}

bool SessionActions::closeStatus(const QString& status)
{
    return interruptStatus(status)
        || status == QStringLiteral("reconnecting");
}

QString SessionActions::generatedSessionName()
{
    static constexpr std::array adjectives {
        "Amber", "Brass", "Cedar", "Copper", "Dusk", "Ember",
        "Fern", "Flint", "Juniper", "Moss", "Slate", "Willow",
    };
    static constexpr std::array animals {
        "Badger", "Crane", "Falcon", "Fox", "Heron", "Lark",
        "Otter", "Owl", "Raven", "Sparrow", "Wolf", "Wren",
    };
    const auto adjective = adjectives.at(
        QRandomGenerator::global()->bounded(
            static_cast<quint32>(adjectives.size())));
    const auto animal = animals.at(
        QRandomGenerator::global()->bounded(
            static_cast<quint32>(animals.size())));
    return QString::fromLatin1(adjective)
        + QLatin1Char(' ')
        + QString::fromLatin1(animal);
}

bool SessionActions::stageReady(
    const SessionCatalogModel::ActionContext& context)
{
    return context.connectionState == QStringLiteral("connected")
        && context.accessState == QStringLiteral("ready")
        && (context.permissions & 1U) != 0;
}

} // namespace kodosi
