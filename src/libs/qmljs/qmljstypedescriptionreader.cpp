#include "qmljstypedescriptionreader.h"

#include "parser/qmljsparser_p.h"
#include "parser/qmljslexer_p.h"
#include "parser/qmljsengine_p.h"
#include "parser/qmljsnodepool_p.h"
#include "parser/qmljsast_p.h"
#include "parser/qmljsastvisitor_p.h"

#include "qmljsbind.h"

#include <QtCore/QIODevice>
#include <QtCore/QBuffer>

using namespace QmlJS;
using namespace QmlJS::AST;
using namespace LanguageUtils;

TypeDescriptionReader::TypeDescriptionReader(const QString &data)
    : _source(data)
    , _objects(0)
{
}

TypeDescriptionReader::~TypeDescriptionReader()
{
}

bool TypeDescriptionReader::operator()(QMap<QString, FakeMetaObject::Ptr> *objects)
{
    QString fileName("typeDescription");
    Engine engine;
    NodePool pool(fileName, &engine);

    Lexer lexer(&engine);
    Parser parser(&engine);

    lexer.setCode(_source, /*line = */ 1);

    if (!parser.parse()) {
        _errorMessage = QString("%1:%2: %3").arg(
                    QString::number(parser.errorLineNumber()),
                    QString::number(parser.errorColumnNumber()),
                    parser.errorMessage());
        return false;
    }

    _objects = objects;
    readDocument(parser.ast());

    return _errorMessage.isEmpty();
}

QString TypeDescriptionReader::errorMessage() const
{
    return _errorMessage;
}

void TypeDescriptionReader::readDocument(UiProgram *ast)
{
    if (!ast) {
        addError(SourceLocation(), "Could not parse document");
        return;
    }

    if (!ast->imports || ast->imports->next) {
        addError(SourceLocation(), "Expected a single import");
        return;
    }

    UiImport *import = ast->imports->import;
    if (Bind::toString(import->importUri) != QLatin1String("QtQuick.tooling")) {
        addError(import->importToken, "Expected import of QtQuick.tooling");
        return;
    }

    ComponentVersion version;
    const QString versionString = _source.mid(import->versionToken.offset, import->versionToken.length);
    const int dotIdx = versionString.indexOf(QLatin1Char('.'));
    if (dotIdx != -1) {
        version = ComponentVersion(versionString.left(dotIdx).toInt(),
                                   versionString.mid(dotIdx + 1).toInt());
    }
    if (version != ComponentVersion(1, 0)) {
        addError(import->versionToken, "Expected version 1.0");
        return;
    }

    if (!ast->members || !ast->members->member || ast->members->next) {
        addError(SourceLocation(), "Expected document to contain a single object definition");
        return;
    }

    UiObjectDefinition *module = dynamic_cast<UiObjectDefinition *>(ast->members->member);
    if (!module) {
        addError(SourceLocation(), "Expected document to contain a single object definition");
        return;
    }

    if (Bind::toString(module->qualifiedTypeNameId) != "Module") {
        addError(SourceLocation(), "Expected document to contain a Module {} member");
        return;
    }

    readModule(module);
}

void TypeDescriptionReader::readModule(UiObjectDefinition *ast)
{
    for (UiObjectMemberList *it = ast->initializer->members; it; it = it->next) {
        UiObjectMember *member = it->member;
        UiObjectDefinition *component = dynamic_cast<UiObjectDefinition *>(member);
        if (!component || Bind::toString(component->qualifiedTypeNameId) != "Component") {
            addError(member->firstSourceLocation(), "Expected only 'Component' object definitions");
            return;
        }

        readComponent(component);
    }
}

void TypeDescriptionReader::addError(const SourceLocation &loc, const QString &message)
{
    _errorMessage += QString("%1:%2: %3\n").arg(
                QString::number(loc.startLine),
                QString::number(loc.startColumn),
                message);
}

void TypeDescriptionReader::readComponent(UiObjectDefinition *ast)
{
    FakeMetaObject::Ptr fmo(new FakeMetaObject);

    for (UiObjectMemberList *it = ast->initializer->members; it; it = it->next) {
        UiObjectMember *member = it->member;
        UiObjectDefinition *component = dynamic_cast<UiObjectDefinition *>(member);
        UiScriptBinding *script = dynamic_cast<UiScriptBinding *>(member);
        if (component) {
            QString name = Bind::toString(component->qualifiedTypeNameId);
            if (name == "Property") {
                readProperty(component, fmo);
            } else if (name == "Method" || name == "Signal") {
                readSignalOrMethod(component, name == "Method", fmo);
            } else if (name == "Enum") {
                readEnum(component, fmo);
            } else {
                addError(component->firstSourceLocation(), "Expected only Property, Method, Signal and Enum object definitions");
                return;
            }
        } else if (script) {
            QString name = Bind::toString(script->qualifiedId);
            if (name == "name") {
                fmo->setClassName(readStringBinding(script));
            } else if (name == "prototype") {
                fmo->setSuperclassName(readStringBinding(script));
            } else if (name == "defaultProperty") {
                fmo->setDefaultPropertyName(readStringBinding(script));
            } else if (name == "exports") {
                readExports(script, fmo);
            } else {
                addError(script->firstSourceLocation(), "Expected only name, prototype, defaultProperty and exports script bindings");
                return;
            }
        } else {
            addError(member->firstSourceLocation(), "Expected only script bindings and object definitions");
            return;
        }
    }

    if (fmo->className().isEmpty()) {
        addError(ast->firstSourceLocation(), "Component definition is missing a name binding");
        return;
    }

    _objects->insert(fmo->className(), fmo);
}

