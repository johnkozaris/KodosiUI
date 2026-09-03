#pragma once

#include "models/AccountContextFence.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QVariantList>
#include <QVector>

#include <optional>

namespace kodosi {

class DeviceLinkRequestsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(quint64 revision READ revision NOTIFY stateChanged)

public:
    enum Role {
        UserCodeRole = Qt::UserRole + 1,
        DeviceLabelRole,
        ExpiresAtRole,
    };
    Q_ENUM(Role)

    explicit DeviceLinkRequestsModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] quint64 revision() const noexcept;
    Q_INVOKABLE [[nodiscard]] QVariantMap presentationAt(int row) const;

signals:
    void countChanged();
    void stateChanged();

private:
    friend class DevicesModel;

    struct Request {
        QString userCode;
        QString deviceLabel;
        QDateTime expiresAt;
    };

    QVector<Request> m_requests;
    quint64 m_revision = 0;

    void replace(QVector<Request> requests);
    void upsert(Request request);
    void remove(const QString& userCode);
    void removeExpired(const QDateTime& now);
    [[nodiscard]] std::optional<QDateTime> nearestExpiry() const;
};

class DevicesModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString selfDeviceId READ selfDeviceId NOTIFY stateChanged)
    Q_PROPERTY(QString selfDeviceLabel READ selfDeviceLabel NOTIFY stateChanged)
    Q_PROPERTY(
        QString selfCertSignerDeviceId
        READ selfCertSignerDeviceId
        NOTIFY stateChanged)
    Q_PROPERTY(
        QDateTime selfCertIssuedAt
        READ selfCertIssuedAt
        NOTIFY stateChanged)
    Q_PROPERTY(bool hasEnrollmentState READ hasEnrollmentState NOTIFY stateChanged)
    Q_PROPERTY(bool localDeviceEnrolled READ localDeviceEnrolled NOTIFY stateChanged)
    Q_PROPERTY(InventoryState inventoryState READ inventoryState NOTIFY stateChanged)
    Q_PROPERTY(
        kodosi::DeviceLinkRequestsModel* pendingLinks
        READ pendingLinks
        CONSTANT)
    Q_PROPERTY(
        QVariantList pendingLinkPresentations
        READ pendingLinkPresentations
        NOTIFY stateChanged)
    Q_PROPERTY(bool hasSelfLinkPending READ hasSelfLinkPending NOTIFY stateChanged)
    Q_PROPERTY(QString selfLinkUserCode READ selfLinkUserCode NOTIFY stateChanged)
    Q_PROPERTY(QDateTime selfLinkExpiresAt READ selfLinkExpiresAt NOTIFY stateChanged)
    Q_PROPERTY(QString lastResolvedUserCode READ lastResolvedUserCode NOTIFY stateChanged)
    Q_PROPERTY(LinkOutcome lastLinkOutcome READ lastLinkOutcome NOTIFY stateChanged)
    Q_PROPERTY(
        QString lastLinkOutcomeMessage
        READ lastLinkOutcomeMessage
        NOTIFY stateChanged)
    Q_PROPERTY(
        bool lastLinkApproved
        READ lastLinkApproved
        NOTIFY stateChanged)
    Q_PROPERTY(SelfLinkOutcome selfLinkOutcome READ selfLinkOutcome NOTIFY stateChanged)
    Q_PROPERTY(
        QString selfLinkOutcomeMessage
        READ selfLinkOutcomeMessage
        NOTIFY stateChanged)
    Q_PROPERTY(
        bool selfLinkApproved
        READ selfLinkApproved
        NOTIFY stateChanged)

