#include "project/Project.h"

#include <QtTest>

namespace {

void writeText(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

// Moves a file's timestamp into the future. Filesystem timestamps are too
// coarse for "touched just now" to be reliably newer than a fresh build.
void touchFuture(const QString &path)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(5),
                             QFileDevice::FileModificationTime));
}

bool buildSync(const BuildPlan &plan, BuildResult *result)
{
    BuildProcess build;
    QSignalSpy finished(&build, &BuildProcess::finished);
    build.start(plan);
    if (!finished.wait(60000))
        return false;
    *result = finished.at(0).at(0).value<BuildResult>();
    return true;
}

Project threeFileProject(const QString &root)
{
    writeText(root + "/main.cpp", "#include \"util.h\"\nint main() { return add(2, 3) - 5; }\n");
    writeText(root + "/src/util.cpp", "#include \"util.h\"\nint add(int a, int b) { return a + b; }\n");
    writeText(root + "/util.h", "int add(int a, int b);\n");

    Project project = Project::create(root, "demo", true);
    project.addFile(root + "/src/util.cpp");
    project.addFile(root + "/util.h");
    project.includeDirs = {"."};
    return project;
}

} // namespace

Q_DECLARE_METATYPE(BuildResult)

class TestProject : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { qRegisterMetaType<BuildResult>(); }

    void savesAndLoadsHumanReadableJson()
    {
        QTemporaryDir dir;
        Project project = Project::create(dir.path(), "demo", true);
        project.addFile(dir.filePath("lib/x.cpp"));
        project.addFile(dir.filePath("lib/x.h"));
        project.cxxStandard = "c++20";
        project.defines = {"FOO=1"};
        project.targetType = TargetType::SharedLibrary;
        project.folders = {"empty"};
        project.runArguments = "--verbose";
        QString error;
        QVERIFY2(project.save(&error), qPrintable(error));

        QFile file(project.filePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray json = file.readAll();
        QVERIFY(json.contains("\n    \"name\": \"demo\""));
        QVERIFY(json.contains("lib/x.cpp"));

        Project loaded;
        QVERIFY2(loaded.load(project.filePath(), &error), qPrintable(error));
        QCOMPARE(loaded.name, QString("demo"));
        QCOMPARE(loaded.sources, QStringList({"main.cpp", "lib/x.cpp"}));
        QCOMPARE(loaded.headers, QStringList({"lib/x.h"}));
        QCOMPARE(loaded.cxxStandard, QString("c++20"));
        QCOMPARE(loaded.defines, QStringList({"FOO=1"}));
        QCOMPARE(loaded.targetType, TargetType::SharedLibrary);
        QCOMPARE(loaded.folders, QStringList({"empty"}));
        QCOMPARE(loaded.runArguments, QString("--verbose"));
    }

    void rejectsInvalidFiles()
    {
        QTemporaryDir dir;
        writeText(dir.filePath("bad.lideproj"), "{ not json");
        Project project;
        QString error;
        QVERIFY(!project.load(dir.filePath("bad.lideproj"), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!project.load(dir.filePath("missing.lideproj"), &error));
    }

    void addAndRemoveFiles()
    {
        QTemporaryDir dir;
        Project project = Project::create(dir.path(), "demo", false);
        QCOMPARE(project.sources, QStringList({"main.c"}));
        QVERIFY(project.addFile(dir.filePath("a.c")));
        QVERIFY(!project.addFile(dir.filePath("a.c")));
        QVERIFY(!project.addFile(dir.filePath("notes.txt")));
        QVERIFY(project.contains(dir.filePath("a.c")));
        QVERIFY(project.removeFile(dir.filePath("a.c")));
        QVERIFY(!project.removeFile(dir.filePath("a.c")));
    }

    void parsesDependencyFiles()
    {
        const QByteArray text = "obj/a.cpp.o: /w/a.cpp /w/my\\ dir/b.h \\\n"
                                " /w/c.h\n"
                                "\n"
                                "/w/my\\ dir/b.h:\n"
                                "\n"
                                "/w/c.h:\n";
        QCOMPARE(Project::parseDependencyFile(text),
                 QStringList({"/w/a.cpp", "/w/my dir/b.h", "/w/c.h"}));
    }

    void buildsMultiFileProjectAndRunsIt()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath("my project");
        const Project project = threeFileProject(root);

        BuildResult result;
        const BuildPlan plan = project.makePlan(Toolchain{}, BuildConfiguration::Debug);
        QCOMPARE(plan.compileJobs.size(), 2);
        QVERIFY(plan.linkJob.has_value());
        QVERIFY(buildSync(plan, &result));
        QVERIFY(result.success);
        QCOMPARE(result.outputPath, project.outputPath(BuildConfiguration::Debug));
        QVERIFY(QFileInfo(result.outputPath).isExecutable());
        QCOMPARE(QProcess::execute(result.outputPath, {}), 0);
    }

    void incrementalBuildOnlyRebuildsWhatChanged()
    {
        QTemporaryDir dir;
        const Project project = threeFileProject(dir.path());
        BuildResult result;
        QVERIFY(buildSync(project.makePlan(Toolchain{}, BuildConfiguration::Debug), &result));
        QVERIFY(result.success);

        // Nothing changed.
        BuildPlan plan = project.makePlan(Toolchain{}, BuildConfiguration::Debug);
        QVERIFY(plan.compileJobs.isEmpty());
        QVERIFY(!plan.linkJob);
        QVERIFY(buildSync(plan, &result));
        QVERIFY(result.success);
        QVERIFY(result.upToDate);

        // One source changed.
        touchFuture(project.absolutePath("src/util.cpp"));
        plan = project.makePlan(Toolchain{}, BuildConfiguration::Debug);
        QCOMPARE(plan.compileJobs.size(), 1);
        QCOMPARE(plan.compileJobs.at(0).label, QString("util.cpp"));
        QVERIFY(plan.linkJob.has_value());
    }

    void headerChangeRebuildsItsDependents()
    {
        QTemporaryDir dir;
        const Project project = threeFileProject(dir.path());
        BuildResult result;
        QVERIFY(buildSync(project.makePlan(Toolchain{}, BuildConfiguration::Debug), &result));
        QVERIFY(result.success);

        touchFuture(project.absolutePath("util.h"));
        QCOMPARE(project.makePlan(Toolchain{}, BuildConfiguration::Debug).compileJobs.size(), 2);
    }

    void changedFlagsRebuildEverything()
    {
        QTemporaryDir dir;
        Project project = threeFileProject(dir.path());
        BuildResult result;
        QVERIFY(buildSync(project.makePlan(Toolchain{}, BuildConfiguration::Debug), &result));
        QVERIFY(result.success);

        project.defines = {"CHANGED"};
        QCOMPARE(project.makePlan(Toolchain{}, BuildConfiguration::Debug).compileJobs.size(), 2);
    }

    void configurationsBuildSeparately()
    {
        QTemporaryDir dir;
        const Project project = threeFileProject(dir.path());
        BuildResult result;
        QVERIFY(buildSync(project.makePlan(Toolchain{}, BuildConfiguration::Debug), &result));
        const BuildPlan release = project.makePlan(Toolchain{}, BuildConfiguration::Release);
        QCOMPARE(release.compileJobs.size(), 2);
        QVERIFY(release.outputPath.contains("/release/"));
        QVERIFY(release.compileJobs.at(0).command.arguments.contains("-O2"));
        QVERIFY(release.compileJobs.at(0).command.arguments.contains("-DNDEBUG"));
    }

    void buildsStaticAndSharedLibraries()
    {
        QTemporaryDir dir;
        Project project = threeFileProject(dir.path());
        project.sources = {"src/util.cpp"};

        project.targetType = TargetType::StaticLibrary;
        BuildResult result;
        QVERIFY(buildSync(project.makePlan(Toolchain{}, BuildConfiguration::Debug), &result));
        QVERIFY(result.success);
        QVERIFY(result.outputPath.endsWith("/libdemo.a"));
        QVERIFY(QFileInfo::exists(result.outputPath));

        project.targetType = TargetType::SharedLibrary;
        QVERIFY(buildSync(project.makePlan(Toolchain{}, BuildConfiguration::Release), &result));
        QVERIFY(result.success);
        QVERIFY(result.outputPath.endsWith("/libdemo.so"));
        QVERIFY(QFileInfo::exists(result.outputPath));
    }
};

QTEST_GUILESS_MAIN(TestProject)
#include "test_project.moc"
