#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

#include <memory>
#include <optional>

namespace kodosi {

class DesktopSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY settingsChanged)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY settingsChanged)
    Q_PROPERTY(CursorStyle cursorStyle READ cursorStyle NOTIFY settingsChanged)
    Q_PROPERTY(double lineHeight READ lineHeight NOTIFY settingsChanged)
    Q_PROPERTY(int scrollbackLines READ scrollbackLines NOTIFY settingsChanged)
    Q_PROPERTY(bool cursorBlink READ cursorBlink NOTIFY settingsChanged)
    Q_PROPERTY(
        bool toolApprovalAlerts
        READ toolApprovalAlerts
        NOTIFY settingsChanged)
    Q_PROPERTY(
        QString lastWorkingDirectory
        READ lastWorkingDirectory
        NOTIFY settingsChanged)
    Q_PROPERTY(
        QString effectiveWorkingDirectory
        READ effectiveWorkingDirectory
        NOTIFY settingsChanged)
    Q_PROPERTY(QString settingsError READ settingsError NOTIFY settingsErrorChanged)
    Q_PROPERTY(int minimumFontSize READ minimumFontSize CONSTANT)
    Q_PROPERTY(int maximumFontSize READ maximumFontSize CONSTANT)
    Q_PROPERTY(double minimumLineHeight READ minimumLineHeight CONSTANT)
    Q_PROPERTY(double maximumLineHeight READ maximumLineHeight CONSTANT)
    Q_PROPERTY(int minimumScrollbackLines READ minimumScrollbackLines CONSTANT)
    Q_PROPERTY(int maximumScrollbackLines READ maximumScrollbackLines CONSTANT)

public:
    enum class CursorStyle {
        Block = 0,
        Bar = 1,
        Underline = 2,
    };
    Q_ENUM(CursorStyle)

    struct Values {
        QString fontFamily;
        int fontSize;
        CursorStyle cursorStyle;
        double lineHeight;
        int scrollbackLines;
        bool cursorBlink;
        bool toolApprovalAlerts;
        std::optional<QString> lastWorkingDirectory;

        bool operator==(const Values&) const = default;
    };

    explicit DesktopSettings(QObject* parent = nullptr);
    explicit DesktopSettings(
        std::unique_ptr<QSettings> settings,
        QObject* parent = nullptr);

    [[nodiscard]] QString fontFamily() const;
    [[nodiscard]] int fontSize() const noexcept;
    [[nodiscard]] CursorStyle cursorStyle() const noexcept;
    [[nodiscard]] double lineHeight() const noexcept;
    [[nodiscard]] int scrollbackLines() const noexcept;
    [[nodiscard]] bool cursorBlink() const noexcept;
    [[nodiscard]] bool toolApprovalAlerts() const noexcept;
    [[nodiscard]] QString lastWorkingDirectory() const;
    [[nodiscard]] QString effectiveWorkingDirectory() const;
    [[nodiscard]] QString settingsError() const;

    [[nodiscard]] static constexpr int minimumFontSize() noexcept { return 8; }
    [[nodiscard]] static constexpr int maximumFontSize() noexcept { return 32; }
    [[nodiscard]] static constexpr double minimumLineHeight() noexcept { return 0.8; }
    [[nodiscard]] static constexpr double maximumLineHeight() noexcept { return 2.0; }
    [[nodiscard]] static constexpr int minimumScrollbackLines() noexcept { return 100; }
    [[nodiscard]] static constexpr int maximumScrollbackLines() noexcept { return 100'000; }
    [[nodiscard]] static Values defaultValues();

    Q_INVOKABLE [[nodiscard]] bool apply(
        const QString& fontFamily,
        int fontSize,
        int cursorStyle,
        double lineHeight,
        int scrollbackLines,
        bool cursorBlink,
        bool toolApprovalAlerts,
        const QString& lastWorkingDirectory);
    Q_INVOKABLE [[nodiscard]] bool reset();
    Q_INVOKABLE [[nodiscard]] bool resetTerminal();
    Q_INVOKABLE void clearError();

signals:
    void settingsChanged();
    void settingsErrorChanged();

private:
    std::unique_ptr<QSettings> m_settings;
    Values m_values = defaultValues();
    QString m_settingsError;

    void load();
    [[nodiscard]] bool persist(const Values& values);
    [[nodiscard]] bool commit(const Values& values);
    void setError(QString error);
    void removeLegacyValues();
};

} // namespace kodosi
