#pragma once

#include <QList>
#include <QString>

// A parsed C++ type, such as const vector<vector<int>>&.
struct CppType
{
    QString name;   // Base name without std::, such as long long, vector or ListNode.
    QList<CppType> templateArgs;
    int pointerDepth = 0;
    bool isReference = false;
    bool isConst = false;

    bool isInteger() const;
    bool isFloating() const;
    bool isString() const;
    bool isVector() const;
    bool isNodePointer(const QString &nodeName) const;
};

struct CppParam
{
    QString name;
    CppType type;
    QString typeText;   // Exactly as written in the starter code.
};

struct CppMethod
{
    QString name;
    bool hasReturnType = false;
    CppType returnType;
    QString returnTypeText;
    QList<CppParam> params;
    bool isConstructor = false;
    bool isStatic = false;
};

struct CppClass
{
    QString name;
    QList<CppMethod> methods;
};

// A type definition LeetCode ships inside a comment, with the decoration removed.
struct CommentedDefinition
{
    QString name;
    QString code;
};

// What LeetCode's C++ starter code declares.
struct CppStarter
{
    QString code;
    QList<CppClass> classes;
    QList<CommentedDefinition> commentedDefinitions;
};

namespace CppSignature {

CppStarter analyzeStarterCode(const QString &code);

CppType parseType(const QString &text);

// With asValue, drops top-level const and reference: a parameter type becomes a variable type.
QString formatType(const CppType &type, bool asValue = false);

} // namespace CppSignature
