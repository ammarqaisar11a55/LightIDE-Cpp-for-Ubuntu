#include "leetcode/HarnessGenerator.h"

#include "leetcode/QuestionParser.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {

const QString I1 = QStringLiteral("    ");
const QString I2 = QStringLiteral("        ");

QString abbreviate(const QString &text, int maximum)
{
    QString single = text;
    static const QRegularExpression whitespace(QStringLiteral(R"(\s+)"));
    single.replace(whitespace, QStringLiteral(" "));
    return single.size() > maximum ? single.left(maximum - 3) + QStringLiteral("...") : single;
}

[[noreturn]] void mismatch(const CppType &type, const LcValue &value)
{
    const QString shown = toLeetCodeText(value);
    throw LcError(QStringLiteral("Value %1 does not fit C++ type \"%2\".")
                      .arg(shown.size() > 60 ? shown.left(57) + QStringLiteral("...") : shown,
                           CppSignature::formatType(type, true)));
}

QString cppCharLiteral(const QString &c)
{
    if (c == u"'")
        return QStringLiteral("'\\''");
    if (c == u"\\")
        return QStringLiteral("'\\\\'");
    if (c == u"\n")
        return QStringLiteral("'\\n'");
    if (c == u"\t")
        return QStringLiteral("'\\t'");
    if (c == u"\r")
        return QStringLiteral("'\\r'");
    if (c == QString(QChar()))
        return QStringLiteral("'\\0'");
    return QLatin1Char('\'') + c + QLatin1Char('\'');
}

bool fitsInt32(const QString &text)
{
    bool negative = text.startsWith(u'-');
    QString digits = text;
    digits.remove(QRegularExpression(QStringLiteral(R"(^[-+]?0*)")));
    if (digits.size() > 10)
        return false;
    const qlonglong magnitude = digits.toLongLong();
    return magnitude <= (negative ? 2147483648LL : 2147483647LL);
}

QString integerLiteral(const CppType &type, const LcValue &value)
{
    if (value.kind == LcValue::Kind::Bool)
        return value.boolean ? QStringLiteral("1") : QStringLiteral("0");
    if (!value.isNumber() || !lcIsIntegerText(value.text))
        mismatch(type, value);

    const QString text = value.text.startsWith(u'+') ? value.text.mid(1) : value.text;
    if (fitsInt32(text))
        return text;
    const bool isUnsigned = type.name.startsWith(QLatin1String("unsigned"))
                            || type.name.startsWith(QLatin1String("uint")) || type.name == u"size_t";
    return text + (isUnsigned ? QStringLiteral("ULL") : QStringLiteral("LL"));
}

QString floatingLiteral(const CppType &type, const LcValue &value)
{
    if (!value.isNumber())
        mismatch(type, value);
    const QString text = value.text.startsWith(u'+') ? value.text.mid(1) : value.text;
    static const QRegularExpression hasPoint(QStringLiteral("[.eE]"));
    return text.contains(hasPoint) ? text : text + QStringLiteral(".0");
}

QString boolLiteral(const CppType &type, const LcValue &value)
{
    if (value.kind == LcValue::Kind::Bool)
        return value.boolean ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isNumber() && (value.text == u"0" || value.text == u"1"))
        return value.text == u"1" ? QStringLiteral("true") : QStringLiteral("false");
    mismatch(type, value);
}

QString charLiteral(const CppType &type, const LcValue &value)
{
    if ((value.kind == LcValue::Kind::String || value.kind == LcValue::Kind::Symbol)
        && value.text.toUcs4().size() == 1) {
        return cppCharLiteral(value.text);
    }
    if (value.isNumber() && value.text.size() == 1 && value.text.at(0).isDigit())
        return cppCharLiteral(value.text);
    mismatch(type, value);
}

std::optional<QStringList> nodeValues(const CppType &type, const LcValue &value, bool allowNull)
{
    if (value.isNull())
        return std::nullopt;
    if (!value.isArray())
        mismatch(type, value);

    QStringList values;
    for (const LcValue &item : value.items) {
        if (item.isNull() && allowNull)
            values << QStringLiteral("lc::null");
        else if (item.isNumber() && lcIsIntegerText(item.text))
            values << item.text;
        else
            mismatch(type, value);
    }
    return values;
}