public:
    enum class InventoryState {
        Idle,
        Fresh,
        Stale,
    };
    Q_ENUM(InventoryState)

    enum class LinkOutcome {
        LinkNone,
        LinkApproved,
        LinkCancelled,
        LinkUnknown,
    };
    Q_ENUM(LinkOutcome)

    enum class SelfLinkOutcome {
        SelfLinkNone,
        SelfLinkApproved,
        SelfLinkCancelled,
        SelfLinkExpired,
        SelfLinkFailed,
        SelfLinkUnknown,
    };
    Q_ENUM(SelfLinkOutcome)

    enum Role {
        DeviceIdRole = Qt::UserRole + 1,
        LabelRole,
        CertSignerDeviceIdRole,
        CertIssuedAtMsRole,
        IsSelfRole,
    };
    Q_ENUM(Role)

    explicit DevicesModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString selfDeviceId() const;
    [[nodiscard]] QString selfDeviceLabel() const;
    [[nodiscard]] QString selfCertSignerDeviceId() const;
    [[nodiscard]] QDateTime selfCertIssuedAt() const;
    [[nodiscard]] bool hasEnrollmentState() const noexcept;
    [[nodiscard]] bool localDeviceEnrolled() const noexcept;
    [[nodiscard]] InventoryState inventoryState() const noexcept;
    [[nodiscard]] DeviceLinkRequestsModel* pendingLinks() noexcept;
    [[nodiscard]] QVariantList pendingLinkPresentations() const;
    [[nodiscard]] bool hasSelfLinkPending() const noexcept;
    [[nodiscard]] QString selfLinkUserCode() const;
    [[nodiscard]] QDateTime selfLinkExpiresAt() const;
    [[nodiscard]] QString lastResolvedUserCode() const;
    [[nodiscard]] LinkOutcome lastLinkOutcome() const noexcept;
    [[nodiscard]] QString lastLinkOutcomeMessage() const;
    [[nodiscard]] bool lastLinkApproved() const noexcept;
    [[nodiscard]] SelfLinkOutcome selfLinkOutcome() const noexcept;
    [[nodiscard]] QString selfLinkOutcomeMessage() const;
    [[nodiscard]] bool selfLinkApproved() const noexcept;

    Q_INVOKABLE [[nodiscard]] QString normalizeUserCode(const QString& value) const;
    Q_INVOKABLE [[nodiscard]] bool isCanonicalUserCode(const QString& value) const;
    Q_INVOKABLE [[nodiscard]] bool containsDevice(const QString& deviceId) const;
    Q_INVOKABLE void dismissLinkOutcome();
    Q_INVOKABLE void dismissSelfLinkOutcome();

    void pruneExpiredLinks(const QDateTime& now);

public slots:
    void ingestAuthEvent(QByteArray json);
    void ingestDevicesEvent(QByteArray json);
    void resetRuntimeAuthority();

signals:
    void countChanged();
    void stateChanged();
    void authorityChanged();
    void decodeError(QString message);
    void operationError(QString operation, QString message, QString userCode);

private:
    struct Device {
        QString deviceId;
        QString label;
        QString certSignerDeviceId;
        quint64 certIssuedAtMs;
    };

    QVector<Device> m_devices;
    DeviceLinkRequestsModel m_pendingLinks;
    AccountContextFence m_accountFence {128};
    QTimer m_expiryTimer;
    QString m_selfDeviceId;
    QString m_selfLinkUserCode;
    QDateTime m_selfLinkExpiresAt;
    QString m_lastResolvedUserCode;
    InventoryState m_inventoryState = InventoryState::Idle;
    LinkOutcome m_lastLinkOutcome = LinkOutcome::LinkNone;
    SelfLinkOutcome m_selfLinkOutcome = SelfLinkOutcome::SelfLinkNone;
    bool m_hasEnrollmentState = false;
    bool m_localDeviceEnrolled = false;

    void activateAccount(QString userId, quint64 epoch);
    void applyDevicesEvent(const QJsonObject& object);
    void clearAccountState();
    void replaceDevices(QVector<Device> devices);
    void scheduleExpiry();

    [[nodiscard]] static std::optional<Device> decodeDevice(const QJsonObject& object);
    [[nodiscard]] static std::optional<DeviceLinkRequestsModel::Request> decodeRequest(
        const QJsonObject& object);
    [[nodiscard]] static std::optional<QDateTime> decodeTimestamp(const QString& value);
    [[nodiscard]] static LinkOutcome decodeLinkOutcome(const QString& value);
    [[nodiscard]] static SelfLinkOutcome decodeSelfLinkOutcome(const QString& value);
    [[nodiscard]] static QString normalizeCode(const QString& value);
    [[nodiscard]] static bool isCanonicalCode(const QString& value);
    [[nodiscard]] const Device* selfDevice() const;
};

} // namespace kodosi
