#include "bridge/RuntimeBridge.hpp"
#include "models/SessionCatalogModel.hpp"
#include "terminal/TerminalSessionRegistry.hpp"
#include "terminal/TerminalView.hpp"
#include <QApplication>
#include <QDir>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest/QTest>

class RuntimeTerminalJourney final : public QObject {
    Q_OBJECT
private slots:
    void createsReadsTypesAndStopsRealLocalTerminal()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(QDir(root.path()).mkdir(QStringLiteral("home")));
        qputenv("HOME", root.filePath(QStringLiteral("home")).toUtf8());
        qputenv("KODOSI_DATA_ROOT", root.filePath(QStringLiteral("data")).toUtf8());
        qputenv("KODOSI_PRODUCTION_DATA_ROOT", root.filePath(QStringLiteral("production-unused")).toUtf8());
        qputenv("KODOSI__BACKEND__API", "http://127.0.0.1:1");
        qputenv("KODOSI__AUTH__ISSUER", "http://127.0.0.1:1");
        qputenv("KODOSI__RUNTIME__INITIAL_SHELL", "/bin/sh");
        kodosi::TerminalSessionRegistry registry;
        kodosi::RuntimeBridge runtime(registry);
        kodosi::SessionCatalogModel sessions;
        QStringList failures;
        QObject::connect(&runtime,&kodosi::RuntimeBridge::eventReceived,&sessions,[&](const QJsonObject& event){
            sessions.apply(event);
            if(event.value(QStringLiteral("type")).toString()==QStringLiteral("session.error")) failures.append(event.value(QStringLiteral("message")).toString());
        });
        QVERIFY(runtime.start());
        QTRY_VERIFY_WITH_TIMEOUT(sessions.hasAuthoritativeSnapshot(),15000);
        const auto request=QUuid::createUuidV7().toString(QUuid::WithoutBraces);
        QVERIFY(runtime.send(QJsonDocument(QJsonObject{{QStringLiteral("type"),QStringLiteral("session.create")},{QStringLiteral("requestId"),request},{QStringLiteral("name"),QStringLiteral("Native journey")},{QStringLiteral("workingDir"),root.path()}}).toJson(QJsonDocument::Compact)));
        QTRY_COMPARE_WITH_TIMEOUT(sessions.rowCount(),1,15000);
        const auto id=sessions.data(sessions.index(0),kodosi::SessionCatalogModel::SessionIdRole).toString();
        const auto incarnation=*sessions.incarnationForSession(id);
        kodosi::TerminalView view;
        view.setWidth(800);view.setHeight(450);view.setTerminalInteraction(true, true);
        QObject::connect(&view,&kodosi::TerminalView::terminalError,&view,[&](const QString& error){failures.append(error);});
        QVERIFY(view.attach(registry,runtime,{id,QUuid::createUuidV7().toString(QUuid::WithoutBraces),1},incarnation));
        QTRY_VERIFY_WITH_TIMEOUT(view.terminalReady(),15000);
        QInputMethodEvent input;
        input.setCommitString(QStringLiteral("printf 'KODOSI_%s_%s\\n' NATIVE OK\n"));
        QCoreApplication::sendEvent(&view,&input);
        QTRY_VERIFY_WITH_TIMEOUT(view.accessibleText().contains(QStringLiteral("KODOSI_NATIVE_OK")),15000);
        view.detach();
        QVERIFY(sessions.containsSession(id));
        QVERIFY(runtime.send(QJsonDocument(QJsonObject{{QStringLiteral("type"),QStringLiteral("session.stop")},{QStringLiteral("requestId"),QUuid::createUuidV7().toString(QUuid::WithoutBraces)},{QStringLiteral("sessionId"),id},{QStringLiteral("expectedRuntimeIncarnationId"),incarnation}}).toJson(QJsonDocument::Compact)));
        QTRY_COMPARE_WITH_TIMEOUT(sessions.rowCount(),0,15000);
        QVERIFY2(failures.isEmpty(),qPrintable(failures.join(QChar(u'\n'))));
        runtime.stop();
    }
};
QTEST_MAIN(RuntimeTerminalJourney)
#include "tst_runtime_terminal_journey.moc"
