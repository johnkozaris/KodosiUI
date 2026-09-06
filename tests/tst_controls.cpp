#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest/QTest>

#include <memory>

class ControlsTest final : public QObject {
    Q_OBJECT
private slots:
    void secondaryTextAndKeyboardFocusAreVisible();
};

void ControlsTest::secondaryTextAndKeyboardFocusAreVisible()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import "Controls"
        ApplicationWindow {
            width: 640; height: 240; visible: true
            KButton {
                objectName: "test.button"
                anchors.centerIn: parent
                width: 450
                text: "Mission"
                secondaryText: "Everyone can read it"
            }
        }
    )", QUrl::fromLocalFile(QStringLiteral(KODOSI_CONTROL_TEST_DIR "/test.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    QVERIFY(window);
    auto* button = root->findChild<QQuickItem*>(QStringLiteral("test.button"));
    QVERIFY(button);
    auto* indicator = button->findChild<QQuickItem*>(QStringLiteral("control.keyboardFocus"));
    QVERIFY(indicator);
    QVERIFY(!indicator->isVisible());
    bool secondaryVisible = false;
    for (auto* item : button->findChildren<QQuickItem*>()) {
        if (item->property("text").toString() == QStringLiteral("Everyone can read it")) {
            secondaryVisible = item->isVisible();
        }
    }
    QVERIFY(secondaryVisible);
    button->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(button->hasActiveFocus());
    QTRY_VERIFY(indicator->isVisible());
    QCOMPARE(indicator->height(), 2.0);
    button->setEnabled(false);
    QTRY_VERIFY(!indicator->isVisible());
}

QTEST_MAIN(ControlsTest)
#include "tst_controls.moc"