// Whether LeetCode's type is represented by the C++ type. This is how problems
// are spotted whose judge inputs don't match the method signature.
bool isCompatible(const QString &metaType, const CppType &cpp)
{
    const QString element = HarnessGenerator::metaElementType(metaType);
    if (!element.isEmpty())
        return cpp.isVector() && isCompatible(element, cpp.templateArgs.first());

    const QString name = metaType.trimmed().toLower();
    if (name == u"integer" || name == u"long")
        return cpp.isInteger();
    if (name == u"double" || name == u"float")
        return cpp.isFloating();
    if (name == u"boolean")
        return cpp.pointerDepth == 0 && cpp.name == u"bool";
    if (name == u"character")
        return cpp.pointerDepth == 0 && cpp.name == u"char";
    if (name == u"string")
        return cpp.isString();
    if (name == u"void")
        return cpp.pointerDepth == 0 && cpp.name == u"void";
    if (name == u"listnode")
        return cpp.isNodePointer(QStringLiteral("ListNode"));
    if (name == u"treenode")
        return cpp.isNodePointer(QStringLiteral("TreeNode"));
    return false;
}

// Best-effort C++ spelling, for manual harness skeletons. Empty if there is none.
QString metaTypeToCpp(const QString &metaType)
{
    const QString element = HarnessGenerator::metaElementType(metaType);
    if (!element.isEmpty()) {
        const QString inner = metaTypeToCpp(element);
        return inner.isEmpty() ? QString() : QStringLiteral("vector<%1>").arg(inner);
    }

    const QString name = metaType.trimmed().toLower();
    if (name == u"integer")
        return QStringLiteral("int");
    if (name == u"long")
        return QStringLiteral("long long");
    if (name == u"double")
        return QStringLiteral("double");
    if (name == u"float")
        return QStringLiteral("float");
    if (name == u"boolean")
        return QStringLiteral("bool");
    if (name == u"character")
        return QStringLiteral("char");
    if (name == u"string")
        return QStringLiteral("string");
    if (name == u"listnode")
        return QStringLiteral("ListNode*");
    if (name == u"treenode")
        return QStringLiteral("TreeNode*");
    return {};
}

QString describeMismatch(const QStringList &metaTypes, const CppMethod &method)
{
    if (metaTypes.size() != method.params.size()) {
        return QStringLiteral("The judge passes %1 input(s) but %2() takes %3 parameter(s).")
            .arg(metaTypes.size())
            .arg(method.name)
            .arg(method.params.size());
    }
    for (qsizetype i = 0; i < metaTypes.size(); ++i) {
        const CppParam &param = method.params.at(i);
        if (!isCompatible(metaTypes.at(i), param.type)) {
            return QStringLiteral("Input %1 is \"%2\" but parameter \"%3\" is \"%4\".")
                .arg(i + 1)
                .arg(metaTypes.at(i), param.name, CppSignature::formatType(param.type));
        }
    }
    return {};
}

QStringList typesOf(const QList<MetaParam> &params)
{
    QStringList types;
    for (const MetaParam &param : params)
        types << param.type;
    return types;
}

const CppMethod *findMethod(const CppClass &cls, const QString &name)
{
    for (const CppMethod &method : cls.methods) {
        if (method.name == name && !method.isConstructor)
            return &method;
    }
    return nullptr;
}

HarnessPlan manualPlan(const QString &className, const QString &reason)
{
    HarnessPlan plan;
    plan.kind = HarnessPlan::Kind::Manual;
    plan.className = className;
    plan.reason = reason;
    return plan;
}

