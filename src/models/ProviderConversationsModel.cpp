#include "models/ProviderConversationsModel.hpp"

#include "models/DesktopSettings.hpp"
#include "platform/DesktopFileIntegration.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <utility>

namespace kodosi {
namespace {

constexpr qsizetype identifierLimit = 4'096;
constexpr qsizetype accountIdLimit = 1'024;
constexpr qsizetype cursorLimit = 512;
constexpr qsizetype titleLimit = 1'024;
constexpr qsizetype timestampLimit = 64;
constexpr qsizetype noticeLimit = 4'096;
constexpr qsizetype roleLimit = 64;
constexpr qsizetype toolNameLimit = 256;
constexpr quint64 maximumExactJsonInteger = 9'007'199'254'740'991ULL;

QString translated(const char* value)
{
    return QCoreApplication::translate("ProviderConversationsModel", value);
}

bool boundedText(
    const QStringView value,
    const qsizetype maximum,
    const bool allowEmpty)
{
    return (allowEmpty || !value.isEmpty()) && !value.contains(QChar::Null)
        && value.toUtf8().size() <= maximum;
}

std::optional<QString> requiredString(
    const QJsonObject& object,
    const QString& key,
    const qsizetype maximum = identifierLimit,
    const bool allowEmpty = false)
{
    const auto value = object.value(key);
    if (!value.isString()) {
        return std::nullopt;
    }
    const auto text = value.toString();
    return boundedText(text, maximum, allowEmpty)
        ? std::optional<QString>(text)
        : std::nullopt;
}

bool nullableString(
    const QJsonObject& object,
    const QString& key,
    QString& destination,
    const qsizetype maximum,
    const bool allowEmpty)
{
    if (!object.contains(key)) {
        return false;
    }
    const auto value = object.value(key);
    if (value.isNull()) {
        destination.clear();
        return true;
    }
    if (!value.isString()) {
        return false;
    }
    const auto text = value.toString();
    if (!boundedText(text, maximum, allowEmpty)) {
        return false;
    }
    destination = text;
    return true;
}

bool nullableAccount(
    const QJsonObject& object,
    const QString& key,
    QString& destination)
{
    const auto value = object.value(key);
    if (value.isUndefined() || value.isNull()) {
        destination.clear();
        return true;
    }
    if (!value.isString()
        || !boundedText(value.toString(), accountIdLimit, false)) {
        return false;
    }
    destination = value.toString();
    return true;
}

bool exactFields(
    const QJsonObject& object,
    const std::initializer_list<QString> fields)
{
    const auto keys = object.keys();
    return QSet<QString>(keys.cbegin(), keys.cend())
        == QSet<QString>(fields.begin(), fields.end());
}

bool exactAccountEnvelope(
    const QJsonObject& object,
    const std::initializer_list<QString> fields)
{
    QSet<QString> expected {
        QStringLiteral("authority"),
        QStringLiteral("accountEpoch"),
        QStringLiteral("type"),
        QStringLiteral("requestId"),
    };
    if (object.contains(QStringLiteral("accountUserId"))) {
        expected.insert(QStringLiteral("accountUserId"));
    }
    for (const auto& field : fields) {
        expected.insert(field);
    }
    const auto keys = object.keys();
    return QSet<QString>(keys.cbegin(), keys.cend()) == expected;
}

bool canonicalUuidV7(const QString& value)
{
    const QUuid id(value);
    return !id.isNull() && id.version() == QUuid::UnixEpoch
        && id.toString(QUuid::WithoutBraces) == value;
}

bool canonicalUuid(const QString& value)
{
    const QUuid id(value);
    return !id.isNull()
        && id.toString(QUuid::WithoutBraces) == value;
}

std::optional<quint64> exactUnsigned(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < 0
        || number > static_cast<double>(maximumExactJsonInteger)
        || std::floor(number) != number) {
        return std::nullopt;
    }
    return static_cast<quint64>(number);
}

bool validWorkingDirectory(const QString& path)
{
    return boundedText(path, identifierLimit, false)
        && QDir::isAbsolutePath(path)
        && QDir::cleanPath(path) == path;
}

bool validTimestamp(const QString& timestamp)
{
    if (timestamp.isEmpty()) {
        return true;
    }
    if (!boundedText(timestamp, timestampLimit, false)
        || !timestamp.contains(QLatin1Char('T'))) {
        return false;
    }
    auto parsed = QDateTime::fromString(timestamp, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(timestamp, Qt::ISODate);
    }
    return parsed.isValid() && parsed.timeSpec() != Qt::LocalTime;
}

QString itemIdentityKey(
    const QString& provider,
    const QString& nativeConversationId)
{
    return provider + QChar::Null + nativeConversationId;
}

} // namespace

ProviderConversationPreviewModel::ProviderConversationPreviewModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ProviderConversationPreviewModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant ProviderConversationPreviewModel::data(
    const QModelIndex& index,
    const int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= m_entries.size()) {
        return {};
    }
    const auto& entry = m_entries.at(index.row());
    switch (role) {
    case RoleRole: return entry.role;
    case ContentRole: return entry.content;
    case ToolNameRole: return entry.toolName;
    case TimestampRole: return entry.timestamp;
    default: return {};
    }
}

QHash<int, QByteArray> ProviderConversationPreviewModel::roleNames() const
{
    return {
        {RoleRole, QByteArrayLiteral("role")},
        {ContentRole, QByteArrayLiteral("content")},
        {ToolNameRole, QByteArrayLiteral("toolName")},
        {TimestampRole, QByteArrayLiteral("timestamp")},
    };
}

void ProviderConversationPreviewModel::replace(QVector<Entry> entries)
{
    beginResetModel();
    const auto countChanged = entries.size() != m_entries.size();
    m_entries = std::move(entries);
    endResetModel();
    if (countChanged) {
        emit this->countChanged();
    }
}

