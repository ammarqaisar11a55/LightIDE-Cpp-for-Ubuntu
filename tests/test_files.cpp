#include "filesystem/FileManager.h"
#include "filesystem/FileSearch.h"

#include <QtTest>

class TestFiles : public QObject
{
    Q_OBJECT

private:
    static void writeBytes(const QString &path, const QByteArray &bytes)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bytes);
    }

private slots:
    void roundTripPreservesEveryByte()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("a b.cpp");
        const QString text = QString::fromUtf8("int café;\r\nint x;\r\n");
        QString error;
        QVERIFY2(FileManager::save(path, text, &error), qPrintable(error));

        LoadedFile loaded;
        QVERIFY2(FileManager::load(path, &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.text, text);
        QCOMPARE(loaded.eol, EndOfLine::Windows);
        QVERIFY(!loaded.lossyEncoding);
    }

    void saveKeepsPermissions()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("script.c");
        writeBytes(path, "old");
        QFile::setPermissions(path, QFile::permissions(path) | QFile::ExeOwner);
        QString error;
        QVERIFY(FileManager::save(path, "new", &error));
        QVERIFY(QFile::permissions(path) & QFile::ExeOwner);
    }

    void stripsBom()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("a.c");
        writeBytes(path, "\xEF\xBB\xBFint x;");
        LoadedFile loaded;
        QString error;
        QVERIFY(FileManager::load(path, &loaded, &error));
        QCOMPARE(loaded.text, QString("int x;"));
    }

    void fallsBackToLatin1()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("a.c");
        writeBytes(path, "// caf\xE9\n");
        LoadedFile loaded;
        QString error;
        QVERIFY(FileManager::load(path, &loaded, &error));
        QVERIFY(loaded.lossyEncoding);
        QCOMPARE(loaded.text, QString::fromUtf8("// café\n"));
    }

    void refusesBinaryFiles()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("a.c");
        writeBytes(path, QByteArray("\x7f" "ELF\0\0\0", 7));
        LoadedFile loaded;
        QString error;
        QVERIFY(!FileManager::load(path, &loaded, &error));
        QVERIFY(!error.isEmpty());
    }

    void missingFileReportsError()
    {
        LoadedFile loaded;
        QString error;
        QVERIFY(!FileManager::load("/nonexistent/x.c", &loaded, &error));
        QVERIFY(!error.isEmpty());
    }

    void detectsLineEndings()
    {
        QCOMPARE(FileManager::detectEol("a\nb\n"), EndOfLine::Unix);
        QCOMPARE(FileManager::detectEol("a\r\nb\r\n"), EndOfLine::Windows);
        QCOMPARE(FileManager::detectEol("a\rb\r"), EndOfLine::ClassicMac);
        QCOMPARE(FileManager::detectEol("no newline"), EndOfLine::Unix);
    }

    void classifiesExtensions()
    {
        for (const char *name : {"a.c", "a.cpp", "a.cc", "a.cxx", "A.CPP"})
            QVERIFY2(FileManager::isSourceFile(name), name);
        for (const char *name : {"a.h", "a.hpp", "a.hh", "a.hxx"})
            QVERIFY2(FileManager::isHeaderFile(name), name);
        QVERIFY(!FileManager::isSupportedFile("a.txt"));
        QCOMPARE(FileManager::supportedPatterns().size(), 8);
    }

    void templatesMatchTheSpecification()
    {
        QVERIFY(FileManager::cTemplate().contains("printf(\"Hello, World!\\n\");"));
        QVERIFY(FileManager::cppTemplate().contains("cout << \"Hello, World!\" << endl;"));
    }

    void findInFiles()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("src");
        QDir(dir.path()).mkpath("build");
        QDir(dir.path()).mkpath(".git");
        writeBytes(dir.filePath("src/a.cpp"), "int alpha;\nint beta;\n  int alpha2;\n");
        writeBytes(dir.filePath("src/b.h"), "// ALPHA\n");
        writeBytes(dir.filePath("build/gen.cpp"), "int alpha;\n");
        writeBytes(dir.filePath(".git/x.cpp"), "int alpha;\n");
        writeBytes(dir.filePath("notes.txt"), "alpha\n");

        FileSearch search;
        QList<FileMatch> matches;
        QObject::connect(&search, &FileSearch::matchesFound, this,
                         [&](const QList<FileMatch> &batch) { matches += batch; });
        QSignalSpy finished(&search, &FileSearch::searchFinished);

        search.search(dir.path(), "alpha", true, false);
        QVERIFY(finished.wait(5000));
        QCOMPARE(matches.size(), 2);
        std::sort(matches.begin(), matches.end(),
                  [](const FileMatch &a, const FileMatch &b) { return a.line < b.line; });
        QCOMPARE(matches.at(0).line, 1);
        QCOMPARE(matches.at(0).column, 5);
        QCOMPARE(matches.at(1).text, QString("int alpha2;"));

        matches.clear();
        search.search(dir.path(), "alpha", false, true);
        QVERIFY(finished.wait(5000));
        QCOMPARE(matches.size(), 2);   // "alpha" and "ALPHA", not "alpha2".
    }
};

QTEST_GUILESS_MAIN(TestFiles)
#include "test_files.moc"
