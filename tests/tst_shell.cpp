#include "app/QmlModelTypes.hpp"
#include "SessionFixture.hpp"
#include <QApplication>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QStandardPaths>
#include "terminal/TerminalSessionRegistry.hpp"
#include <QtTest/QTest>

#include <QtQml/QQmlExtensionPlugin>
Q_IMPORT_QML_PLUGIN(Kodosi_ModelsPlugin)
Q_IMPORT_QML_PLUGIN(KodosiPlugin)

class ShellCommands final : public kodosi::CommandDispatcher {
public:
    QList<QJsonObject> sent;
    Result send(QByteArrayView bytes) override { sent.append(QJsonDocument::fromJson(bytes.toByteArray()).object()); return {}; }
};
class ShellTest final : public QObject {
    Q_OBJECT
private slots:
    void loadsReducedShellAndTerminalTile() {
        QTemporaryDir directory;
        ShellCommands commands;
        kodosi::TerminalSessionRegistry registry;
        kodosi::RuntimeBridge runtime(registry);
        kodosi::SessionCatalogModel sessions;
        kodosi::DesktopStateModel desktop(std::make_unique<QSettings>(directory.filePath(QStringLiteral("ui.ini")),QSettings::IniFormat),false);
        desktop.attachSessionCatalog(&sessions);
        kodosi::Workspace workspace(commands,sessions,desktop);
        kodosi::ProviderTools providers(commands);
        kodosi::AppearanceModel appearance;
        kodosi::DesktopSettings settings(std::make_unique<QSettings>(directory.filePath(QStringLiteral("terminal.ini")),QSettings::IniFormat));
        kodosi::DesktopFileIntegration files([](const QUrl&){return true;});
        kodosi::TerminalTilingLayoutModel tiling;
        kodosi::ApplicationLifecycleModel lifecycle(runtime,[](auto completion){completion({});},[]{});
        kodosi::TerminalSurfaceController surfaces(registry,runtime,sessions);
        kodosi::qml::configureModelInstances(workspace,providers,appearance,settings,desktop,sessions,tiling,files,lifecycle,surfaces);
        QQmlApplicationEngine engine;QStringList failures;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& error:errors) failures.append(error.toString());});
        engine.loadFromModule(QStringLiteral("Kodosi"), QStringLiteral("Main"));
        QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(failures.join(QChar(u'\n'))));
        lifecycle.scheduleInitialStart();QTRY_COMPARE(lifecycle.state(),kodosi::ApplicationLifecycleModel::State::Ready);
        workspace.apply(test::snapshot({test::session(1)}));QVERIFY(workspace.activateSession(test::id(1)));
        QCoreApplication::processEvents();
        QTRY_VERIFY(tiling.rowCount()>0);
        const auto root=engine.rootObjects().first();
        const auto tileName=QStringLiteral("stage.tile.")+test::id(1);
        const auto findItem = [](QQuickItem* item,const QString& name,auto&& self) -> QQuickItem* {
            if (item->objectName()==name) return item;
            for (auto* child:item->childItems()) if(auto* result=self(child,name,self)) return result;
            return nullptr;
        };
        auto* window=qobject_cast<QQuickWindow*>(root);QVERIFY(window);
        QTRY_VERIFY(findItem(window->contentItem(),tileName,findItem)!=nullptr);
        QCOMPARE(findItem(window->contentItem(),tileName,findItem)->property("sessionName").toString(),QStringLiteral("Terminal"));
        auto* details=root->findChild<QObject*>(QStringLiteral("panel.sessionDetails"));
        QVERIFY(details);
        QVERIFY(QMetaObject::invokeMethod(details,"openSession",Q_ARG(QVariant,QVariant(test::id(1)))));
        QCoreApplication::processEvents();
        QVERIFY(QMetaObject::invokeMethod(details,"close"));
        QVERIFY(QMetaObject::invokeMethod(root,"openCreate",Q_ARG(QVariant,QVariant(QString {}))));
        QCoreApplication::processEvents();
        QCOMPARE(commands.sent.last().value(QStringLiteral("type")).toString(),QStringLiteral("session.create"));
        QVERIFY(!commands.sent.last().value(QStringLiteral("name")).toString().isEmpty());
        QVERIFY(!root->findChild<QObject*>(QStringLiteral("panel.createSession")));
        for(int view=0;view<4;++view){desktop.setActiveView(view);QCoreApplication::processEvents();}
        QVERIFY2(failures.isEmpty(),qPrintable(failures.join(QChar(u'\n'))));
        engine.rootObjects().first()->setProperty("visible",false);
    }
};
int main(int argc,char** argv){QQuickStyle::setStyle(QStringLiteral("Basic"));QApplication app(argc,argv);QCoreApplication::setOrganizationName(QStringLiteral("KodosiTest"));QCoreApplication::setApplicationName(QStringLiteral("ShellTest"));QStandardPaths::setTestModeEnabled(true);ShellTest test;return QTest::qExec(&test,argc,argv);}
#include "tst_shell.moc"