ProviderConversationsModel::ProviderConversationsModel(
    CommandDispatcher& dispatcher,
    DesktopFileIntegration& desktopFiles,
    DesktopSettings& settings,
    const qint64 replyTimeoutMs,
    QObject* parent)
    : QAbstractListModel(parent)
    , m_dispatcher(dispatcher)
    , m_desktopFiles(desktopFiles)
    , m_settings(settings)
    , m_preview(this)
    , m_replyTimeoutMs(replyTimeoutMs)
{
    Q_ASSERT(replyTimeoutMs >= 0);
    Q_ASSERT(replyTimeoutMs <= std::numeric_limits<int>::max());
    m_catalogReplyTimer.setSingleShot(true);
    m_previewReplyTimer.setSingleShot(true);
    connect(&m_catalogReplyTimer, &QTimer::timeout, this, [this] {
        if (!m_catalogPending
            || !catalogPendingCurrent(*m_catalogPending)) {
            return;
        }
        catalogFailure(
            m_catalogPending->operation,
            translated("The runtime did not reply in time."),
            false);
    });
    connect(&m_previewReplyTimer, &QTimer::timeout, this, [this] {
        if (!m_previewPending
            || !previewPendingCurrent(*m_previewPending)) {
            return;
        }
        previewFailure(
            translated("The runtime did not reply in time."),
            false);
    });
    connect(
        &desktopFiles,
        &DesktopFileIntegration::directoryPicked,
        this,
        [this](
            const QString& requestId,
            const DesktopFileIntegration::Purpose purpose,
            const QString& directory) {
            handleFolderPicked(
                requestId,
                static_cast<int>(purpose),
                directory);
        });
    connect(
        &desktopFiles,
        &DesktopFileIntegration::directoryPickCancelled,
        this,
        [this](
            const QString& requestId,
            const DesktopFileIntegration::Purpose purpose) {
            handleFolderCancelled(requestId, static_cast<int>(purpose));
        });
    connect(
        &desktopFiles,
        &DesktopFileIntegration::operationFailed,
        this,
        [this](
            const QString& requestId,
            const DesktopFileIntegration::Purpose purpose,
            DesktopFileIntegration::ErrorCode,
            const QString& message) {
            handleFolderFailure(
                requestId,
                static_cast<int>(purpose),
                message);
        });
}

int ProviderConversationsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_visibleRows.size();
}

int ProviderConversationsModel::totalCount() const noexcept
{
    return static_cast<int>(m_items.size());
}

QVariant ProviderConversationsModel::data(
    const QModelIndex& index,
    const int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= m_visibleRows.size()) {
        return {};
    }
    const auto& item = m_items.at(m_visibleRows.at(index.row()));
    switch (role) {
    case PresentationIdRole: return item.presentationId;
    case TitleRole:
        return item.title.isEmpty()
            ? translated("Untitled conversation")
            : item.title;
    case DateRole:
        return !item.updatedAt.isEmpty() ? item.updatedAt : item.createdAt;
    case ProviderLabelRole:
        return item.provider == QStringLiteral("claude")
            ? translated("Claude")
            : translated("Copilot");
    case SummaryRole: return QString {};
    case AccessibleIdRole: return item.accessibleId;
    default: return {};
    }
}

QHash<int, QByteArray> ProviderConversationsModel::roleNames() const
{
    return {
        {PresentationIdRole, QByteArrayLiteral("presentationId")},
        {TitleRole, QByteArrayLiteral("title")},
        {DateRole, QByteArrayLiteral("date")},
        {ProviderLabelRole, QByteArrayLiteral("providerLabel")},
        {SummaryRole, QByteArrayLiteral("summary")},
        {AccessibleIdRole, QByteArrayLiteral("accessibleId")},
    };
}

ProviderConversationsModel::State
ProviderConversationsModel::state() const noexcept
{
    return m_state;
}

ProviderConversationsModel::Provider
ProviderConversationsModel::provider() const noexcept
{
    return m_provider;
}

void ProviderConversationsModel::setProvider(const Provider provider)
{
    if (provider == m_provider) {
        return;
    }
    m_provider = provider;
    ++m_demandGeneration;
    invalidateDemand();
    emit stateChanged();
    if (m_demanded && m_hasAccountContext && !m_workingDirectory.isEmpty()) {
        (void)beginCatalog(CatalogOperation::Initial, {});
    }
}

QString ProviderConversationsModel::providerLabel() const
{
    return providerDisplay(m_provider);
}

QString ProviderConversationsModel::workingDirectoryLabel() const
{
    return folderDisplay(m_workingDirectory);
}

QString ProviderConversationsModel::searchText() const
{
    return m_searchText;
}

void ProviderConversationsModel::setSearchText(const QString& searchText)
{
    const auto bounded = searchText.left(256);
    if (m_searchText == bounded) {
        return;
    }
    m_searchText = bounded;
    rebuildFilter();
    emit stateChanged();
    emit selectionChanged();
}

