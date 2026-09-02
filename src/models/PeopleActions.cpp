#include "models/PeopleActions.hpp"

#include <QJsonDocument>
#include <QUuid>

#include <utility>

namespace kodosi {

PeopleActions::PeopleActions(
    CommandDispatcher& dispatcher,
    PeopleModel& people,
    QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
    , m_people(people)
{
    connect(
        &people,
        &PeopleModel::operationError,
        this,
        [this](const QString&, const QString& message, const QString&) {
            m_lastError = message;
            emit stateChanged();
        });
}

QString PeopleActions::lastError() const
{
    return m_lastError;
}

bool PeopleActions::refresh()
{
    return send({
        {QStringLiteral("type"), QStringLiteral("friends.refresh")},
    });
}

bool PeopleActions::sendRequest(const QString& username)
{
    const auto normalized = normalizeUsername(username);
    if (normalized.isEmpty()) {
        m_lastError = QStringLiteral("Enter a username.");
        emit stateChanged();
        return false;
    }
    return send({
        {QStringLiteral("type"), QStringLiteral("friends.request.send")},
        {QStringLiteral("username"), normalized},
        {QStringLiteral("requestId"),
         QUuid::createUuidV7().toString(QUuid::WithoutBraces)},
    });
}

bool PeopleActions::accept(const QString& username)
{
    return relationshipAction(
        username,
        PeopleModel::Relationship::IncomingRequest,
        QStringLiteral("friends.request.accept"));
}

bool PeopleActions::reject(const QString& username)
{
    return relationshipAction(
        username,
        PeopleModel::Relationship::IncomingRequest,
        QStringLiteral("friends.request.reject"));
}

bool PeopleActions::cancel(const QString& username)
{
    return relationshipAction(
        username,
        PeopleModel::Relationship::OutgoingRequest,
        QStringLiteral("friends.request.cancel"));
}

bool PeopleActions::remove(const QString& username)
{
    return relationshipAction(
        username,
        PeopleModel::Relationship::Friend,
        QStringLiteral("friends.remove"));
}

void PeopleActions::clearError()
{
    if (m_lastError.isEmpty()) {
        return;
    }
    m_lastError.clear();
    emit stateChanged();
}

bool PeopleActions::send(QJsonObject command)
{
    m_lastError.clear();
    const auto json = QJsonDocument(std::move(command)).toJson(QJsonDocument::Compact);
    if (m_dispatcher.send(CommandLane::Friends, json)) {
        emit stateChanged();
        return true;
    }
    m_lastError = QStringLiteral("The runtime did not accept the People request.");
    emit stateChanged();
    return false;
}

bool PeopleActions::relationshipAction(
    const QString& username,
    const PeopleModel::Relationship expected,
    const QString& commandType)
{
    const auto normalized = normalizeUsername(username);
    if (normalized.isEmpty()
        || m_people.relationshipForHandle(normalized)
            != std::optional<PeopleModel::Relationship> {expected}) {
        m_lastError =
            QStringLiteral("That People action is no longer available.");
        emit stateChanged();
        return false;
    }
    return send({
        {QStringLiteral("type"), commandType},
        {QStringLiteral("username"), normalized},
    });
}

QString PeopleActions::normalizeUsername(const QString& value)
{
    auto normalized = value.trimmed();
    if (normalized.startsWith(QLatin1Char('@'))) {
        normalized.removeFirst();
    }
    return normalized.trimmed();
}

} // namespace kodosi
