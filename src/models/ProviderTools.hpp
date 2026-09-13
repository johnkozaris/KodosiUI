#pragma once
#include "bridge/RuntimeBridge.hpp"
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <optional>
namespace kodosi {
class ProviderTools final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString provider READ provider NOTIFY selectionChanged)
    Q_PROPERTY(QString directory READ directory NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList conversations READ conversations NOTIFY conversationsChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(QVariantMap selectedConversation READ selectedConversation NOTIFY entriesChanged)
    Q_PROPERTY(QVariantMap installation READ installation NOTIFY installationChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY conversationsChanged)
    Q_PROPERTY(bool hasOlder READ hasOlder NOTIFY entriesChanged)
    Q_PROPERTY(bool hasNewer READ hasNewer NOTIFY entriesChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    explicit ProviderTools(CommandDispatcher& commands, QObject* parent = nullptr);
    QString provider() const { return m_provider; }
    QString directory() const { return m_directory; }
    QVariantList conversations() const { return m_conversations; }
    QVariantList entries() const { return m_entries; }
    QVariantMap selectedConversation() const { return m_selected; }
    QVariantMap installation() const { return m_installation; }
    bool busy() const { return !m_requestId.isEmpty(); }
    bool hasMore() const { return !m_cursor.isEmpty(); }
    bool hasOlder() const { return m_before.has_value(); }
    bool hasNewer() const { return !m_newer.isEmpty(); }
    QString error() const { return m_error; }
    Q_INVOKABLE void select(const QString& provider, const QString& directory);
    Q_INVOKABLE void discover();
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void preview(const QString& nativeId);
    Q_INVOKABLE void loadOlder();
    Q_INVOKABLE void loadNewer();
    Q_INVOKABLE void loadLatest();
    Q_INVOKABLE void inspect();
    Q_INVOKABLE void clearError();
    void apply(const QJsonObject& event);
    Q_INVOKABLE void reset();
signals:
    void selectionChanged();
    void conversationsChanged();
    void entriesChanged();
    void installationChanged();
    void busyChanged();
    void errorChanged();

private:
    CommandDispatcher& m_commands;
    QTimer m_timeout;
    QString m_provider = QStringLiteral("claude"), m_directory, m_requestId, m_operation, m_error, m_cursor;
    QVariantList m_conversations, m_entries;
    QVariantMap m_selected, m_installation;
    std::optional<qint64> m_before, m_pageBefore, m_requestedBefore;
    QList<std::optional<qint64>> m_newer;
    enum class Navigation { Latest, Older, Newer };
    Navigation m_navigation = Navigation::Latest;
    void readPage(std::optional<qint64> before, Navigation navigation);
    bool m_append = false;
    void send(QString operation, QJsonObject values = {});
    void fail(QString message);
};
}