QString ProviderConversationsModel::error() const { return m_error; }
QString ProviderConversationsModel::loadMoreError() const
{
    return m_loadMoreError;
}
bool ProviderConversationsModel::loading() const noexcept
{
    return m_state == State::Loading;
}
bool ProviderConversationsModel::loadingMore() const noexcept
{
    return m_loadingMore;
}
bool ProviderConversationsModel::hasMore() const noexcept { return m_hasMore; }
bool ProviderConversationsModel::capped() const noexcept { return m_capped; }
QString ProviderConversationsModel::cappedNotice() const
{
    return m_capped
        ? translated("Showing the first 500 conversations.")
        : QString {};
}
bool ProviderConversationsModel::browsing() const noexcept
{
    return !m_folderRequestId.isEmpty();
}
QString ProviderConversationsModel::selectedPresentationId() const
{
    return m_selectedPresentationId;
}
QString ProviderConversationsModel::selectedTitle() const
{
    const auto* item = selectedItem();
    return item
        ? (item->title.isEmpty()
                ? translated("Untitled conversation")
                : item->title)
        : QString {};
}
QString ProviderConversationsModel::selectedDate() const
{
    const auto* item = selectedItem();
    return item
        ? (!item->updatedAt.isEmpty() ? item->updatedAt : item->createdAt)
        : QString {};
}
QString ProviderConversationsModel::selectedProviderLabel() const
{
    const auto* item = selectedItem();
    return item
        ? (item->provider == QStringLiteral("claude")
                ? translated("Claude")
                : translated("Copilot"))
        : QString {};
}
QString ProviderConversationsModel::selectedSummary() const
{
    return {};
}
bool ProviderConversationsModel::canResume() const
{
    return selectedItem() != nullptr && selectedVisible()
        && m_hasAccountContext
        && m_state == State::Ready && !m_workingDirectory.isEmpty();
}
bool ProviderConversationsModel::previewLoading() const noexcept
{
    return m_previewLoading;
}
QString ProviderConversationsModel::previewError() const
{
    return m_previewError;
}
QString ProviderConversationsModel::previewNotice() const
{
    return m_previewNotice;
}
ProviderConversationPreviewModel*
ProviderConversationsModel::preview() noexcept
{
    return &m_preview;
}

bool ProviderConversationsModel::open()
{
    m_demanded = true;
    if (m_workingDirectory.isEmpty()) {
        const auto validation = DesktopFileIntegration::validateDirectory(
            m_settings.effectiveWorkingDirectory());
        if (!validation.valid()) {
            m_state = State::Failed;
            m_error = translated(
                "Choose a readable project folder before loading conversations.");
            emit stateChanged();
            return false;
        }
        setWorkingDirectory(validation.canonicalPath);
    }
    if (!m_hasAccountContext) {
        m_state = State::Failed;
        m_error = translated(
            "Provider conversations require an active runtime account context.");
        emit stateChanged();
        return false;
    }
    if (m_state == State::Ready || m_catalogPending) {
        return true;
    }
    return beginCatalog(CatalogOperation::Initial, {});
}

void ProviderConversationsModel::close()
{
    m_demanded = false;
    ++m_demandGeneration;
    if (!m_folderRequestId.isEmpty()) {
        (void)cancelFolderBrowse();
        m_folderRequestId.clear();
    }
    invalidateDemand();
    m_state = State::Dormant;
    m_error.clear();
    m_loadMoreError.clear();
    emit stateChanged();
}

bool ProviderConversationsModel::reload()
{
    if (!m_demanded || !m_hasAccountContext
        || m_workingDirectory.isEmpty()) {
        return false;
    }
    ++m_demandGeneration;
    invalidateDemand();
    return beginCatalog(CatalogOperation::Initial, {});
}

bool ProviderConversationsModel::retry()
{
    if (m_catalogPending || m_state != State::Failed) {
        return false;
    }
    return beginCatalog(CatalogOperation::Initial, {});
}

bool ProviderConversationsModel::loadMore()
{
    if (!m_hasMore || m_nextCursor.isEmpty() || m_loadingMore
        || m_catalogPending || m_capped) {
        return false;
    }
    return beginCatalog(CatalogOperation::More, m_nextCursor);
}

bool ProviderConversationsModel::retryLoadMore()
{
    if (m_catalogPending || m_failedCatalogOperation != CatalogOperation::More
        || m_failedCursor.isEmpty()) {
        return false;
    }
    return beginCatalog(CatalogOperation::More, m_failedCursor);
}

bool ProviderConversationsModel::select(const QString& presentationId)
{
    if (!presentationId.isEmpty()
        && itemForPresentationId(presentationId) == nullptr) {
        return false;
    }
    ++m_selectionGeneration;
    m_selectedPresentationId = presentationId;
    clearPreview();
    emit selectionChanged();
    const auto* item = selectedItem();
    return item == nullptr || beginPreview(*item);
}

bool ProviderConversationsModel::retryPreview()
{
    if (m_previewPending || m_previewError.isEmpty()) {
        return false;
    }
    const auto* item = selectedItem();
    return item != nullptr && beginPreview(*item);
}

bool ProviderConversationsModel::browseFolder()
{
    if (!m_folderRequestId.isEmpty()) {
        return true;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    m_folderRequestId = requestId;
    m_error.clear();
    emit stateChanged();
    if (!m_desktopFiles.requestDirectory(
            requestId,
            DesktopFileIntegration::Purpose::ResumeAgentWork,
            m_workingDirectory)) {
        if (m_folderRequestId == requestId) {
            m_folderRequestId.clear();
            emit stateChanged();
        }
        return false;
    }
    return true;
}

bool ProviderConversationsModel::cancelFolderBrowse()
{
    if (m_folderRequestId.isEmpty()) {
        return false;
    }
    return m_desktopFiles.cancelDirectory(
        m_folderRequestId,
        DesktopFileIntegration::Purpose::ResumeAgentWork);
}

std::optional<ProviderConversationResumeTarget>
ProviderConversationsModel::resolveResumeTarget(
    const QString& presentationId) const
{
    if (!canResume() || presentationId != m_selectedPresentationId) {
        return std::nullopt;
    }
    const auto* item = selectedItem();
    if (item == nullptr || item->workingDirectory != m_workingDirectory
        || item->provider != providerWire(m_provider)
        || !validWorkingDirectory(item->workingDirectory)
        || !canonicalUuid(item->nativeConversationId)) {
        return std::nullopt;
    }
    const auto validation =
        DesktopFileIntegration::validateDirectory(item->workingDirectory);
    if (!validation.valid()
        || validation.canonicalPath != item->workingDirectory) {
        return std::nullopt;
    }
    return ProviderConversationResumeTarget {
        .provider = item->provider,
        .nativeConversationId = item->nativeConversationId,
        .workingDirectory = item->workingDirectory,
        .title = item->title,
        .accountUserId = m_accountUserId,
        .accountEpoch = m_accountEpoch,
    };
}

void ProviderConversationsModel::ingestAuthEvent(QByteArray json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }
    const auto object = document.object();
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("auth.ready")
        && type != QStringLiteral("auth.required")) {
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    QString userId;
    if (!epoch || (type == QStringLiteral("auth.ready")
            && !nullableAccount(object, QStringLiteral("userId"), userId))) {
        emit decodeError(translated("Authentication context is malformed."));
        return;
    }
    activateAccount(std::move(userId), *epoch);
}

