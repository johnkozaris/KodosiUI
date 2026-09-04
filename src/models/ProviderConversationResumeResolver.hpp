#pragma once

#include <QString>

#include <optional>

namespace kodosi {

struct ProviderConversationResumeTarget {
    QString provider;
    QString nativeConversationId;
    QString workingDirectory;
    QString title;
    QString accountUserId;
    quint64 accountEpoch = 0;
};

class ProviderConversationResumeResolver {
public:
    virtual ~ProviderConversationResumeResolver() = default;

    [[nodiscard]] virtual std::optional<ProviderConversationResumeTarget>
    resolveResumeTarget(const QString& presentationId) const = 0;
};

} // namespace kodosi
