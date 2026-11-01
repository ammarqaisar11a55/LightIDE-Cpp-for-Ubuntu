#include "compiler/DiagnosticsParser.h"

#include <QtTest>

class TestDiagnostics : public QObject
{
    Q_OBJECT

private slots:
    void errorWithColumn()
    {
        const auto d = DiagnosticsParser::parseLine(
            "main.cpp:5:12: error: 'x' was not declared in this scope");
        QVERIFY(d);
        QCOMPARE(d->severity, Diagnostic::Severity::Error);
        QCOMPARE(d->file, QString("main.cpp"));
        QCOMPARE(d->line, 5);
        QCOMPARE(d->column, 12);
        QCOMPARE(d->message, QString("'x' was not declared in this scope"));
    }

    void warningAndNote()
    {
        auto d = DiagnosticsParser::parseLine("a.c:3:9: warning: unused variable 'y' [-Wunused-variable]");
        QVERIFY(d);
        QCOMPARE(d->severity, Diagnostic::Severity::Warning);
        QVERIFY(d->message.endsWith("[-Wunused-variable]"));

        d = DiagnosticsParser::parseLine("a.c:1:1: note: declared here");
        QVERIFY(d);
        QCOMPARE(d->severity, Diagnostic::Severity::Note);
    }

    void fatalErrorCountsAsError()
    {
        const auto d = DiagnosticsParser::parseLine(
            "main.cpp:1:10: fatal error: missing.h: No such file or directory");
        QVERIFY(d);
        QCOMPARE(d->severity, Diagnostic::Severity::Error);
        QCOMPARE(d->message, QString("missing.h: No such file or directory"));
    }

    void pathWithSpaces()
    {
        const auto d = DiagnosticsParser::parseLine(
            "/home/me/My Projects/hello world.cpp:7:3: error: expected ';'");
        QVERIFY(d);
        QCOMPARE(d->file, QString("/home/me/My Projects/hello world.cpp"));
        QCOMPARE(d->line, 7);
        QCOMPARE(d->column, 3);
    }

    void pathContainingLineLikeSequence()
    {
        const auto d = DiagnosticsParser::parseLine("/tmp/a:1: b/main.cpp:3:5: error: bad");
        QVERIFY(d);
        QCOMPARE(d->file, QString("/tmp/a:1: b/main.cpp"));
        QCOMPARE(d->line, 3);
    }

    void withoutColumn()
    {
        const auto d = DiagnosticsParser::parseLine("gen.cpp:42: error: stray token");
        QVERIFY(d);
        QCOMPARE(d->line, 42);
        QCOMPARE(d->column, 0);
    }

    void messageQuotingAnotherLocationStaysInMessage()
    {
        const auto d = DiagnosticsParser::parseLine(
            "a.cpp:2:1: error: redefinition, see b.cpp:9:1: note: first here");
        QVERIFY(d);
        QCOMPARE(d->file, QString("a.cpp"));
        QCOMPARE(d->line, 2);
    }

    void toolMessagesHaveNoLocation()
    {
        auto d = DiagnosticsParser::parseLine("collect2: error: ld returned 1 exit status");
        QVERIFY(d);
        QCOMPARE(d->severity, Diagnostic::Severity::Error);
        QVERIFY(!d->hasLocation());

        d = DiagnosticsParser::parseLine("cc1plus: fatal error: x.cpp: No such file or directory");
        QVERIFY(d);
        QVERIFY(d->file.isEmpty());
    }

    void undefinedReferenceWithLine()
    {
        const auto d = DiagnosticsParser::parseLine(
            "/usr/bin/ld: /tmp/cc1.o:/home/me/my app/main.cpp:5:(.text+0x9): undefined reference to `f()'");
        QVERIFY(d);
        QCOMPARE(d->file, QString("/home/me/my app/main.cpp"));
        QCOMPARE(d->line, 5);
        QCOMPARE(d->message, QString("undefined reference to `f()'"));
    }

    void undefinedReferenceWithoutDebugInfo()
    {
        const auto d = DiagnosticsParser::parseLine(
            "main.cpp:(.text+0x9): undefined reference to `f()'");
        QVERIFY(d);
        QCOMPARE(d->file, QString("main.cpp"));
        QCOMPARE(d->line, 0);
        QVERIFY(!d->hasLocation());
    }

    void linkerLibraryError()
    {
        const auto d = DiagnosticsParser::parseLine(
            "/usr/bin/ld: cannot find -lnope: No such file or directory");
        QVERIFY(d);
        QCOMPARE(d->severity, Diagnostic::Severity::Error);
        QCOMPARE(d->message, QString("cannot find -lnope: No such file or directory"));
    }

    void linkerContextLineIsIgnored()
    {
        QVERIFY(!DiagnosticsParser::parseLine("/usr/bin/ld: /tmp/cc1.o: in function `main':"));
    }

    void contextLinesAreIgnored()
    {
        QVERIFY(!DiagnosticsParser::parseLine("main.cpp: In function 'int main()':"));
        QVERIFY(!DiagnosticsParser::parseLine("In file included from a.h:1,"));
        QVERIFY(!DiagnosticsParser::parseLine("    5 |     int x"));
        QVERIFY(!DiagnosticsParser::parseLine("      |         ^"));
        QVERIFY(!DiagnosticsParser::parseLine(""));
    }

    void parsesWholeOutput()
    {
        const QString output = "main.cpp: In function 'int main()':\n"
                               "main.cpp:3:5: warning: unused [-Wunused]\n"
                               "    3 |     int x;\n"
                               "main.cpp:4:1: error: oops\n";
        const auto diagnostics = DiagnosticsParser::parse(output);
        QCOMPARE(diagnostics.size(), 2);
        QCOMPARE(diagnostics.at(1).line, 4);
    }
};

QTEST_APPLESS_MAIN(TestDiagnostics)
#include "test_diagnostics.moc"
