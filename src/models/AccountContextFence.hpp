#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QVector>

#include <optional>

namespace kodosi {

struct AccountContext {
    QString userId;
    quint64 epoch;
};

enum class AccountEventAdmission {
    Current,
    Queued,
    Stale,
    Oversized,
};

struct AccountActivation {
    bool accepted;
    bool changed;
    QVector<QByteArray> pendingEvents;
};

[[nodiscard]] std::optional<quint64> exactUnsignedJsonField(
    QByteArrayView json,
    QByteArrayView field);

class AccountContextFence final {
public:
    explicit AccountContextFence(qsizetype maximumPendingEvents);

    void reset();
    [[nodiscard]] AccountActivation activate(AccountContext context);
    [[nodiscard]] AccountEventAdmission admit(
        const AccountContext& eventContext,
        QByteArray event);

private:
    static constexpr qsizetype maximumPendingBytes = 8 * 1024 * 1024;

    QVector<QByteArray> m_pendingEvents;
    qsizetype m_pendingBytes = 0;
    qsizetype m_maximumPendingEvents;
    AccountContext m_activeContext;
    bool m_hasActiveContext = false;
};

} // namespace kodosi