void TypeDescriptionReader::readSignalOrMethod(UiObjectDefinition *ast, bool isMethod, FakeMetaObject::Ptr fmo)
{
    FakeMetaMethod fmm;
    // ### confusion between Method and Slot. Method should be removed.
    if (isMethod)
        fmm.setMethodType(FakeMetaMethod::Slot);
    else
        fmm.setMethodType(FakeMetaMethod::Signal);

    for (UiObjectMemberList *it = ast->initializer->members; it; it = it->next) {
        UiObjectMember *member = it->member;
        UiObjectDefinition *component = dynamic_cast<UiObjectDefinition *>(member);
        UiScriptBinding *script = dynamic_cast<UiScriptBinding *>(member);
        if (component) {
            QString name = Bind::toString(component->qualifiedTypeNameId);
            if (name == "Parameter") {
                readParameter(component, &fmm);
            } else {
                addError(component->firstSourceLocation(), "Expected only Parameter object definitions");
                return;
            }
        } else if (script) {
            QString name = Bind::toString(script->qualifiedId);
            if (name == "name") {
                fmm.setMethodName(readStringBinding(script));
            } else if (name == "type") {
                fmm.setReturnType(readStringBinding(script));
            } else {
                addError(script->firstSourceLocation(), "Expected only name and type script bindings");
                return;
            }

        } else {
            addError(member->firstSourceLocation(), "Expected only script bindings and object definitions");
            return;
        }
    }

    if (fmm.methodName().isEmpty()) {
        addError(ast->firstSourceLocation(), "Method or Signal is missing a name script binding");
        return;
    }

    fmo->addMethod(fmm);
}

void TypeDescriptionReader::readProperty(UiObjectDefinition *ast, FakeMetaObject::Ptr fmo)
{
    QString name;
    QString type;
    bool isPointer = false;
    bool isReadonly = false;
    bool isList = false;

    for (UiObjectMemberList *it = ast->initializer->members; it; it = it->next) {
        UiObjectMember *member = it->member;
        UiScriptBinding *script = dynamic_cast<UiScriptBinding *>(member);
        if (!script) {
            addError(member->firstSourceLocation(), "Expected script binding");
            return;
        }

        QString id = Bind::toString(script->qualifiedId);
        if (id == "name") {
            name = readStringBinding(script);
        } else if (id == "type") {
            type = readStringBinding(script);
        } else if (id == "isPointer") {
            isPointer = readBoolBinding(script);
        } else if (id == "isReadonly") {
            isReadonly = readBoolBinding(script);
        } else if (id == "isList") {
            isList = readBoolBinding(script);
        } else {
            addError(script->firstSourceLocation(), "Expected only type, name, isPointer, isReadonly and isList script bindings");
            return;
        }
    }

    if (name.isEmpty() || type.isEmpty()) {
        addError(ast->firstSourceLocation(), "Property object is missing a name or type script binding");
        return;
    }

    fmo->addProperty(FakeMetaProperty(name, type, isList, !isReadonly, isPointer));
}

void TypeDescriptionReader::readEnum(UiObjectDefinition *ast, FakeMetaObject::Ptr fmo)
{
    FakeMetaEnum fme;

    for (UiObjectMemberList *it = ast->initializer->members; it; it = it->next) {
        UiObjectMember *member = it->member;
        UiScriptBinding *script = dynamic_cast<UiScriptBinding *>(member);
        if (!script) {
            addError(member->firstSourceLocation(), "Expected script binding");
            return;
        }

        QString name = Bind::toString(script->qualifiedId);
        if (name == "name") {
            fme.setName(readStringBinding(script));
        } else if (name == "values") {
            readEnumValues(script, &fme);
        } else {
            addError(script->firstSourceLocation(), "Expected only name and values script bindings");
            return;
        }
    }

    fmo->addEnum(fme);
}

void TypeDescriptionReader::readParameter(UiObjectDefinition *ast, FakeMetaMethod *fmm)
{
    QString name;
    QString type;

    for (UiObjectMemberList *it = ast->initializer->members; it; it = it->next) {
        UiObjectMember *member = it->member;
        UiScriptBinding *script = dynamic_cast<UiScriptBinding *>(member);
        if (!script) {
            addError(member->firstSourceLocation(), "Expected script binding");
            return;
        }

        QString id = Bind::toString(script->qualifiedId);
        if (id == "name") {
            id = readStringBinding(script);
        } else if (id == "type") {
            type = readStringBinding(script);
        } else if (id == "isPointer") {
            // ### unhandled
        } else if (id == "isReadonly") {
            // ### unhandled
        } else if (id == "isList") {
            // ### unhandled
        } else {
            addError(script->firstSourceLocation(), "Expected only name and type script bindings");
            return;
        }
    }

    fmm->addParameter(name, type);
}