void ProviderConversationsModel::ingestAgentIntelEvent(QByteArray json)
{
    if (json.size() > 8 * 1024 * 1024) {
        emit decodeError(translated(
            "Provider conversation reply exceeds the ABI frame limit."));
        return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        emit decodeError(translated(
            "Provider conversation event is not valid JSON."));
        return;
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("authority")).toString()
        != QStringLiteral("accountContext")) {
        return;
    }
    const auto epoch =
        exactUnsignedJsonField(json, QByteArrayLiteral("accountEpoch"));
    QString accountUserId;
    if (!epoch
        || !nullableAccount(
            object,
            QStringLiteral("accountUserId"),
            accountUserId)) {
        emit decodeError(translated(
            "Provider conversation authority is malformed."));
        return;
    }
    const auto admission = m_accountFence.admit(
        {.userId = std::move(accountUserId), .epoch = *epoch},
        json);
    if (admission == AccountEventAdmission::Oversized) {
        emit decodeError(translated(
            "Future provider conversation reply exceeds the ABI frame limit."));
    } else if (admission == AccountEventAdmission::Current) {
        applyAgentIntelEvent(json);
    }
}

void ProviderConversationsModel::resetRuntimeAuthority()
{
    ++m_runtimeGeneration;
    ++m_demandGeneration;
    ++m_folderGeneration;
    ++m_selectionGeneration;
    m_accountFence.reset();
    m_hasAccountContext = false;
    m_accountUserId.clear();
    m_accountEpoch = 0;
    if (!m_folderRequestId.isEmpty()) {
        (void)cancelFolderBrowse();
        m_folderRequestId.clear();
    }
    invalidateDemand(
        translated("The runtime restarted. Reload provider conversations."));
    m_state = m_demanded ? State::Failed : State::Dormant;
    emit stateChanged();
}

void ProviderConversationsModel::activateAccount(
    QString userId,
    const quint64 epoch)
{
    auto activation = m_accountFence.activate({
        .userId = userId,
        .epoch = epoch,
    });
    if (!activation.accepted) {
        return;
    }
    m_hasAccountContext = true;
    if (activation.changed) {
        m_accountUserId = std::move(userId);
        m_accountEpoch = epoch;
        ++m_demandGeneration;
        ++m_selectionGeneration;
        invalidateDemand();
        if (m_demanded && !m_workingDirectory.isEmpty()) {
            (void)beginCatalog(CatalogOperation::Initial, {});
        }
    }
    for (auto& pending : activation.pendingEvents) {
        ingestAgentIntelEvent(std::move(pending));
    }
}

void ProviderConversationsModel::applyAgentIntelEvent(
    const QByteArray& json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }
    const auto object = document.object();
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("agent.intel.reply")
        && type != QStringLiteral("agent.intel.error")) {
        return;
    }
    const auto requestId =
        requiredString(object, QStringLiteral("requestId"));
    if (!requestId || !canonicalUuidV7(*requestId)) {
        return;
    }
    const auto catalogMatch = m_catalogPending
        && m_catalogPending->requestId == *requestId
        && catalogPendingCurrent(*m_catalogPending);
    const auto previewMatch = m_previewPending
        && m_previewPending->requestId == *requestId
        && previewPendingCurrent(*m_previewPending);
    if (!catalogMatch && !previewMatch) {
        return;
    }
    const auto validEnvelope =
        type == QStringLiteral("agent.intel.reply")
        ? exactAccountEnvelope(object, {QStringLiteral("payload")})
        : exactAccountEnvelope(
            object,
            {
                QStringLiteral("message"),
                QStringLiteral("failureKind"),
                QStringLiteral("mutationId"),
                QStringLiteral("reconciliationRequired"),
            });
    if (!validEnvelope) {
        if (catalogMatch) {
            catalogFailure(
                m_catalogPending->operation,
                translated("The runtime returned a malformed conversation catalog reply."),
                true);
        } else {
            previewFailure(
                translated("The runtime returned a malformed conversation preview reply."),
                true);
        }
        return;
    }
    if (type == QStringLiteral("agent.intel.error")) {
        handleRuntimeError(object, *requestId);
        return;
    }
    const auto payload = object.value(QStringLiteral("payload"));
    if (!payload.isObject()) {
        if (catalogMatch) {
            catalogFailure(
                m_catalogPending->operation,
                translated("The runtime returned a malformed conversation catalog reply."),
                true);
        } else {
            previewFailure(
                translated("The runtime returned a malformed conversation preview reply."),
                true);
        }
        return;
    }
    if (catalogMatch) {
        handleCatalogReply(payload.toObject(), *m_catalogPending);
    } else {
        handlePreviewReply(payload.toObject(), *m_previewPending);
    }
}

