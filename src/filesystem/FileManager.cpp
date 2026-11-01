#include "filesystem/FileManager.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>

namespace {

const QStringList sourceExtensions = {QStringLiteral("c"), QStringLiteral("cpp"),
                                      QStringLiteral("cc"), QStringLiteral("cxx")};
const QStringList headerExtensions = {QStringLiteral("h"), QStringLiteral("hpp"),
                                      QStringLiteral("hh"), QStringLiteral("hxx")};

QString tr(const char *text)
{
    return QCoreApplication::translate("FileManager", text);
}

} // namespace

namespace FileManager {

bool load(const QString &path, LoadedFile *file, QString *error)
{
    QFile input(path);
    if (input.size() > maxFileSize) {
        *error = tr("The file is larger than 64 MB.");
        return false;
    }
    if (!input.open(QIODevice::ReadOnly)) {
        *error = input.errorString();
        return false;
    }

    QByteArray bytes = input.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);
    if (bytes.contains('\0')) {
        *error = tr("This looks like a binary file.");
        return false;
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder.decode(bytes);
    file->lossyEncoding = decoder.hasError();
    if (file->lossyEncoding)
        text = QString::fromLatin1(bytes);

    file->eol = detectEol(text);
    file->text = std::move(text);
    return true;
}

bool save(const QString &path, const QString &text, QString *error)
{
    QSaveFile output(path);
    // Falls back to writing in place where a temporary sibling cannot be
    // created or renamed, such as in a directory that is not writable.
    output.setDirectWriteFallback(true);
    if (!output.open(QIODevice::WriteOnly)) {
        *error = output.errorString();
        return false;
    }

    const QByteArray bytes = text.toUtf8();
    if (output.write(bytes) != bytes.size() || !output.commit()) {
        *error = output.errorString();
        return false;
    }
    return true;
}

EndOfLine detectEol(const QString &text)
{
    const qsizetype newline = text.indexOf(QLatin1Char('\n'));
    const qsizetype carriageReturn = text.indexOf(QLatin1Char('\r'));
    if (carriageReturn < 0)
        return EndOfLine::Unix;
    if (newline == carriageReturn + 1)
        return EndOfLine::Windows;
    if (newline < 0 || carriageReturn < newline)
        return EndOfLine::ClassicMac;
    return EndOfLine::Unix;
}

bool isSourceFile(const QString &path)
{
    return sourceExtensions.contains(QFileInfo(path).suffix().toLower());
}

bool isHeaderFile(const QString &path)
{
    return headerExtensions.contains(QFileInfo(path).suffix().toLower());
}

bool isSupportedFile(const QString &path)
{
    return isSourceFile(path) || isHeaderFile(path);
}

QStringList supportedPatterns()
{
    QStringList patterns;
    for (const QString &extension : sourceExtensions + headerExtensions)
        patterns << QStringLiteral("*.") + extension;
    return patterns;
}

QString openFileFilter()
{
    return tr("C/C++ files (%1);;All files (*)")
        .arg(supportedPatterns().join(QLatin1Char(' ')));
}

QString cTemplate()
{
    return QStringLiteral("#include <stdio.h>\n"
                          "\n"
                          "int main() {\n"
                          "    printf(\"Hello, World!\\n\");\n"
                          "    return 0;\n"
                          "}\n");
}

QString cppTemplate()
{
    return QStringLiteral("#include <iostream>\n"
                          "using namespace std;\n"
                          "\n"
                          "int main() {\n"
                          "    cout << \"Hello, World!\" << endl;\n"
                          "    return 0;\n"
                          "}\n");
}

} // namespace FileManager