HarnessPlan planFunction(const ProblemMeta &meta, const CppStarter &starter)
{
    const CppClass *owner = nullptr;
    for (const CppClass &cls : starter.classes) {
        if (cls.name == u"Solution" && std::any_of(cls.methods.cbegin(), cls.methods.cend(),
                                                   [&](const CppMethod &m) { return m.name == meta.name; })) {
            owner = &cls;
            break;
        }
    }
    if (!owner) {
        for (const CppClass &cls : starter.classes) {
            if (findMethod(cls, meta.name)) {
                owner = &cls;
                break;
            }
        }
    }

    if (!owner) {
        if (starter.classes.isEmpty())
            return manualPlan({}, QStringLiteral("The starter code does not declare a class."));
        const QString name = starter.classes.first().name;
        return manualPlan(name, QStringLiteral("The starter class \"%1\" has no method named \"%2\" "
                                               "(the judge uses a custom driver).")
                                    .arg(name, meta.name));
    }

    const CppMethod *method = findMethod(*owner, meta.name);
    const QString problem = describeMismatch(typesOf(meta.params), *method);
    if (!problem.isEmpty()) {
        HarnessPlan plan = manualPlan(owner->name, problem);
        plan.hasMethod = true;
        plan.method = *method;
        return plan;
    }
    if (meta.returnType != u"void" && method->hasReturnType
        && !isCompatible(meta.returnType, method->returnType) && meta.manual) {
        HarnessPlan plan = manualPlan(owner->name, QStringLiteral("The judge reports \"%1\" but the "
                                                                  "method returns \"%2\".")
                                                       .arg(meta.returnType, method->returnTypeText));
        plan.hasMethod = true;
        plan.method = *method;
        return plan;
    }

    HarnessPlan plan;
    plan.kind = HarnessPlan::Kind::Function;
    plan.className = owner->name;
    plan.hasMethod = true;
    plan.method = *method;
    if (meta.hasOutput && meta.outputParamIndex >= 0 && meta.outputParamIndex < method->params.size()) {
        plan.hasOutput = true;
        plan.outputParamIndex = meta.outputParamIndex;
        plan.prefixFromReturn = meta.outputSize == u"ret";
    }
    return plan;
}

HarnessPlan planDesign(const ProblemMeta &meta, const CppStarter &starter)
{
    const auto cls = std::find_if(starter.classes.cbegin(), starter.classes.cend(),
                                  [&](const CppClass &c) { return c.name == meta.name; });
    if (cls == starter.classes.cend())
        return manualPlan({}, QStringLiteral("The starter code has no class named \"%1\".").arg(meta.name));

    HarnessPlan plan;
    plan.kind = HarnessPlan::Kind::Design;
    plan.className = cls->name;

    const auto constructor = std::find_if(cls->methods.cbegin(), cls->methods.cend(),
                                          [](const CppMethod &m) { return m.isConstructor; });
    if (constructor != cls->methods.cend()) {
        plan.hasConstructor = true;
        plan.constructorMethod = *constructor;
        const QString problem = describeMismatch(typesOf(meta.constructorParams), *constructor);
        if (!problem.isEmpty())
            return manualPlan(cls->name, QStringLiteral("Constructor: ") + problem);
    }

    for (const MetaMethod &metaMethod : meta.methods) {
        const CppMethod *method = findMethod(*cls, metaMethod.name);
        if (!method) {
            return manualPlan(cls->name, QStringLiteral("The class has no method named \"%1\".")
                                             .arg(metaMethod.name));
        }
        const QString problem = describeMismatch(typesOf(metaMethod.params), *method);
        if (!problem.isEmpty())
            return manualPlan(cls->name, metaMethod.name + QStringLiteral(": ") + problem);
        plan.methods.append(*method);
    }
    return plan;
}

QString safeIdentifier(const QString &name)
{
    QString cleaned = name;
    static const QRegularExpression nonWord(QStringLiteral(R"(\W)"));
    cleaned.replace(nonWord, QStringLiteral("_"));
    if (cleaned.isEmpty())
        return QStringLiteral("value");
    return cleaned.at(0).isDigit() ? QLatin1Char('_') + cleaned : cleaned;
}

QStringList variableNames(const QList<CppParam> &params)
{
    static const QSet<QString> reserved = {
        QStringLiteral("solution"), QStringLiteral("result"), QStringLiteral("timer"),
        QStringLiteral("harness"),  QStringLiteral("out"),    QStringLiteral("obj"),
        QStringLiteral("main"),     QStringLiteral("lc"),     QStringLiteral("std"),
    };

    QStringList names;
    for (const CppParam &param : params) {
        QString name = safeIdentifier(param.name);
        while (reserved.contains(name) || names.contains(name))
            name += QLatin1Char('_');
        names << name;
    }
    return names;
}

