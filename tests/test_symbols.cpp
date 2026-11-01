#include "core/SymbolScanner.h"

#include <QtTest>

class TestSymbols : public QObject
{
    Q_OBJECT

private:
    static QStringList names(const QString &text)
    {
        QStringList result;
        const QList<Symbol> symbols = SymbolScanner::scan(text);
        for (const Symbol &symbol : symbols)
            result << symbol.name;
        return result;
    }

private slots:
    void findsFunctions()
    {
        const QString text = "#include <vector>\n"
                             "\n"
                             "int add(int a, int b) {\n"
                             "    return a + b;\n"
                             "}\n"
                             "\n"
                             "static std::vector<int> make()\n"
                             "{\n"
                             "    return {};\n"
                             "}\n";
        const QList<Symbol> symbols = SymbolScanner::scan(text);
        QCOMPARE(symbols.size(), 2);
        QCOMPARE(symbols.at(0).name, QString("add"));
        QCOMPARE(symbols.at(0).line, 3);
        QCOMPARE(symbols.at(1).name, QString("make"));
        QCOMPARE(symbols.at(1).line, 7);
    }

    void findsTypes()
    {
        const QList<Symbol> symbols = SymbolScanner::scan("class A {};\n"
                                                          "struct B : A {};\n"
                                                          "enum class C { X };\n"
                                                          "namespace d {\n"
                                                          "}\n");
        QCOMPARE(symbols.size(), 4);
        QCOMPARE(symbols.at(0).kind, Symbol::Kind::Class);
        QCOMPARE(symbols.at(1).kind, Symbol::Kind::Struct);
        QCOMPARE(symbols.at(1).name, QString("B"));
        QCOMPARE(symbols.at(2).kind, Symbol::Kind::Enum);
        QCOMPARE(symbols.at(2).name, QString("C"));
        QCOMPARE(symbols.at(3).kind, Symbol::Kind::Namespace);
    }

    void findsQualifiedMemberDefinitions()
    {
        QCOMPARE(names("void Widget::paint() const {\n}\n"), QStringList({"Widget::paint"}));
        QCOMPARE(names("Widget::~Widget() {\n}\n"), QStringList({"Widget::~Widget"}));
    }

    void ignoresDeclarations()
    {
        QVERIFY(names("int add(int a, int b);\nclass Forward;\nextern int x;\n").isEmpty());
    }

    void ignoresControlFlow()
    {
        const QStringList found = names("int main() {\n"
                                        "    if (x) {\n"
                                        "    }\n"
                                        "    for (;;) {\n"
                                        "    }\n"
                                        "    while (y) {\n"
                                        "    }\n"
                                        "}\n");
        QCOMPARE(found, QStringList({"main"}));
    }

    void ignoresCommentsAndStrings()
    {
        QVERIFY(names("// int fake() {\n/* class Fake { */\nconst char *s = \"void f() {\";\n")
                    .isEmpty());
    }

    void lineNumbersSurviveMultilineComments()
    {
        const QList<Symbol> symbols = SymbolScanner::scan("/* one\n two\n three */\nvoid f() {\n}\n");
        QCOMPARE(symbols.size(), 1);
        QCOMPARE(symbols.at(0).line, 4);
    }

    void findsDefinitionByName()
    {
        const QString text = "int f();\n\nint f() {\n    return 1;\n}\n\nvoid S::g() {\n}\n";
        QCOMPARE(SymbolScanner::findDefinition(text, "f"), 3);
        QCOMPARE(SymbolScanner::findDefinition(text, "g"), 7);
        QCOMPARE(SymbolScanner::findDefinition(text, "missing"), 0);
    }
};

QTEST_APPLESS_MAIN(TestSymbols)
#include "test_symbols.moc"
