#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

// A value from GDB/MI output: a C string, a {tuple} or a [list].
struct MiValue
{
    enum class Kind { String, Tuple, List };

    Kind kind = Kind::String;
    QString string;
    // Tuples and lists of results hold name=value pairs. Plain lists leave the name empty.
    QList<QPair<QString, MiValue>> entries;

    const MiValue *find(const QString &name) const;
    QString text(const QString &name) const;   // Empty when absent.
    QList<MiValue> items() const;              // Values of a list, ignoring names.
};

struct MiRecord
{
    enum class Type {
        Unknown,
        Prompt,         // (gdb)
        Result,         // ^done, ^running, ^error, ^connected, ^exit
        ExecAsync,      // *stopped, *running
        StatusAsync,    // +download
        NotifyAsync,    // =thread-created
        ConsoleStream,  // ~"text"
        TargetStream,   // @"text"
        LogStream,      // &"text"
    };

    Type type = Type::Unknown;
    int token = -1;
    QString resultClass;
    MiValue payload;   // A tuple of the record's results.
    QString stream;    // Decoded text of stream records.
    QString raw;
};

class MiParser
{
public:
    static MiRecord parse(const QByteArray &line);

    // Quotes a string for use as an MI command argument.
    static QString quote(const QString &text);
};