QString TypeDescriptionReader::readStringBinding(UiScriptBinding *ast)
{
    if (!ast || !ast->statement) {
        addError(ast->colonToken, "Expected string after colon");
        return QString();
    }

    ExpressionStatement *expStmt = dynamic_cast<ExpressionStatement *>(ast->statement);
    if (!expStmt) {
        addError(ast->statement->firstSourceLocation(), "Expected string after colon");
        return QString();
    }

    StringLiteral *stringLit = dynamic_cast<StringLiteral *>(expStmt->expression);
    if (!stringLit) {
        addError(expStmt->firstSourceLocation(), "Expected string after colon");
        return QString();
    }

    return stringLit->value->asString();
}

bool TypeDescriptionReader::readBoolBinding(AST::UiScriptBinding *ast)
{
    if (!ast || !ast->statement) {
        addError(ast->colonToken, "Expected boolean after colon");
        return false;
    }

    ExpressionStatement *expStmt = dynamic_cast<ExpressionStatement *>(ast->statement);
    if (!expStmt) {
        addError(ast->statement->firstSourceLocation(), "Expected boolean after colon");
        return false;
    }

    TrueLiteral *trueLit = dynamic_cast<TrueLiteral *>(expStmt->expression);
    FalseLiteral *falseLit = dynamic_cast<FalseLiteral *>(expStmt->expression);
    if (!trueLit && !falseLit) {
        addError(expStmt->firstSourceLocation(), "Expected true or false after colon");
        return false;
    }

    return trueLit;
}

void TypeDescriptionReader::readExports(UiScriptBinding *ast, FakeMetaObject::Ptr fmo)
{
    if (!ast || !ast->statement) {
        addError(ast->colonToken, "Expected array of strings after colon");
        return;
    }

    ExpressionStatement *expStmt = dynamic_cast<ExpressionStatement *>(ast->statement);
    if (!expStmt) {
        addError(ast->statement->firstSourceLocation(), "Expected array of strings after colon");
        return;
    }

    ArrayLiteral *arrayLit = dynamic_cast<ArrayLiteral *>(expStmt->expression);
    if (!arrayLit) {
        addError(expStmt->firstSourceLocation(), "Expected array of strings after colon");
        return;
    }

    for (ElementList *it = arrayLit->elements; it; it = it->next) {
        StringLiteral *stringLit = dynamic_cast<StringLiteral *>(it->expression);
        if (!stringLit) {
            addError(arrayLit->firstSourceLocation(), "Expected array literal with only string literal members");
            return;
        }
        QString exp = stringLit->value->asString();
        int slashIdx = exp.indexOf(QLatin1Char('/'));
        int spaceIdx = exp.indexOf(QLatin1Char(' '));
        ComponentVersion version(exp.mid(spaceIdx + 1));

        if (spaceIdx == -1 || !version.isValid()) {
            addError(stringLit->firstSourceLocation(), "Expected string literal to contain 'Package/Name major.minor' or 'Name major.minor'");
            continue;
        }
        QString package;
        if (slashIdx != -1)
            package = exp.left(slashIdx);
        QString name = exp.mid(slashIdx + 1, spaceIdx - (slashIdx+1));

        // ### relocatable exports where package is empty?
        fmo->addExport(name, package, version);
    }
}

void TypeDescriptionReader::readEnumValues(AST::UiScriptBinding *ast, LanguageUtils::FakeMetaEnum *fme)
{
    if (!ast || !ast->statement) {
        addError(ast->colonToken, "Expected object literal after colon");
        return;
    }

    ExpressionStatement *expStmt = dynamic_cast<ExpressionStatement *>(ast->statement);
    if (!expStmt) {
        addError(ast->statement->firstSourceLocation(), "Expected object literal after colon");
        return;
    }

    ObjectLiteral *objectLit = dynamic_cast<ObjectLiteral *>(expStmt->expression);
    if (!objectLit) {
        addError(expStmt->firstSourceLocation(), "Expected object literal after colon");
        return;
    }

    for (PropertyNameAndValueList *it = objectLit->properties; it; it = it->next) {
        StringLiteralPropertyName *propName = dynamic_cast<StringLiteralPropertyName *>(it->name);
        NumericLiteral *value = dynamic_cast<NumericLiteral *>(it->value);
        UnaryMinusExpression *minus = dynamic_cast<UnaryMinusExpression *>(it->value);
        if (minus)
            value = dynamic_cast<NumericLiteral *>(minus->expression);
        if (!propName || !value) {
            addError(objectLit->firstSourceLocation(), "Expected object literal to contain only 'string: number' elements");
            continue;
        }

        double v = value->value;
        if (minus)
            v = -v;
        fme->addKey(propName->id->asString(), v);
    }
}