void ProviderConversationsModel::handleCatalogReply(
    const QJsonObject& payload,
    const CatalogPending& pending)
{
    const auto reject = [this, &pending] {
        catalogFailure(
            pending.operation,
            translated("The runtime returned an invalid conversation catalog page."),
            true);
    };
    if (!exactFields(
            payload,
            {
                QStringLiteral("items"),
                QStringLiteral("nextCursor"),
                QStringLiteral("hasMore"),
                QStringLiteral("responseBytes"),
            })) {
        reject();
        return;
    }
    const auto itemsValue = payload.value(QStringLiteral("items"));
    const auto cursorValue = payload.value(QStringLiteral("nextCursor"));
    const auto hasMoreValue = payload.value(QStringLiteral("hasMore"));
    const auto responseBytes =
        exactUnsigned(payload.value(QStringLiteral("responseBytes")));
    if (!itemsValue.isArray()
        || itemsValue.toArray().size() > pageItemLimit
        || !hasMoreValue.isBool() || !responseBytes
        || *responseBytes > discoveryByteLimit
        || QJsonDocument(payload).toJson(QJsonDocument::Compact).size()
            > discoveryByteLimit + 4'096) {
        reject();
        return;
    }
    QString nextCursor;
    if (cursorValue.isNull()) {
        nextCursor.clear();
    } else if (cursorValue.isString()
        && boundedText(cursorValue.toString(), cursorLimit, false)) {
        nextCursor = cursorValue.toString();
    } else {
        reject();
        return;
    }
    const auto hasMore = hasMoreValue.toBool();
    if ((hasMore && (nextCursor.isEmpty() || itemsValue.toArray().isEmpty()))
        || (!hasMore && !nextCursor.isEmpty())
        || (!nextCursor.isEmpty()
            && (nextCursor == pending.cursor
                || m_seenCursors.contains(nextCursor)))) {
        reject();
        return;
    }

    QVector<Item> decoded;
    decoded.reserve(itemsValue.toArray().size());
    qsizetype calculatedResponseBytes = 0;
    QSet<QString> pageIdentities;
    for (const auto& itemValue : itemsValue.toArray()) {
        if (!itemValue.isObject()) {
            reject();
            return;
        }
        const auto item = itemValue.toObject();
        calculatedResponseBytes +=
            QJsonDocument(item).toJson(QJsonDocument::Compact).size();
        if (!exactFields(
                item,
                {
                    QStringLiteral("provider"),
                    QStringLiteral("nativeConversationId"),
                    QStringLiteral("workingDirectory"),
                    QStringLiteral("title"),
                    QStringLiteral("createdAt"),
                    QStringLiteral("updatedAt"),
                })) {
            reject();
            return;
        }
        const auto provider =
            requiredString(item, QStringLiteral("provider"), 16);
        const auto nativeId =
            requiredString(item, QStringLiteral("nativeConversationId"), 64);
        const auto workingDirectory =
            requiredString(item, QStringLiteral("workingDirectory"));
        QString title;
        QString createdAt;
        QString updatedAt;
        if (!provider || *provider != pending.provider
            || !nativeId || !canonicalUuid(*nativeId)
            || !workingDirectory
            || *workingDirectory != pending.workingDirectory
            || !validWorkingDirectory(*workingDirectory)
            || !nullableString(
                item,
                QStringLiteral("title"),
                title,
                titleLimit,
                true)
            || !nullableString(
                item,
                QStringLiteral("createdAt"),
                createdAt,
                timestampLimit,
                false)
            || !nullableString(
                item,
                QStringLiteral("updatedAt"),
                updatedAt,
                timestampLimit,
                false)
            || !validTimestamp(createdAt) || !validTimestamp(updatedAt)) {
            reject();
            return;
        }
        const auto identity = itemIdentityKey(*provider, *nativeId);
        if (pageIdentities.contains(identity)) {
            continue;
        }
        pageIdentities.insert(identity);
        const auto presentationId = stablePresentationId(identity);
        decoded.push_back({
            .presentationId = presentationId,
            .identityKey = identity,
            .provider = *provider,
            .nativeConversationId = *nativeId,
            .workingDirectory = *workingDirectory,
            .title = std::move(title),
            .createdAt = std::move(createdAt),
            .updatedAt = std::move(updatedAt),
            .accessibleId = presentationId,
        });
    }
    if (calculatedResponseBytes != static_cast<qsizetype>(*responseBytes)) {
        reject();
        return;
    }

    QVector<Item> combined =
        pending.operation == CatalogOperation::Initial ? QVector<Item> {}
                                                       : m_items;
    QSet<QString> identities;
    for (const auto& item : std::as_const(combined)) {
        identities.insert(item.identityKey);
    }
    bool retentionOverflow = false;
    for (auto& item : decoded) {
        if (identities.contains(item.identityKey)) {
            continue;
        }
        identities.insert(item.identityKey);
        if (combined.size() >= retainedItemLimit) {
            retentionOverflow = true;
            continue;
        }
        combined.push_back(std::move(item));
    }
    m_catalogReplyTimer.stop();
    m_catalogPending.reset();
    m_failedCatalogOperation.reset();
    m_failedCursor.clear();
    m_loadingMore = false;
    m_items = std::move(combined);
    if (!nextCursor.isEmpty()) {
        m_seenCursors.insert(nextCursor);
    }
    m_nextCursor = nextCursor;
    m_capped = retentionOverflow
        || (hasMore && m_items.size() >= retainedItemLimit);
    m_hasMore = hasMore && !m_capped;
    m_state = State::Ready;
    m_error.clear();
    m_loadMoreError.clear();
    rebuildFilter();
    if (m_selectedPresentationId.isEmpty() && !m_items.isEmpty()) {
        (void)select(m_items.constFirst().presentationId);
    } else {
        emit selectionChanged();
    }
    emit stateChanged();
}