QStringList functionBody(const HarnessPlan &plan, const LcTestCase &test)
{
    const CppMethod &method = plan.method;
    if (test.inputs.size() != method.params.size()) {
        throw LcError(QStringLiteral("expected %1 input(s) but the test has %2")
                          .arg(method.params.size())
                          .arg(test.inputs.size()));
    }

    const QStringList names = variableNames(method.params);
    QStringList lines;
    for (qsizetype i = 0; i < method.params.size(); ++i) {
        const CppType &type = method.params.at(i).type;
        lines << QStringLiteral("%1 %2 = %3;")
                     .arg(CppSignature::formatType(type, true), names.at(i),
                          HarnessGenerator::literal(type, test.inputs.at(i)));
    }

    const QString call = QStringLiteral("solution.%1(%2)").arg(method.name, names.join(QStringLiteral(", ")));
    const bool returnsVoid = !method.hasReturnType || method.returnTypeText == u"void";
    lines << QString() << plan.className + QStringLiteral(" solution;") << QStringLiteral("timer.start();")
          << (returnsVoid ? call + QLatin1Char(';') : QStringLiteral("auto result = %1;").arg(call))
          << QStringLiteral("timer.stop();");

    if (plan.hasOutput) {
        const QString &name = names.at(plan.outputParamIndex);
        if (plan.prefixFromReturn && !returnsVoid) {
            lines << QStringLiteral("return lc::serializePrefix(result, %1, %2);")
                         .arg(HarnessGenerator::cppStringLiteral(method.params.at(plan.outputParamIndex).name), name);
        } else {
            lines << QStringLiteral("return lc::serialize(%1);").arg(name);
        }
    } else if (returnsVoid) {
        // No declared output: report the first parameter the method could have modified.
        const auto modified = std::find_if(method.params.cbegin(), method.params.cend(), [](const CppParam &p) {
            return p.type.isReference || p.type.pointerDepth > 0;
        });
        lines << (modified != method.params.cend()
                      ? QStringLiteral("return lc::serialize(%1);")
                            .arg(names.at(modified - method.params.cbegin()))
                      : QStringLiteral("return std::string(\"null\");"));
    } else {
        lines << QStringLiteral("return lc::serialize(result);");
    }
    return lines;
}

struct DesignArguments
{
    QStringList setup;
    QString callArgs;
};

DesignArguments designArguments(const QList<CppParam> &params, const QList<LcValue> &values,
                                const QString &prefix)
{
    if (params.size() != values.size()) {
        throw LcError(QStringLiteral("expected %1 argument(s) but got %2")
                          .arg(params.size())
                          .arg(values.size()));
    }

    DesignArguments result;
    QStringList callArgs;
    for (qsizetype i = 0; i < params.size(); ++i) {
        const CppParam &param = params.at(i);
        const QString literal = HarnessGenerator::literal(param.type, values.at(i));
        // Non-const lvalue references need a named variable.
        if (param.type.isReference && !param.type.isConst) {
            const QString name = prefix + QLatin1Char('_') + param.name;
            result.setup << QStringLiteral("%1 %2 = %3;")
                                .arg(CppSignature::formatType(param.type, true), name, literal);
            callArgs << name;
        } else {
            callArgs << (literal.startsWith(u'{') ? CppSignature::formatType(param.type, true) + literal
                                                  : literal);
        }
    }
    result.callArgs = callArgs.join(QStringLiteral(", "));
    return result;
}

