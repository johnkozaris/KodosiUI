#pragma once

#include "bridge/RuntimeBridge.hpp"
#include "models/PeopleModel.hpp"

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace kodosi {

class PeopleActions final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)

public:
    PeopleActions(
        CommandDispatcher& dispatcher,
        PeopleModel& people,
        QObject* parent = nullptr);

    [[nodiscard]] QString lastError() const;

    Q_INVOKABLE [[nodiscard]] bool refresh();
    Q_INVOKABLE [[nodiscard]] bool sendRequest(const QString& username);
    Q_INVOKABLE [[nodiscard]] bool accept(const QString& username);
    Q_INVOKABLE [[nodiscard]] bool reject(const QString& username);
    Q_INVOKABLE [[nodiscard]] bool cancel(const QString& username);
    Q_INVOKABLE [[nodiscard]] bool remove(const QString& username);
    Q_INVOKABLE void clearError();

signals:
    void stateChanged();

private:
    CommandDispatcher& m_dispatcher;
    PeopleModel& m_people;
    QString m_lastError;

    [[nodiscard]] bool send(QJsonObject command);
    [[nodiscard]] bool relationshipAction(
        const QString& username,
        PeopleModel::Relationship expected,
        const QString& commandType);
    [[nodiscard]] static QString normalizeUsername(const QString& value);
};

} // namespace kodosi
