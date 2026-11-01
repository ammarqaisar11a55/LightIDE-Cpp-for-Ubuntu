#include "debugger/MiParser.h"

#include <QtTest>

class TestMiParser : public QObject
{
    Q_OBJECT

private slots:
    void prompt()
    {
        QCOMPARE(MiParser::parse("(gdb) ").type, MiRecord::Type::Prompt);
    }

    void resultWithToken()
    {
        const MiRecord record = MiParser::parse("12^done,value=\"42\"");
        QCOMPARE(record.type, MiRecord::Type::Result);
        QCOMPARE(record.token, 12);
        QCOMPARE(record.resultClass, QString("done"));
        QCOMPARE(record.payload.text("value"), QString("42"));
    }

    void resultWithoutPayload()
    {
        const MiRecord record = MiParser::parse("^running");
        QCOMPARE(record.token, -1);
        QCOMPARE(record.resultClass, QString("running"));
        QVERIFY(record.payload.entries.isEmpty());
    }

    void errorMessage()
    {
        const MiRecord record = MiParser::parse("5^error,msg=\"No symbol \\\"x\\\" in context.\"");
        QCOMPARE(record.resultClass, QString("error"));
        QCOMPARE(record.payload.text("msg"), QString("No symbol \"x\" in context."));
    }

    void stoppedRecordWithFrame()
    {
        const MiRecord record = MiParser::parse(
            "*stopped,reason=\"breakpoint-hit\",bkptno=\"1\",frame={addr=\"0x1\",func=\"main\","
            "args=[],file=\"a.cpp\",fullname=\"/w/a b/a.cpp\",line=\"7\"},thread-id=\"1\"");
        QCOMPARE(record.type, MiRecord::Type::ExecAsync);
        QCOMPARE(record.resultClass, QString("stopped"));
        QCOMPARE(record.payload.text("reason"), QString("breakpoint-hit"));
        const MiValue *frame = record.payload.find("frame");
        QVERIFY(frame);
        QCOMPARE(frame->kind, MiValue::Kind::Tuple);
        QCOMPARE(frame->text("fullname"), QString("/w/a b/a.cpp"));
        QCOMPARE(frame->text("line"), QString("7"));
        QCOMPARE(record.payload.text("thread-id"), QString("1"));
    }

    void listOfNamedResults()
    {
        const MiRecord record = MiParser::parse(
            "3^done,stack=[frame={level=\"0\",func=\"f\"},frame={level=\"1\",func=\"main\"}]");
        const MiValue *stack = record.payload.find("stack");
        QVERIFY(stack);
        QCOMPARE(stack->kind, MiValue::Kind::List);
        const auto frames = stack->items();
        QCOMPARE(frames.size(), 2);
        QCOMPARE(frames.at(1).text("func"), QString("main"));
    }

    void listOfTuples()
    {
        const MiRecord record = MiParser::parse(
            "4^done,variables=[{name=\"a\",arg=\"1\",value=\"1\"},{name=\"b\",value=\"{1, 2}\"}]");
        const auto variables = record.payload.find("variables")->items();
        QCOMPARE(variables.size(), 2);
        QCOMPARE(variables.at(0).text("arg"), QString("1"));
        QCOMPARE(variables.at(1).text("value"), QString("{1, 2}"));
    }

    void listOfStrings()
    {
        const MiRecord record = MiParser::parse("^done,items=[\"a\",\"b\"]");
        const auto items = record.payload.find("items")->items();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.at(1).string, QString("b"));
    }

    void emptyContainers()
    {
        const MiRecord record = MiParser::parse("^done,a=[],b={}");
        QCOMPARE(record.payload.find("a")->kind, MiValue::Kind::List);
        QCOMPARE(record.payload.find("b")->kind, MiValue::Kind::Tuple);
    }

    void streamRecords()
    {
        const MiRecord record = MiParser::parse("~\"line one\\n\\ttabbed\\\"\\\\\"");
        QCOMPARE(record.type, MiRecord::Type::ConsoleStream);
        QCOMPARE(record.stream, QString("line one\n\ttabbed\"\\"));
        QCOMPARE(MiParser::parse("&\"x\"").type, MiRecord::Type::LogStream);
        QCOMPARE(MiParser::parse("@\"x\"").type, MiRecord::Type::TargetStream);
    }

    void octalEscapedUtf8()
    {
        const MiRecord record = MiParser::parse("~\"caf\\303\\251\"");
        QCOMPARE(record.stream, QString::fromUtf8("café"));
    }

    void notifyRecord()
    {
        const MiRecord record = MiParser::parse("=thread-group-added,id=\"i1\"");
        QCOMPARE(record.type, MiRecord::Type::NotifyAsync);
        QCOMPARE(record.resultClass, QString("thread-group-added"));
    }

    void unknownLinesDoNotCrash()
    {
        QCOMPARE(MiParser::parse("random text from a program").type, MiRecord::Type::Unknown);
        QCOMPARE(MiParser::parse("").type, MiRecord::Type::Unknown);
        MiParser::parse("^done,broken={a=\"1\"");
        MiParser::parse("~\"unterminated");
    }

    void quotingCommandArguments()
    {
        QCOMPARE(MiParser::quote("/a b/c\"d\\e"), QString("\"/a b/c\\\"d\\\\e\""));
    }
};

QTEST_APPLESS_MAIN(TestMiParser)
#include "test_mi_parser.moc"