QStringList designBody(const HarnessPlan &plan, const LcTestCase &test)
{
    if (test.inputs.size() < 2 || !test.inputs.at(0).isArray() || !test.inputs.at(1).isArray())
        throw LcError(QStringLiteral("design tests need an operations array and an arguments array"));

    const QList<LcValue> &operations = test.inputs.at(0).items;
    const QList<LcValue> &arguments = test.inputs.at(1).items;
    if (operations.size() != arguments.size()) {
        throw LcError(QStringLiteral("%1 operation(s) but %2 argument list(s)")
                          .arg(operations.size())
                          .arg(arguments.size()));
    }

    QStringList lines = {QStringLiteral("lc::OutputList out;"), QStringLiteral("timer.start();")};
    for (qsizetype i = 0; i < operations.size(); ++i) {
        const LcValue &operation = operations.at(i);
        const LcValue &argumentList = arguments.at(i);
        const bool hasName = operation.kind == LcValue::Kind::String || operation.kind == LcValue::Kind::Symbol;
        if (!hasName || operation.text.isEmpty() || !(argumentList.isArray() || argumentList.isNull()))
            throw LcError(QStringLiteral("operation %1 is malformed").arg(i + 1));

        const QList<LcValue> values = argumentList.isArray() ? argumentList.items : QList<LcValue>();
        if (i == 0) {
            const QList<CppParam> params = plan.hasConstructor ? plan.constructorMethod.params : QList<CppParam>();
            const DesignArguments args = designArguments(params, values, QStringLiteral("ctor"));
            lines << args.setup;
            lines << QStringLiteral("%1* obj = new %1(%2);").arg(plan.className, args.callArgs)
                  << QStringLiteral("out.null();");
            continue;
        }

        const auto method = std::find_if(plan.methods.cbegin(), plan.methods.cend(),
                                         [&](const CppMethod &m) { return m.name == operation.text; });
        if (method == plan.methods.cend())
            throw LcError(QStringLiteral("unknown operation \"%1\"").arg(operation.text));

        const DesignArguments args = designArguments(method->params, values, QStringLiteral("a%1").arg(i));
        const QString call = QStringLiteral("obj->%1(%2)").arg(method->name, args.callArgs);
        const QString statement = method->returnTypeText == u"void"
                                      ? call + QStringLiteral("; out.null();")
                                      : QStringLiteral("out.add(%1);").arg(call);
        if (args.setup.isEmpty()) {
            lines << statement;
        } else {
            lines << QStringLiteral("{");
            for (const QString &line : args.setup)
                lines << I1 + line;
            lines << I1 + statement << QStringLiteral("}");
        }
    }
    lines << QStringLiteral("timer.stop();") << QStringLiteral("return out.str();");
    return lines;
}

QStringList manualHeader(const HarnessPlan &plan)
{
    return {
        QStringLiteral("// MANUAL HARNESS - this section is generated once and never overwritten, so edit it freely."),
        QStringLiteral("// Why: ") + plan.reason,
        QStringLiteral("// LeetCode's judge builds special inputs for this problem, so each test below declares"),
        QStringLiteral("// the raw example inputs; adapt them to the method signature and return the output."),
        QStringLiteral("// Helpers (see .lc/leetcode.hpp): lc::buildList, lc::buildTree, lc::makeCycle, lc::nodeAt,"),
        QStringLiteral("// lc::findNode, lc::serialize. Tests added to tests.json later are not added here automatically."),
    };
}

QString methodSignature(const CppMethod &method)
{
    QStringList params;
    for (const CppParam &param : method.params) {
        const bool glued = param.typeText.endsWith(u'*') || param.typeText.endsWith(u'&');
        params << param.typeText + (glued ? QString() : QStringLiteral(" ")) + param.name;
    }
    return QStringLiteral("%1 %2(%3)")
        .arg(method.returnTypeText, method.name, params.join(QStringLiteral(", ")))
        .trimmed();
}

