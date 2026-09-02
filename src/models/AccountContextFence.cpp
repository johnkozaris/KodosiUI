#include "models/AccountContextFence.hpp"

#include <QJsonArray>
#include <QJsonDocument>

#include <charconv>
#include <system_error>
#include <utility>

namespace kodosi {
namespace {

std::optional<QString> decodeJsonString(const QByteArrayView token)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(
        QByteArrayLiteral("[") + token.toByteArray() + QByteArrayLiteral("]"),
        &error);
    const auto array = document.array();
    if (error.error != QJsonParseError::NoError || !document.isArray()
        || array.size() != 1 || !array.at(0).isString()) {
        return std::nullopt;
    }
    return array.at(0).toString();
}

qsizetype previousSignificantByte(const QByteArrayView json, qsizetype cursor)
{
    while (cursor >= 0
        && (json[cursor] == ' ' || json[cursor] == '\t'
            || json[cursor] == '\r' || json[cursor] == '\n')) {
        --cursor;
    }
    return cursor;
}

} // namespace

std::optional<quint64> exactUnsignedJsonField(
    const QByteArrayView json,
    const QByteArrayView field)
{
    const auto expectedField =
        QString::fromUtf8(field.data(), static_cast<qsizetype>(field.size()));
    std::optional<quint64> result;
    qsizetype depth = 0;

    for (qsizetype cursor = 0; cursor < json.size(); ++cursor) {
        const auto byte = json[cursor];
        if (byte == '{' || byte == '[') {
            ++depth;
            continue;
        }
        if (byte == '}' || byte == ']') {
            --depth;
            continue;
        }
        if (byte != '"') {
            continue;
        }

        const auto tokenStart = cursor;
        bool escaped = false;
        for (++cursor; cursor < json.size(); ++cursor) {
            if (escaped) {
                escaped = false;
            } else if (json[cursor] == '\\') {
                escaped = true;
            } else if (json[cursor] == '"') {
                break;
            }
        }
        if (cursor >= json.size()) {
            return std::nullopt;
        }

        const auto previous = previousSignificantByte(json, tokenStart - 1);
        if (depth != 1 || previous < 0
            || (json[previous] != '{' && json[previous] != ',')) {
            continue;
        }
        const auto key = decodeJsonString(
            json.sliced(tokenStart, cursor - tokenStart + 1));
        if (!key || *key != expectedField) {
            continue;
        }
        if (result) {
            return std::nullopt;
        }

        auto valueCursor = cursor + 1;
        while (valueCursor < json.size()
            && (json[valueCursor] == ' ' || json[valueCursor] == '\t'
                || json[valueCursor] == '\r' || json[valueCursor] == '\n')) {
            ++valueCursor;
        }
        if (valueCursor >= json.size() || json[valueCursor++] != ':') {
            return std::nullopt;
        }
        while (valueCursor < json.size()
            && (json[valueCursor] == ' ' || json[valueCursor] == '\t'
                || json[valueCursor] == '\r' || json[valueCursor] == '\n')) {
            ++valueCursor;
        }
        const auto valueStart = valueCursor;
        while (valueCursor < json.size()
            && json[valueCursor] >= '0' && json[valueCursor] <= '9') {
            ++valueCursor;
        }
        const auto valueEnd = valueCursor;
        while (valueCursor < json.size()
            && (json[valueCursor] == ' ' || json[valueCursor] == '\t'
                || json[valueCursor] == '\r' || json[valueCursor] == '\n')) {
            ++valueCursor;
        }
        if (valueEnd == valueStart || valueCursor >= json.size()
            || (json[valueCursor] != ',' && json[valueCursor] != '}')) {
            return std::nullopt;
        }
        quint64 value = 0;
        const auto parsed = std::from_chars(
            json.data() + valueStart,
            json.data() + valueEnd,
            value);
        if (parsed.ec != std::errc {} || parsed.ptr != json.data() + valueEnd) {
            return std::nullopt;
        }
        result = value;
    }
    return result;
}

AccountContextFence::AccountContextFence(const qsizetype maximumPendingEvents)
    : m_maximumPendingEvents(maximumPendingEvents)
{
    Q_ASSERT(maximumPendingEvents > 0);
}

void AccountContextFence::reset()
{
    m_pendingEvents.clear();
    m_pendingBytes = 0;
    m_activeContext = {};
    m_hasActiveContext = false;
}

AccountActivation AccountContextFence::activate(AccountContext context)
{
    if (m_hasActiveContext && context.epoch < m_activeContext.epoch) {
        return {
            .accepted = false,
            .changed = false,
            .pendingEvents = {},
        };
    }
    const auto changed = !m_hasActiveContext
        || context.epoch != m_activeContext.epoch
        || context.userId != m_activeContext.userId;
    m_hasActiveContext = true;
    m_activeContext = std::move(context);
    m_pendingBytes = 0;
    return {
        .accepted = true,
        .changed = changed,
        .pendingEvents = std::exchange(m_pendingEvents, {}),
    };
}

AccountEventAdmission AccountContextFence::admit(
    const AccountContext& eventContext,
    QByteArray event)
{
    if (!m_hasActiveContext || eventContext.epoch > m_activeContext.epoch) {
        if (event.size() > maximumPendingBytes) {
            return AccountEventAdmission::Oversized;
        }
        while (!m_pendingEvents.isEmpty()
            && (m_pendingEvents.size() >= m_maximumPendingEvents
                || event.size() > maximumPendingBytes - m_pendingBytes)) {
            m_pendingBytes -= m_pendingEvents.front().size();
            m_pendingEvents.removeFirst();
        }
        m_pendingBytes += event.size();
        m_pendingEvents.push_back(std::move(event));
        return AccountEventAdmission::Queued;
    }
    if (eventContext.epoch != m_activeContext.epoch
        || eventContext.userId != m_activeContext.userId) {
        return AccountEventAdmission::Stale;
    }
    return AccountEventAdmission::Current;
}

} // namespace kodosi
