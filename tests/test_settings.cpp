#include "core/Settings.h"

#include <QtTest>

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void defaults()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("s.ini"));
        settings.load();
        QCOMPARE(settings.editor.tabWidth, 4);
        QCOMPARE(settings.compiler.gxxPath, QString("g++"));
        QCOMPARE(settings.compiler.cxxStandard, QString("c++17"));
        QCOMPARE(settings.appearance.theme, ThemeKind::Dark);
    }

    void persistsBetweenSessions()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("s.ini");
        {
            Settings settings(path);
            settings.editor.tabWidth = 2;
            settings.editor.wordWrap = true;
            settings.appearance.theme = ThemeKind::Light;
            settings.appearance.fontFamily = "Fira Code";
            settings.appearance.editorBackground = "#101010";
            settings.appearance.syntaxColors.insert("keyword", "#ff0000");
            settings.appearance.sidebarVisible = false;
            settings.compiler.cxxStandard = "c++23";
            settings.compiler.includeDirs = {"/a b", "/c"};
            settings.compiler.configuration = BuildConfiguration::Release;
            settings.save();
        }

        Settings settings(path);
        settings.load();
        QCOMPARE(settings.editor.tabWidth, 2);
        QVERIFY(settings.editor.wordWrap);
        QCOMPARE(settings.appearance.theme, ThemeKind::Light);
        QCOMPARE(settings.appearance.fontFamily, QString("Fira Code"));
        QCOMPARE(settings.appearance.editorBackground, QString("#101010"));
        QCOMPARE(settings.appearance.syntaxColors.value("keyword"), QString("#ff0000"));
        QVERIFY(!settings.appearance.sidebarVisible);
        QCOMPARE(settings.compiler.cxxStandard, QString("c++23"));
        QCOMPARE(settings.compiler.includeDirs, QStringList({"/a b", "/c"}));
        QCOMPARE(settings.compiler.configuration, BuildConfiguration::Release);
    }

    void removedSyntaxColourStaysRemoved()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("s.ini");
        Settings settings(path);
        settings.appearance.syntaxColors.insert("keyword", "#ff0000");
        settings.save();
        settings.appearance.syntaxColors.clear();
        settings.save();
        settings.load();
        QVERIFY(settings.appearance.syntaxColors.isEmpty());
    }

    void recentFilesAreMostRecentFirstAndCapped()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("s.ini"));
        for (int i = 0; i < 15; ++i)
            settings.addRecentFile(QString("/f%1").arg(i));
        settings.addRecentFile("/f10");
        QCOMPARE(settings.recentFiles().size(), 10);
        QCOMPARE(settings.recentFiles().first(), QString("/f10"));
        QCOMPARE(settings.recentFiles().count("/f10"), 1);
    }

    void buildOptionsFollowConfiguration()
    {
        CompilerSettings compiler;
        compiler.warningFlags = "-Wall -Wshadow";
        compiler.libraries = "m pthread";
        compiler.additionalArgs = "-DX=\"a b\"";

        const BuildOptions debug = compiler.buildOptions(BuildConfiguration::Debug);
        QVERIFY(debug.debugInfo);
        QCOMPARE(debug.optimization, QString("-O0"));
        QCOMPARE(debug.warningFlags, QStringList({"-Wall", "-Wshadow"}));
        QCOMPARE(debug.libraries, QStringList({"m", "pthread"}));
        QCOMPARE(debug.extraArgs, QStringList({"-DX=a b"}));

        const BuildOptions release = compiler.buildOptions(BuildConfiguration::Release);
        QVERIFY(!release.debugInfo);
        QCOMPARE(release.optimization, QString("-O2"));
        QVERIFY(release.defines.contains("NDEBUG"));
    }

    void runOptionsArePerFile()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("s.ini"));
        settings.setRunOptions("/w/a.cpp", {"--flag \"two words\"", "/tmp", "tool"});
        const FileRunOptions options = settings.runOptions("/w/a.cpp");
        QCOMPARE(options.arguments, QString("--flag \"two words\""));
        QCOMPARE(options.workingDirectory, QString("/tmp"));
        QCOMPARE(options.outputName, QString("tool"));
        QVERIFY(settings.runOptions("/w/b.cpp").arguments.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "test_settings.moc"