QStringList manualBody(const HarnessPlan &plan, const ProblemMeta &meta, const LcTestCase &test,
                       const QStringList &knownTypes)
{
    static const QStringList structTypes = {QStringLiteral("ListNode"), QStringLiteral("TreeNode")};

    QStringList lines;
    const QStringList keys = QuestionParser::inputKeys(meta);
    const QStringList metaTypes = meta.systemDesign ? QStringList() : typesOf(meta.params);

    for (qsizetype i = 0; i < test.inputs.size(); ++i) {
        const LcValue &value = test.inputs.at(i);
        const QString name = safeIdentifier(i < keys.size() ? keys.at(i) : QStringLiteral("input%1").arg(i + 1));
        const QString cppType = i < metaTypes.size() ? metaTypeToCpp(metaTypes.at(i)) : QString();

        // Only declare node types the problem defines. Populating Next Right
        // Pointers, for one, has Node and no TreeNode.
        const bool usable = !cppType.isEmpty()
                            && std::all_of(structTypes.cbegin(), structTypes.cend(), [&](const QString &t) {
                                   return !cppType.contains(t) || knownTypes.contains(t);
                               });
        if (usable) {
            try {
                const QString literal = HarnessGenerator::literal(CppSignature::parseType(cppType), value);
                lines << QStringLiteral("[[maybe_unused]] %1 %2 = %3;").arg(cppType, name, literal);
                continue;
            } catch (const LcError &) {
                // Falls through to a comment.
            }
        }
        lines << QStringLiteral("// %1 = %2").arg(name, abbreviate(toLeetCodeText(value), 100));
    }

    const QString className = plan.className.isEmpty() ? QStringLiteral("Solution") : plan.className;
    const QString signature = plan.hasMethod ? methodSignature(plan.method)
                                             : className + QStringLiteral(" (see USER SOLUTION)");
    lines << QString()
          << QStringLiteral("// TODO: adapt the inputs above to the signature below, call it, and return the output:")
          << QStringLiteral("//   ") + signature
          << QStringLiteral("// %1 solution;").arg(className)
          << QStringLiteral("timer.start();")
          << QStringLiteral("// auto result = solution.%1(...);")
                 .arg(plan.hasMethod ? plan.method.name : QStringLiteral("method"))
          << QStringLiteral("timer.stop();")
          << QStringLiteral("// return lc::serialize(result);")
          << QStringLiteral("return lc::notWired();");
    return lines;
}

QString expectedLiteral(const LcTestCase &test)
{
    if (test.expected)
        return HarnessGenerator::cppStringLiteral(toLeetCodeText(*test.expected));
    return test.expectedText.isEmpty() ? QStringLiteral("nullptr")
                                       : HarnessGenerator::cppStringLiteral(test.expectedText);
}

} // namespace

