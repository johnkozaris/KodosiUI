#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/AccountContextFence.hpp"
#include "models/ProviderConversationResumeResolver.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

#include <optional>

namespace kodosi {

class DesktopFileIntegration;
class DesktopSettings;

class ProviderConversationPreviewModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        RoleRole = Qt::UserRole + 1,
        ContentRole,
        ToolNameRole,
        TimestampRole,
    };
    Q_ENUM(Role)

    explicit ProviderConversationPreviewModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    friend class ProviderConversationsModel;
    struct Entry {
        QString role;
        QString content;
        QString toolName;
        QString timestamp;
    };
    QVector<Entry> m_entries;
    void replace(QVector<Entry> entries);
};

class ProviderConversationsModel final
    : public QAbstractListModel
    , public ProviderConversationResumeResolver {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY stateChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(Provider provider READ provider WRITE setProvider NOTIFY stateChanged)
    Q_PROPERTY(QString providerLabel READ providerLabel NOTIFY stateChanged)
    Q_PROPERTY(
        QString workingDirectoryLabel
        READ workingDirectoryLabel
        NOTIFY stateChanged)
    Q_PROPERTY(
        QString searchText
        READ searchText
        WRITE setSearchText
        NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString loadMoreError READ loadMoreError NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool loadingMore READ loadingMore NOTIFY stateChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY stateChanged)
    Q_PROPERTY(bool capped READ capped NOTIFY stateChanged)
    Q_PROPERTY(QString cappedNotice READ cappedNotice NOTIFY stateChanged)
    Q_PROPERTY(bool browsing READ browsing NOTIFY stateChanged)
    Q_PROPERTY(
        QString selectedPresentationId
        READ selectedPresentationId
        NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedTitle READ selectedTitle NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedDate READ selectedDate NOTIFY selectionChanged)
    Q_PROPERTY(
        QString selectedProviderLabel
        READ selectedProviderLabel
        NOTIFY selectionChanged)
    Q_PROPERTY(
        QString selectedSummary
        READ selectedSummary
        NOTIFY selectionChanged)
    Q_PROPERTY(bool canResume READ canResume NOTIFY selectionChanged)
    Q_PROPERTY(bool previewLoading READ previewLoading NOTIFY selectionChanged)
    Q_PROPERTY(QString previewError READ previewError NOTIFY selectionChanged)
    Q_PROPERTY(QString previewNotice READ previewNotice NOTIFY selectionChanged)
    Q_PROPERTY(
        kodosi::ProviderConversationPreviewModel* preview
        READ preview
        CONSTANT)

public:
    enum class State {
        Dormant,
        Loading,
        Ready,
        Failed,
    };
    Q_ENUM(State)

    enum class Provider {
        Claude,
        Copilot,
    };
    Q_ENUM(Provider)

    enum Role {
        PresentationIdRole = Qt::UserRole + 1,
        TitleRole,
        DateRole,
        ProviderLabelRole,
        SummaryRole,
        AccessibleIdRole,
    };
    Q_ENUM(Role)

    ProviderConversationsModel(
        CommandDispatcher& dispatcher,
        DesktopFileIntegration& desktopFiles,
        DesktopSettings& settings,
        qint64 replyTimeoutMs = 15'000,
        QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int totalCount() const noexcept;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] Provider provider() const noexcept;
    void setProvider(Provider provider);
    [[nodiscard]] QString providerLabel() const;
    [[nodiscard]] QString workingDirectoryLabel() const;
    [[nodiscard]] QString searchText() const;
    void setSearchText(const QString& searchText);
    [[nodiscard]] QString error() const;
    [[nodiscard]] QString loadMoreError() const;
    [[nodiscard]] bool loading() const noexcept;
    [[nodiscard]] bool loadingMore() const noexcept;
    [[nodiscard]] bool hasMore() const noexcept;
    [[nodiscard]] bool capped() const noexcept;
    [[nodiscard]] QString cappedNotice() const;
    [[nodiscard]] bool browsing() const noexcept;
    [[nodiscard]] QString selectedPresentationId() const;
    [[nodiscard]] QString selectedTitle() const;
    [[nodiscard]] QString selectedDate() const;
    [[nodiscard]] QString selectedProviderLabel() const;
    [[nodiscard]] QString selectedSummary() const;
    [[nodiscard]] bool canResume() const;
    [[nodiscard]] bool previewLoading() const noexcept;
    [[nodiscard]] QString previewError() const;
    [[nodiscard]] QString previewNotice() const;
    [[nodiscard]] ProviderConversationPreviewModel* preview() noexcept;

    Q_INVOKABLE [[nodiscard]] bool open();
    Q_INVOKABLE void close();
    Q_INVOKABLE [[nodiscard]] bool reload();
    Q_INVOKABLE [[nodiscard]] bool retry();
    Q_INVOKABLE [[nodiscard]] bool loadMore();
    Q_INVOKABLE [[nodiscard]] bool retryLoadMore();
    Q_INVOKABLE [[nodiscard]] bool select(const QString& presentationId);
    Q_INVOKABLE [[nodiscard]] bool retryPreview();
    Q_INVOKABLE [[nodiscard]] bool browseFolder();
    Q_INVOKABLE [[nodiscard]] bool cancelFolderBrowse();

    [[nodiscard]] std::optional<ProviderConversationResumeTarget>
    resolveResumeTarget(const QString& presentationId) const override;

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestAgentIntelEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void countChanged();
    void stateChanged();
    void selectionChanged();
    void decodeError(QString message);

private:
    static constexpr qsizetype pageItemLimit = 50;
    static constexpr qsizetype retainedItemLimit = 500;
    static constexpr qsizetype discoveryByteLimit = 131'072;
    static constexpr qsizetype previewRecordLimit = 100;
    static constexpr qsizetype previewRetainedLimit = 50;
    static constexpr qsizetype previewByteLimit = 262'144;

    struct Item {
        QString presentationId;
        QString identityKey;
        QString provider;
        QString nativeConversationId;
        QString workingDirectory;
        QString title;
        QString createdAt;
        QString updatedAt;
        QString accessibleId;
    };

    enum class CatalogOperation {
        Initial,
        More,
    };

    struct CatalogPending {
        QString requestId;
        QString cursor;
        CatalogOperation operation = CatalogOperation::Initial;
        quint64 runtimeGeneration = 0;
        quint64 demandGeneration = 0;
        quint64 folderGeneration = 0;
        QString provider;
        QString workingDirectory;
    };

    struct PreviewPending {
        QString requestId;
        quint64 runtimeGeneration = 0;
        quint64 demandGeneration = 0;
        quint64 folderGeneration = 0;
        quint64 selectionGeneration = 0;
        QString presentationId;
        QString identityKey;
        QString provider;
        QString workingDirectory;
        QString nativeConversationId;
    };

    CommandDispatcher& m_dispatcher;
    DesktopFileIntegration& m_desktopFiles;
    DesktopSettings& m_settings;
    AccountContextFence m_accountFence {256};
    ProviderConversationPreviewModel m_preview;
    QVector<Item> m_items;
    QVector<qsizetype> m_visibleRows;
    QSet<QString> m_seenCursors;
    std::optional<CatalogPending> m_catalogPending;
    std::optional<PreviewPending> m_previewPending;
    std::optional<CatalogOperation> m_failedCatalogOperation;
    QString m_failedCursor;
    QString m_workingDirectory;
    QString m_searchText;
    QString m_nextCursor;
    QString m_selectedPresentationId;
    QString m_error;
    QString m_loadMoreError;
    QString m_previewError;
    QString m_previewNotice;
    QString m_folderRequestId;
    QString m_accountUserId;
    QTimer m_catalogReplyTimer;
    QTimer m_previewReplyTimer;
    qint64 m_replyTimeoutMs;
    quint64 m_accountEpoch = 0;
    quint64 m_runtimeGeneration = 1;
    quint64 m_demandGeneration = 0;
    quint64 m_folderGeneration = 0;
    quint64 m_selectionGeneration = 0;
    State m_state = State::Dormant;
    Provider m_provider = Provider::Claude;
    bool m_hasAccountContext = false;
    bool m_demanded = false;
    bool m_loadingMore = false;
    bool m_hasMore = false;
    bool m_capped = false;
    bool m_previewLoading = false;

    void activateAccount(QString userId, quint64 epoch);
    void applyAgentIntelEvent(const QByteArray& json);
    void handleCatalogReply(
        const QJsonObject& payload,
        const CatalogPending& pending);
    void handlePreviewReply(
        const QJsonObject& payload,
        const PreviewPending& pending);
    void handleRuntimeError(
        const QJsonObject& object,
        const QString& requestId);
    [[nodiscard]] bool beginCatalog(
        CatalogOperation operation,
        const QString& cursor);
    [[nodiscard]] bool beginPreview(const Item& item);
    [[nodiscard]] bool sendCatalog(
        QJsonObject command,
        CatalogPending pending);
    [[nodiscard]] bool sendPreview(
        QJsonObject command,
        PreviewPending pending);
    [[nodiscard]] bool catalogPendingCurrent(
        const CatalogPending& pending) const;
    [[nodiscard]] bool previewPendingCurrent(
        const PreviewPending& pending) const;
    void catalogFailure(
        CatalogOperation operation,
        QString message,
        bool protocolFault);
    void previewFailure(QString message, bool protocolFault);
    void invalidateDemand(QString message = {});
    void clearCatalog();
    void clearPreview();
    void rebuildFilter();
    void setWorkingDirectory(QString directory);
    void handleFolderPicked(
        const QString& requestId,
        int purpose,
        const QString& canonicalDirectory);
    void handleFolderCancelled(const QString& requestId, int purpose);
    void handleFolderFailure(
        const QString& requestId,
        int purpose,
        const QString& message);
    [[nodiscard]] QString stablePresentationId(const QString& identityKey);
    [[nodiscard]] const Item* itemForPresentationId(
        const QString& presentationId) const;
    [[nodiscard]] const Item* selectedItem() const;
    [[nodiscard]] bool selectedVisible() const;
    [[nodiscard]] static QString providerWire(Provider provider);
    [[nodiscard]] static QString providerDisplay(Provider provider);
    [[nodiscard]] static QString folderDisplay(const QString& directory);
};

} // namespace kodosi