void ProviderConversationsModel::handlePreviewReply(
    const QJsonObject& payload,
    const PreviewPending& pending)
{
    const auto reject = [this] {
        previewFailure(
            translated("The runtime returned an invalid conversation preview."),
            true);
    };
    if (!exactFields(
            payload,
            {
                QStringLiteral("entries"),
                QStringLiteral("nextBeforeByte"),
                QStringLiteral("sourceFileBytes"),
                QStringLiteral("readBytes"),
                QStringLiteral("sourceRecords"),
                QStringLiteral("degradedReason"),
            })) {
        reject();
        return;
    }
    const auto entriesValue = payload.value(QStringLiteral("entries"));
    const auto sourceFileBytes =
        exactUnsigned(payload.value(QStringLiteral("sourceFileBytes")));
    const auto readBytes =
        exactUnsigned(payload.value(QStringLiteral("readBytes")));
    const auto sourceRecords =
        exactUnsigned(payload.value(QStringLiteral("sourceRecords")));
    const auto nextBeforeValue =
        payload.value(QStringLiteral("nextBeforeByte"));
    std::optional<quint64> nextBeforeByte;
    if (nextBeforeValue.isNull()) {
        nextBeforeByte.reset();
    } else {
        nextBeforeByte = exactUnsigned(nextBeforeValue);
    }
    QString degradedReason;
    if (!entriesValue.isArray()
        || entriesValue.toArray().size() > 500
        || !sourceFileBytes || !readBytes || !sourceRecords
        || *readBytes > previewByteLimit
        || *sourceRecords > previewRecordLimit
        || (nextBeforeValue.isNull() == false && !nextBeforeByte)
        || (nextBeforeByte && *nextBeforeByte > *sourceFileBytes)
        || !nullableString(
            payload,
            QStringLiteral("degradedReason"),
            degradedReason,
            noticeLimit,
            false)
        || QJsonDocument(payload).toJson(QJsonDocument::Compact).size()
            > previewByteLimit + 8'192) {
        reject();
        return;
    }
    QVector<ProviderConversationPreviewModel::Entry> decoded;
    decoded.reserve(entriesValue.toArray().size());
    qsizetype textBytes = 0;
    for (const auto& entryValue : entriesValue.toArray()) {
        if (!entryValue.isObject()) {
            reject();
            return;
        }
        const auto entry = entryValue.toObject();
        if (!exactFields(
                entry,
                {
                    QStringLiteral("role"),
                    QStringLiteral("content"),
                    QStringLiteral("toolName"),
                    QStringLiteral("timestamp"),
                })) {
            reject();
            return;
        }
        const auto role =
            requiredString(entry, QStringLiteral("role"), roleLimit);
        const auto content =
            requiredString(
                entry,
                QStringLiteral("content"),
                previewByteLimit,
                true);
        QString toolName;
        QString timestamp;
        if (!role || !content
            || !nullableString(
                entry,
                QStringLiteral("toolName"),
                toolName,
                toolNameLimit,
                false)
            || !nullableString(
                entry,
                QStringLiteral("timestamp"),
                timestamp,
                timestampLimit,
                false)
            || !validTimestamp(timestamp)) {
            reject();
            return;
        }
        textBytes += content->toUtf8().size();
        if (textBytes > previewByteLimit) {
            reject();
            return;
        }
        decoded.push_back({
            .role = *role,
            .content = *content,
            .toolName = std::move(toolName),
            .timestamp = std::move(timestamp),
        });
    }
    const auto locallyTrimmed =
        decoded.size() > previewRetainedLimit;
    if (locallyTrimmed) {
        decoded = decoded.sliced(
            decoded.size() - previewRetainedLimit,
            previewRetainedLimit);
    }
    if (!previewPendingCurrent(pending)) {
        return;
    }
    m_previewReplyTimer.stop();
    m_previewPending.reset();
    m_preview.replace(std::move(decoded));
    m_previewLoading = false;
    m_previewError.clear();
    m_previewNotice = !degradedReason.isEmpty()
        ? degradedReason
        : nextBeforeByte || locallyTrimmed
        ? translated(
            "Showing the latest entries. Earlier turns remain with the provider.")
        : QString {};
    emit selectionChanged();
}

void ProviderConversationsModel::handleRuntimeError(
    const QJsonObject& object,
    const QString& requestId)
{
    const auto message =
        requiredString(object, QStringLiteral("message"), noticeLimit);
    const auto failureKind =
        requiredString(object, QStringLiteral("failureKind"), 64);
    const auto mutation = object.value(QStringLiteral("mutationId"));
    const auto reconciliation =
        object.value(QStringLiteral("reconciliationRequired"));
    if (!message || !failureKind
        || (!mutation.isNull() && !mutation.isString())
        || !reconciliation.isBool()) {
        if (m_catalogPending
            && m_catalogPending->requestId == requestId) {
            catalogFailure(
                m_catalogPending->operation,
                translated("The runtime returned a malformed conversation error."),
                true);
        } else {
            previewFailure(
                translated("The runtime returned a malformed preview error."),
                true);
        }
        return;
    }
    if (m_catalogPending && m_catalogPending->requestId == requestId) {
        catalogFailure(
            m_catalogPending->operation,
            m_catalogPending->operation == CatalogOperation::Initial
                ? translated("Could not read provider conversations.")
                : translated("Could not load more provider conversations."),
            false);
    } else if (m_previewPending
        && m_previewPending->requestId == requestId) {
        previewFailure(translated("Could not load the conversation preview."), false);
    }
}