namespace HarnessGenerator {

QString metaElementType(const QString &metaType)
{
    const QString t = metaType.trimmed();
    if (t.endsWith(QLatin1String("[]")))
        return t.chopped(2);
    static const QRegularExpression list(QStringLiteral("^list<(.+)>$"),
                                         QRegularExpression::CaseInsensitiveOption);
    return list.match(t).captured(1);
}

QString describeInputs(const QStringList &keys, const QList<LcValue> &inputs)
{
    QStringList parts;
    for (qsizetype i = 0; i < inputs.size(); ++i) {
        parts << QStringLiteral("%1 = %2")
                     .arg(i < keys.size() ? keys.at(i) : QStringLiteral("input%1").arg(i + 1),
                          toLeetCodeText(inputs.at(i)));
    }
    return parts.join(QStringLiteral(", "));
}

QString cppStringLiteral(const QString &value)
{
    QString out = QStringLiteral("\"");
    for (const QChar c : value) {
        if (c == u'"') {
            out += QStringLiteral("\\\"");
        } else if (c == u'\\') {
            out += QStringLiteral("\\\\");
        } else if (c == u'\n') {
            out += QStringLiteral("\\n");
        } else if (c == u'\r') {
            out += QStringLiteral("\\r");
        } else if (c == u'\t') {
            out += QStringLiteral("\\t");
        } else if (c.unicode() < 0x20) {
            // \x consumes every following hex digit, so the literal closes right after it.
            out += QStringLiteral("\\x%1\"\"").arg(int(c.unicode()), 2, 16, QLatin1Char('0'));
        } else {
            out += c;
        }
    }
    return out + QLatin1Char('"');
}

QString literal(const CppType &type, const LcValue &value)
{
    if (type.pointerDepth == 0 && type.name == u"bool")
        return boolLiteral(type, value);
    if (type.pointerDepth == 0 && type.name == u"char")
        return charLiteral(type, value);
    if (type.isInteger())
        return integerLiteral(type, value);
    if (type.isFloating())
        return floatingLiteral(type, value);
    if (type.isString()) {
        if (value.kind != LcValue::Kind::String)
            mismatch(type, value);
        return cppStringLiteral(value.text);
    }
    if (type.isVector()) {
        if (!value.isArray())
            mismatch(type, value);
        QStringList items;
        for (const LcValue &item : value.items)
            items << literal(type.templateArgs.first(), item);
        return QLatin1Char('{') + items.join(QStringLiteral(", ")) + QLatin1Char('}');
    }
    if (type.isNodePointer(QStringLiteral("ListNode"))) {
        const auto values = nodeValues(type, value, false);
        if (!values || values->isEmpty())
            return QStringLiteral("nullptr");
        return QStringLiteral("lc::buildList<ListNode>({%1})").arg(values->join(QStringLiteral(", ")));
    }
    if (type.isNodePointer(QStringLiteral("TreeNode"))) {
        const auto values = nodeValues(type, value, true);
        if (!values || values->isEmpty())
            return QStringLiteral("nullptr");
        return QStringLiteral("lc::buildTree<TreeNode>({%1})").arg(values->join(QStringLiteral(", ")));
    }
    throw LcError(QStringLiteral("The C++ type \"%1\" is not supported by the test generator yet.")
                      .arg(CppSignature::formatType(type)));
}

HarnessPlan planHarness(const ProblemMeta &meta, const CppStarter &starter)
{
    return meta.systemDesign ? planDesign(meta, starter) : planFunction(meta, starter);
}

GeneratedHarness generate(const HarnessPlan &plan, const ProblemMeta &meta,
                          const QList<LcTestCase> &tests, const QStringList &knownTypes)
{
    GeneratedHarness harness;
    const QStringList keys = QuestionParser::inputKeys(meta);
    QStringList lines;

    if (plan.kind == HarnessPlan::Kind::Manual) {
        lines << manualHeader(plan);
    } else {
        lines << QStringLiteral("// Generated from tests.json by LeetRunner. This section is rewritten")
              << QStringLiteral("// before every run: add or edit tests in tests.json (or use \"LeetCode: Add Custom Test\").")
              << QStringLiteral("// Run this file directly (without arguments) to print every test result, e.g. when debugging.");
    }
    lines << QString() << QStringLiteral("int main(int argc, char** argv) {")
          << I1 + QStringLiteral("lc::Harness harness(argc, argv);") << QString();

    for (const LcTestCase &test : tests) {
        lines << I1 + QStringLiteral("// Test Case %1%2: %3")
                          .arg(test.id)
                          .arg(test.source == TestSource::Custom ? QStringLiteral(" (custom)") : QString(),
                               abbreviate(describeInputs(keys, test.inputs), 110));
        const QString expected = expectedLiteral(test);

        QStringList body;
        try {
            switch (plan.kind) {
            case HarnessPlan::Kind::Function: body = functionBody(plan, test); break;
            case HarnessPlan::Kind::Design: body = designBody(plan, test); break;
            case HarnessPlan::Kind::Manual: body = manualBody(plan, meta, test, knownTypes); break;
            }
        } catch (const LcError &error) {
            harness.warnings << QStringLiteral("Test Case %1: %2").arg(test.id).arg(error.message());
            lines << I1 + QStringLiteral("harness.test(%1, %2, [&](lc::Timer&) -> std::string {").arg(test.id).arg(expected)
                  << I2 + QStringLiteral("throw lc::HarnessError(%1);")
                              .arg(cppStringLiteral(QStringLiteral("Could not generate this test: ") + error.message()))
                  << I1 + QStringLiteral("});") << QString();
            continue;
        }

        lines << I1 + QStringLiteral("harness.test(%1, %2, [&](lc::Timer& timer) {").arg(test.id).arg(expected);
        for (const QString &line : std::as_const(body))
            lines << (line.isEmpty() ? QString() : I2 + line);
        lines << I1 + QStringLiteral("});") << QString();
    }

    if (tests.isEmpty()) {
        lines << I1 + QStringLiteral("// No tests yet: add some to tests.json or run \"LeetCode: Add Custom Test\".")
              << QString();
    }
    lines << I1 + QStringLiteral("return harness.finish();") << QStringLiteral("}") << QString();
    harness.code = lines.join(QLatin1Char('\n'));
    return harness;
}

} // namespace HarnessGenerator
