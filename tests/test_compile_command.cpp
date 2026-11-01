#include "compiler/CompileCommand.h"
#include "compiler/CompilerManager.h"

#include <QtTest>

class TestCompileCommand : public QObject
{
    Q_OBJECT

private slots:
    void cppUsesGxxAndCxxStandard()
    {
        BuildOptions options;
        options.cxxStandard = "c++20";
        const CommandLine command =
            CompileCommand::singleFile(Toolchain{}, options, "/w/main.cpp", "/w/main");
        QCOMPARE(command.program, QString("g++"));
        QVERIFY(command.arguments.contains("-std=c++20"));
        QVERIFY(command.arguments.contains("-g"));
        QVERIFY(command.arguments.contains("-Wall"));
        QVERIFY(command.arguments.contains("-fmessage-length=0"));
        const int output = command.arguments.indexOf("-o");
        QVERIFY(output > 0);
        QCOMPARE(command.arguments.at(output - 1), QString("/w/main.cpp"));
        QCOMPARE(command.arguments.at(output + 1), QString("/w/main"));
    }

    void cUsesGccAndCStandard()
    {
        BuildOptions options;
        options.cStandard = "c11";
        const CommandLine command =
            CompileCommand::singleFile(Toolchain{}, options, "/w/a.c", "/w/a");
        QCOMPARE(command.program, QString("gcc"));
        QVERIFY(command.arguments.contains("-std=c11"));
    }

    void includeAndLibraryOptions()
    {
        BuildOptions options;
        options.includeDirs = {"/inc dir"};
        options.libraryDirs = {"/lib dir"};
        options.libraries = {"m"};
        options.extraArgs = {"-pthread"};
        const QStringList args =
            CompileCommand::singleFile(Toolchain{}, options, "a.cpp", "a").arguments;
        QVERIFY(args.contains("-I/inc dir"));
        QVERIFY(args.contains("-L/lib dir"));
        QVERIFY(args.contains("-lm"));
        QVERIFY(args.contains("-pthread"));
        // Libraries must follow the source for the linker to resolve symbols.
        QVERIFY(args.indexOf("-lm") > args.indexOf("a.cpp"));
    }

    void objectWritesDependencies()
    {
        const QStringList args =
            CompileCommand::object(Toolchain{}, BuildOptions{}, "a.cpp", "a.cpp.o").arguments;
        QVERIFY(args.contains("-MMD"));
        QVERIFY(args.contains("-MP"));
        QVERIFY(args.contains("-c"));
    }

    void linkChoosesDriverByLanguage()
    {
        QCOMPARE(CompileCommand::link(Toolchain{}, {}, true, {"a.o"}, "a", false).program,
                 QString("g++"));
        QCOMPARE(CompileCommand::link(Toolchain{}, {}, false, {"a.o"}, "a", false).program,
                 QString("gcc"));
        QVERIFY(CompileCommand::link(Toolchain{}, {}, true, {"a.o"}, "liba.so", true)
                    .arguments.contains("-shared"));
    }

    void languageFromExtension()
    {
        QCOMPARE(CompileCommand::languageOf("a.c"), SourceLanguage::C);
        QCOMPARE(CompileCommand::languageOf("a.cpp"), SourceLanguage::Cpp);
        QCOMPARE(CompileCommand::languageOf("a.cc"), SourceLanguage::Cpp);
        QCOMPARE(CompileCommand::languageOf("a.cxx"), SourceLanguage::Cpp);
        QCOMPARE(CompileCommand::languageOf("a.C"), SourceLanguage::Cpp);
    }

    void outputPaths()
    {
        QCOMPARE(CompileCommand::outputPathFor("/w/hello world.cpp", ""),
                 QString("/w/hello world"));
        QCOMPARE(CompileCommand::outputPathFor("/w/a.cpp", "bin"), QString("/w/bin/a"));
        QCOMPARE(CompileCommand::outputPathFor("/w/a.cpp", "/out"), QString("/out/a"));
        QCOMPARE(CompileCommand::outputPathFor("/w/a.cpp", "", "tool"), QString("/w/tool"));
        QCOMPARE(CompileCommand::outputPathFor("/w/a.b.cpp", ""), QString("/w/a.b"));
    }

    void displayQuotesForTheShell()
    {
        const CommandLine command{"g++", {"-c", "my file.cpp", "it's.cpp"}};
        QCOMPARE(command.display(), QString("g++ -c 'my file.cpp' 'it'\\''s.cpp'"));
    }

    void splitsQuotedArguments()
    {
        QCOMPARE(CompileCommand::splitArguments("-DA=1 \"-I/a b\" -lm"),
                 QStringList({"-DA=1", "-I/a b", "-lm"}));
        QVERIFY(CompileCommand::splitArguments("").isEmpty());
    }

    void parsesToolVersions()
    {
        QCOMPARE(CompilerManager::parseVersion("gcc (Ubuntu 13.2.0-23ubuntu4) 13.2.0\nCopyright"),
                 QString("13.2.0"));
        QCOMPARE(CompilerManager::parseVersion("GNU gdb (Ubuntu 15.0.50-0ubuntu1) 15.0.50.20240403-git"),
                 QString("15.0.50.20240403"));
        QCOMPARE(CompilerManager::parseVersion("GNU gdb (GDB) 17.1\n"), QString("17.1"));
    }
};

QTEST_APPLESS_MAIN(TestCompileCommand)
#include "test_compile_command.moc"