bool ProviderConversationsModel::beginCatalog(
    const CatalogOperation operation,
    const QString& cursor)
{
    if (!m_hasAccountContext || !m_demanded || m_workingDirectory.isEmpty()
        || m_catalogPending) {
        return false;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.discoverProviderConversations")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("provider"), providerWire(m_provider)},
        {QStringLiteral("workingDirectory"), m_workingDirectory},
        {QStringLiteral("limit"), static_cast<qint64>(pageItemLimit)},
        {QStringLiteral("maxBytes"),
         static_cast<qint64>(discoveryByteLimit)},
    };
    if (!cursor.isEmpty()) {
        command.insert(QStringLiteral("cursor"), cursor);
    }
    if (operation == CatalogOperation::Initial) {
        m_state = State::Loading;
        m_error.clear();
        m_seenCursors.clear();
    } else {
        m_loadingMore = true;
        m_loadMoreError.clear();
    }
    emit stateChanged();
    return sendCatalog(
        std::move(command),
        {
            .requestId = requestId,
            .cursor = cursor,
            .operation = operation,
            .runtimeGeneration = m_runtimeGeneration,
            .demandGeneration = m_demandGeneration,
            .folderGeneration = m_folderGeneration,
            .provider = providerWire(m_provider),
            .workingDirectory = m_workingDirectory,
        });
}

bool ProviderConversationsModel::beginPreview(const Item& item)
{
    if (m_previewPending || item.presentationId != m_selectedPresentationId
        || item.workingDirectory != m_workingDirectory
        || item.provider != providerWire(m_provider)) {
        return false;
    }
    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QJsonObject command {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.readSessionConversation")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("agent"), item.provider},
        {QStringLiteral("cwd"), item.workingDirectory},
        {QStringLiteral("sessionId"), item.nativeConversationId},
        {QStringLiteral("maxRecords"),
         static_cast<qint64>(previewRecordLimit)},
        {QStringLiteral("maxBytes"),
         static_cast<qint64>(previewByteLimit)},
    };
    m_previewLoading = true;
    m_previewError.clear();
    m_previewNotice.clear();
    emit selectionChanged();
    return sendPreview(
        std::move(command),
        {
            .requestId = requestId,
            .runtimeGeneration = m_runtimeGeneration,
            .demandGeneration = m_demandGeneration,
            .folderGeneration = m_folderGeneration,
            .selectionGeneration = m_selectionGeneration,
            .presentationId = item.presentationId,
            .identityKey = item.identityKey,
            .provider = item.provider,
            .workingDirectory = item.workingDirectory,
            .nativeConversationId = item.nativeConversationId,
        });
}

bool ProviderConversationsModel::sendCatalog(
    QJsonObject command,
    CatalogPending pending)
{
    Q_ASSERT(!m_catalogPending);
    m_catalogPending = pending;
    const auto json =
        QJsonDocument(std::move(command)).toJson(QJsonDocument::Compact);
    if (m_dispatcher.send(CommandLane::AgentIntel, json)) {
        m_catalogReplyTimer.start(static_cast<int>(m_replyTimeoutMs));
        return true;
    }
    if (m_catalogPending
        && m_catalogPending->requestId == pending.requestId) {
        catalogFailure(
            pending.operation,
            translated("The runtime did not accept the conversation request."),
            false);
    }
    return false;
}

bool ProviderConversationsModel::sendPreview(
    QJsonObject command,
    PreviewPending pending)
{
    Q_ASSERT(!m_previewPending);
    m_previewPending = pending;
    const auto json =
        QJsonDocument(std::move(command)).toJson(QJsonDocument::Compact);
    if (m_dispatcher.send(CommandLane::AgentIntel, json)) {
        m_previewReplyTimer.start(static_cast<int>(m_replyTimeoutMs));
        return true;
    }
    if (m_previewPending
        && m_previewPending->requestId == pending.requestId) {
        previewFailure(
            translated("The runtime did not accept the preview request."),
            false);
    }
    return false;
}

bool ProviderConversationsModel::catalogPendingCurrent(
    const CatalogPending& pending) const
{
    return pending.runtimeGeneration == m_runtimeGeneration
        && pending.demandGeneration == m_demandGeneration
        && pending.folderGeneration == m_folderGeneration
        && pending.provider == providerWire(m_provider)
        && pending.workingDirectory == m_workingDirectory
        && m_hasAccountContext && m_demanded;
}

bool ProviderConversationsModel::previewPendingCurrent(
    const PreviewPending& pending) const
{
    if (pending.runtimeGeneration != m_runtimeGeneration
        || pending.demandGeneration != m_demandGeneration
        || pending.folderGeneration != m_folderGeneration
        || pending.selectionGeneration != m_selectionGeneration
        || pending.presentationId != m_selectedPresentationId
        || pending.provider != providerWire(m_provider)
        || pending.workingDirectory != m_workingDirectory) {
        return false;
    }
    const auto* item = selectedItem();
    return item && item->identityKey == pending.identityKey
        && item->nativeConversationId == pending.nativeConversationId;
}

void ProviderConversationsModel::catalogFailure(
    const CatalogOperation operation,
    QString message,
    const bool protocolFault)
{
    if (protocolFault) {
        emit decodeError(message);
    }
    m_catalogReplyTimer.stop();
    if (m_catalogPending) {
        m_failedCursor = m_catalogPending->cursor;
    }
    m_catalogPending.reset();
    m_failedCatalogOperation = operation;
    m_loadingMore = false;
    if (operation == CatalogOperation::Initial) {
        clearCatalog();
        m_state = State::Failed;
        m_error = std::move(message);
    } else {
        m_loadMoreError = std::move(message);
    }
    emit stateChanged();
}

void ProviderConversationsModel::previewFailure(
    QString message,
    const bool protocolFault)
{
    if (protocolFault) {
        emit decodeError(message);
    }
    m_previewReplyTimer.stop();
    m_previewPending.reset();
    m_previewLoading = false;
    m_previewError = std::move(message);
    m_previewNotice.clear();
    m_preview.replace({});
    emit selectionChanged();
}

void ProviderConversationsModel::invalidateDemand(QString message)
{
    m_catalogReplyTimer.stop();
    m_previewReplyTimer.stop();
    m_catalogPending.reset();
    m_previewPending.reset();
    m_failedCatalogOperation.reset();
    m_failedCursor.clear();
    clearCatalog();
    clearPreview();
    m_loadingMore = false;
    m_state = State::Dormant;
    m_error = std::move(message);
    emit selectionChanged();
}

void ProviderConversationsModel::clearCatalog()
{
    const auto hadSelection = !m_selectedPresentationId.isEmpty();
    beginResetModel();
    const auto hadRows = !m_visibleRows.isEmpty();
    m_items.clear();
    m_visibleRows.clear();
    endResetModel();
    if (hadRows) {
        emit countChanged();
    }
    m_nextCursor.clear();
    m_seenCursors.clear();
    m_hasMore = false;
    m_capped = false;
    m_loadMoreError.clear();
    m_selectedPresentationId.clear();
    if (hadSelection) {
        emit selectionChanged();
    }
}

void ProviderConversationsModel::clearPreview()
{
    m_previewReplyTimer.stop();
    m_previewPending.reset();
    m_preview.replace({});
    m_previewLoading = false;
    m_previewError.clear();
    m_previewNotice.clear();
}

void ProviderConversationsModel::rebuildFilter()
{
    const auto query = m_searchText.trimmed();
    QVector<qsizetype> visible;
    visible.reserve(m_items.size());
    for (qsizetype index = 0; index < m_items.size(); ++index) {
        const auto& item = m_items.at(index);
        const auto date =
            !item.updatedAt.isEmpty() ? item.updatedAt : item.createdAt;
        const auto matches = query.isEmpty()
            || item.title.contains(query, Qt::CaseInsensitive)
            || date.contains(query, Qt::CaseInsensitive)
            || (item.provider == QStringLiteral("claude")
                    ? QStringLiteral("Claude")
                    : QStringLiteral("Copilot"))
                   .contains(query, Qt::CaseInsensitive);
        if (matches) {
            visible.push_back(index);
        }
    }
    beginResetModel();
    const auto changed = visible.size() != m_visibleRows.size();
    m_visibleRows = std::move(visible);
    endResetModel();
    if (changed) {
        emit countChanged();
    }
}

void ProviderConversationsModel::setWorkingDirectory(QString directory)
{
    if (directory == m_workingDirectory) {
        return;
    }
    m_workingDirectory = std::move(directory);
    ++m_folderGeneration;
    ++m_demandGeneration;
    ++m_selectionGeneration;
    invalidateDemand();
    emit stateChanged();
    emit selectionChanged();
    if (m_demanded && m_hasAccountContext) {
        (void)beginCatalog(CatalogOperation::Initial, {});
    }
}

void ProviderConversationsModel::handleFolderPicked(
    const QString& requestId,
    const int purpose,
    const QString& canonicalDirectory)
{
    if (requestId != m_folderRequestId
        || purpose
            != static_cast<int>(
                DesktopFileIntegration::Purpose::ResumeAgentWork)) {
        return;
    }
    m_folderRequestId.clear();
    emit stateChanged();
    setWorkingDirectory(canonicalDirectory);
}

void ProviderConversationsModel::handleFolderCancelled(
    const QString& requestId,
    const int purpose)
{
    if (requestId != m_folderRequestId
        || purpose
            != static_cast<int>(
                DesktopFileIntegration::Purpose::ResumeAgentWork)) {
        return;
    }
    m_folderRequestId.clear();
    emit stateChanged();
}

void ProviderConversationsModel::handleFolderFailure(
    const QString& requestId,
    const int purpose,
    const QString& message)
{
    if (requestId != m_folderRequestId
        || purpose
            != static_cast<int>(
                DesktopFileIntegration::Purpose::ResumeAgentWork)) {
        return;
    }
    m_folderRequestId.clear();
    m_error = message;
    if (m_items.isEmpty()) {
        m_state = State::Failed;
    }
    emit stateChanged();
}

QString ProviderConversationsModel::stablePresentationId(
    const QString& identityKey)
{
    QByteArray input("kodosi.provider-conversation.presentation.v1");
    input.append('\0');
    input.append(identityKey.toUtf8());
    return QStringLiteral("conversation-")
        + QString::fromLatin1(
            QCryptographicHash::hash(input, QCryptographicHash::Sha256)
                .toHex());
}

const ProviderConversationsModel::Item*
ProviderConversationsModel::itemForPresentationId(
    const QString& presentationId) const
{
    const auto found = std::ranges::find(
        m_items,
        presentationId,
        &Item::presentationId);
    return found == m_items.cend() ? nullptr : &*found;
}

const ProviderConversationsModel::Item*
ProviderConversationsModel::selectedItem() const
{
    return itemForPresentationId(m_selectedPresentationId);
}

bool ProviderConversationsModel::selectedVisible() const
{
    const auto* selected = selectedItem();
    if (selected == nullptr) {
        return false;
    }
    return std::ranges::any_of(
        m_visibleRows,
        [this, selected](const qsizetype row) {
            return &m_items.at(row) == selected;
        });
}

QString ProviderConversationsModel::providerWire(const Provider provider)
{
    return provider == Provider::Claude
        ? QStringLiteral("claude")
        : QStringLiteral("copilot");
}

QString ProviderConversationsModel::providerDisplay(const Provider provider)
{
    return provider == Provider::Claude
        ? translated("Claude")
        : translated("Copilot");
}

QString ProviderConversationsModel::folderDisplay(const QString& directory)
{
    if (directory.isEmpty()) {
        return translated("No project folder selected");
    }
    const QFileInfo info(directory);
    const auto name = info.fileName();
    return name.isEmpty() ? translated("Project folder") : name;
}

} // namespace kodosi
