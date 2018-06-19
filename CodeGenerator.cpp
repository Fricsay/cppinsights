/******************************************************************************
 *
 * C++ Insights, copyright (C) by Andreas Fertig
 * Distributed under an MIT license. See LICENSE for details
 *
 ****************************************************************************/

#include "CodeGenerator.h"
#include "DPrint.h"
#include "InsightsBase.h"
#include "InsightsMatchers.h"
#include "InsightsStrCat.h"
#include "NumberIterator.h"
//-----------------------------------------------------------------------------

/// \brief Convenience macro to create a \ref LambdaScopeHandler on the stack.
#define LAMBDA_SCOPE_HELPER(type)                                                                                      \
    LambdaScopeHandler lambdaScopeHandler{mLambdaStack, mOutputFormatHelper, LambdaCallerType::type};
//-----------------------------------------------------------------------------

namespace clang::insights {

static const char* AccessToString(const AccessSpecifier& access)
{
    switch(access) {
        case AS_public: return "public";
        case AS_protected: return "protected";
        case AS_private: return "private";
        default: return "";
    }
}
//-----------------------------------------------------------------------------

static std::string AccessToStringWithColon(const AccessSpecifier& access)
{
    return StrCat(AccessToString(access), ": ");
}
//-----------------------------------------------------------------------------

static std::string AccessToStringWithColon(const CXXMethodDecl& decl)
{
    return AccessToStringWithColon(decl.getAccess());
}
//-----------------------------------------------------------------------------

class ArrayInitCodeGenerator final : public CodeGenerator
{
    const uint64_t mIndex;

public:
    ArrayInitCodeGenerator(OutputFormatHelper& _outputFormatHelper, const uint64_t index)
    : CodeGenerator{_outputFormatHelper}
    , mIndex{index}
    {
    }

    void InsertArg(const Stmt* stmt) override { CodeGenerator::InsertArg(stmt); }
    void InsertArg(const ArrayInitIndexExpr*) override { mOutputFormatHelper.Append(std::to_string(mIndex)); }
};
//-----------------------------------------------------------------------------

CodeGenerator::LambdaScopeHandler::LambdaScopeHandler(LambdaStackType&       stack,
                                                      OutputFormatHelper&    outputFormatHelper,
                                                      const LambdaCallerType lambdaCallerType)
: mStack{stack}
, mHelper{lambdaCallerType, GetBuffer(outputFormatHelper)}
{
    DPrint("xx: %d\n", static_cast<int>(lambdaCallerType));
    mStack.push(mHelper);
}
//-----------------------------------------------------------------------------

CodeGenerator::LambdaScopeHandler::~LambdaScopeHandler()
{
    DPrint("ddddd\n");
    if(!mStack.empty()) {
        mStack.pop()->finish();
    }
}
//-----------------------------------------------------------------------------

OutputFormatHelper& CodeGenerator::LambdaScopeHandler::GetBuffer(OutputFormatHelper& outputFormatHelper) const
{
    DPrint("kkk  - ");
    // Find the most outer element to place the lambda class definition. For example, if we have this:
    // Test( [&]() {} );
    // The lambda's class definition needs to be placed _before_ the CallExpr to Test.
    auto* element = [&]() -> LambdaHelper* {
        for(auto& l : mStack) {
            DPrint(" x: %d  ", static_cast<int>(l.callerType()));

            switch(l.callerType()) {
                case LambdaCallerType::CallExpr:
                case LambdaCallerType::VarDecl:
                case LambdaCallerType::ReturnStmt:
                case LambdaCallerType::OperatorCallExpr:
                case LambdaCallerType::MemberCallExpr:
                case LambdaCallerType::BinaryOperator: return &l;
                default: break;
            }
        }

        return nullptr;
    }();

    if(element) {
        return element->buffer();
    }

    return outputFormatHelper;
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXForRangeStmt* rangeForStmt)
{
    mOutputFormatHelper.OpenScope();

    InsertArg(rangeForStmt->getRangeStmt());
    InsertArg(rangeForStmt->getBeginStmt());
    InsertArg(rangeForStmt->getEndStmt());

    // add blank line after the declarations
    mOutputFormatHelper.AppendNewLine();

    mOutputFormatHelper.Append("for( ; ");

    InsertArg(rangeForStmt->getCond());

    mOutputFormatHelper.Append("; ");

    InsertArg(rangeForStmt->getInc());

    mOutputFormatHelper.AppendNewLine(" )");
    // open for loop scope
    mOutputFormatHelper.OpenScope();

    InsertArg(rangeForStmt->getLoopVariable());

    const auto* body         = rangeForStmt->getBody();
    const bool  isBodyBraced = isa<CompoundStmt>(body);

    /* we already opened a scope. Skip the initial one */
    if(!isBodyBraced) {
        InsertArg(body);
    } else {
        HandleCompoundStmt(dyn_cast_or_null<CompoundStmt>(body));
    }

    if(!isBodyBraced && !isa<NullStmt>(body)) {
        mOutputFormatHelper.AppendNewLine(';');
    }

    // close range-for scope in for
    mOutputFormatHelper.CloseScope(OutputFormatHelper::NoNewLineBefore::Yes);

    // close outer range-for scope
    mOutputFormatHelper.CloseScope();
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const UnresolvedLookupExpr* stmt)
{
    mOutputFormatHelper.Append(stmt->getName().getAsString());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const ConditionalOperator* stmt)
{
    InsertArg(stmt->getCond());
    mOutputFormatHelper.Append(" ? ");
    InsertArg(stmt->getLHS());
    mOutputFormatHelper.Append(" : ");
    InsertArg(stmt->getRHS());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const DoStmt* stmt)
{
    mOutputFormatHelper.Append("do ");
    const auto* body = stmt->getBody();
    InsertArg(body);

    if(isa<CompoundStmt>(body)) {
        mOutputFormatHelper.Append(' ');
    }

    mOutputFormatHelper.Append("while");
    WrapInParensOrCurlys(BraceKind::Parens, [&]() { InsertArg(stmt->getCond()); }, AddSpaceAtTheEnd::Yes);
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CaseStmt* stmt)
{
    mOutputFormatHelper.Append("case ");
    InsertArg(stmt->getLHS());
    // TODO what is getRHS??
    mOutputFormatHelper.Append(": ");
    InsertArg(stmt->getSubStmt());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const BreakStmt* /*stmt*/)
{
    mOutputFormatHelper.Append("break");
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const DefaultStmt* stmt)
{
    mOutputFormatHelper.Append("default: ");
    InsertArg(stmt->getSubStmt());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const SwitchStmt* stmt)
{
    const bool hasInit{stmt->getInit() || stmt->getConditionVariable()};

    if(hasInit) {
        mOutputFormatHelper.OpenScope();

        if(const auto* conditionVar = stmt->getConditionVariable()) {
            InsertArg(conditionVar);
        }

        if(const auto* init = stmt->getInit()) {
            InsertArg(init);
        }
    }

    mOutputFormatHelper.Append("switch");

    WrapInParensOrCurlys(BraceKind::Parens, [&]() { InsertArg(stmt->getCond()); }, AddSpaceAtTheEnd::Yes);

    InsertArg(stmt->getBody());

    if(hasInit) {
        mOutputFormatHelper.CloseScope();
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const WhileStmt* stmt)
{
    mOutputFormatHelper.Append("while");
    WrapInParensOrCurlys(BraceKind::Parens, [&]() { InsertArg(stmt->getCond()); }, AddSpaceAtTheEnd::Yes);

    InsertArg(stmt->getBody());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const MemberExpr* stmt)
{
    InsertArg(stmt->getBase());

    const std::string op{stmt->isArrow() ? "->" : "."};
    const auto*       meDecl = stmt->getMemberDecl();
    bool              skipTemplateArgs{false};
    const auto        name = [&]() -> std::string {
        // Handle a special case where we have a lambda static invoke operator. In that case use the appropriate using
        // retType as return type
        if(const auto* m = dyn_cast_or_null<CXXMethodDecl>(meDecl)) {
            if(const auto* rd = m->getParent(); rd && rd->isLambda()) {
                skipTemplateArgs = true;

                return StrCat("operator ", GetLambdaName(*rd), "::retType");
            }
        }

        return stmt->getMemberNameInfo().getName().getAsString();
    }();

    mOutputFormatHelper.Append(op, name);

    if(!skipTemplateArgs) {
        if(const auto cxxMethod = dyn_cast_or_null<CXXMethodDecl>(meDecl)) {
            InsertTemplateArgs(*cxxMethod->getAsFunction());
        }
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const UnaryExprOrTypeTraitExpr* stmt)
{
    mOutputFormatHelper.Append(GetKind(*stmt));

    if(!stmt->isArgumentType()) {
        InsertArg(stmt->getArgumentExpr());
    } else {
        mOutputFormatHelper.Append("(", GetName(stmt->getTypeOfArgument()), ")");
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const IntegerLiteral* stmt)
{
    const auto& type     = stmt->getType();
    const bool  isSigned = type->isSignedIntegerType();

    mOutputFormatHelper.Append(stmt->getValue().toString(10, isSigned));
    InsertSuffix(type);
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const FloatingLiteral* stmt)
{
    // FIXME: not working correctly
    mOutputFormatHelper.Append(EvaluateAsFloat(*stmt));
    InsertSuffix(stmt->getType());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXTypeidExpr* stmt)
{
    mOutputFormatHelper.Append("typeid");
    WrapInParensOrCurlys(BraceKind::Parens, [&]() {
        if(stmt->isTypeOperand()) {
            mOutputFormatHelper.Append(GetName(stmt->getType()));
        } else {
            InsertArg(stmt->getExprOperand());
        }
    });
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const BinaryOperator* stmt)
{
    LAMBDA_SCOPE_HELPER(BinaryOperator);

    InsertArg(stmt->getLHS());
    mOutputFormatHelper.Append(" ", stmt->getOpcodeStr(), " ");
    InsertArg(stmt->getRHS());
}
//-----------------------------------------------------------------------------

static bool IsReference(const QualType& type)
{
    return GetDesugarType(type)->isLValueReferenceType();
}
//-----------------------------------------------------------------------------

static bool IsReference(const ValueDecl& valDecl)
{
    return IsReference(valDecl.getType());
}
//-----------------------------------------------------------------------------

/*
 * Go deep in a Stmt if necessary and look to all childs for a DeclRefExpr.
 */
static const DeclRefExpr* FindDeclRef(const Stmt* stmt)
{
    if(const auto* dref = dyn_cast_or_null<DeclRefExpr>(stmt)) {
        return dref;
    } else if(const auto* arrayInitExpr = dyn_cast_or_null<ArrayInitLoopExpr>(stmt)) {
        const auto* srcExpr = arrayInitExpr->getCommonExpr()->getSourceExpr();

        if(const auto* arrayDeclRefExpr = dyn_cast_or_null<DeclRefExpr>(srcExpr)) {
            return arrayDeclRefExpr;
        }
    }

    for(const auto* child : stmt->children()) {
        if(const auto* childRef = FindDeclRef(child)) {
            return childRef;
        }
    }

    return nullptr;
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const DecompositionDecl* decompositionDeclStmt)
{
    const auto* declName = FindDeclRef(decompositionDeclStmt->getInit());
    const auto  baseVarName{[&]() {
        if(declName) {
            std::string name = GetPlainName(*declName);

            const std::string operatorName{"operator"};
            if(name.find(operatorName) != std::string::npos) {
                return operatorName;
            }

            return name;
        }

        Error(decompositionDeclStmt, "unknown decl\n");
        return std::string{""};
    }()};

    const std::string tmpVarName = [&]() {
        if(declName && declName->getDecl()) {
            return BuildInternalVarName(baseVarName, decompositionDeclStmt->getLocStart(), GetSM(*declName->getDecl()));
        }

        return BuildInternalVarName(baseVarName);
    }();

    mOutputFormatHelper.Append(GetTypeNameAsParameter(decompositionDeclStmt->getType(), tmpVarName), " = ");

    InsertArg(decompositionDeclStmt->getInit());

    mOutputFormatHelper.AppendNewLine(';');

    const bool isRefToObject = IsReference(*decompositionDeclStmt);

    for(const auto* bindingDecl : decompositionDeclStmt->bindings()) {
        if(const auto* binding = bindingDecl->getBinding()) {

            DPrint("sb name: %s\n", GetName(binding->getType()));

            const auto* holdingVarOrMemberExpr = [&]() -> const Expr* {
                if(const auto* holdingVar = bindingDecl->getHoldingVar()) {
                    return holdingVar->getAnyInitializer();
                }

                return dyn_cast_or_null<MemberExpr>(binding);
            }();

            const std::string refOrRefRef = [&]() -> std::string {
                const bool isArrayBinding{isa<ArraySubscriptExpr>(binding) && isRefToObject};
                const bool isNotTemporary{holdingVarOrMemberExpr && !isa<ExprWithCleanups>(holdingVarOrMemberExpr)};
                if(isArrayBinding || isNotTemporary) {
                    return "&";
                }

                return "";
            }();

            mOutputFormatHelper.Append(GetName(bindingDecl->getType()), refOrRefRef, " ", GetName(*bindingDecl), " = ");

            // tuple decomposition
            if(holdingVarOrMemberExpr) {
                DPrint("4444\n");

                StructuredBindingsCodeGenerator structuredBindingsCodeGenerator{mOutputFormatHelper, tmpVarName};
                CodeGenerator&                  codeGenerator = structuredBindingsCodeGenerator;
                codeGenerator.InsertArg(holdingVarOrMemberExpr);

                // array decomposition
            } else if(const auto* arraySubscription = dyn_cast_or_null<ArraySubscriptExpr>(binding)) {
                mOutputFormatHelper.Append(tmpVarName);

                InsertArg(arraySubscription);

            } else {
                TODO(bindingDecl, mOutputFormatHelper);
            }

            mOutputFormatHelper.AppendNewLine(';');
        }
    }
}
//-----------------------------------------------------------------------------

static std::string GetQualifiers(const VarDecl& vd)
{
    std::string qualifiers{};

    if(vd.isInline()) {
        qualifiers += "inline ";
    }

    if(SC_Extern == vd.getStorageClass()) {
        qualifiers += "extern ";
    }

    if(SC_Static == vd.getStorageClass()) {
        qualifiers += "static ";
    }

    if(vd.isConstexpr()) {
        qualifiers += "constexpr ";
    }

    return qualifiers;
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const VarDecl* stmt)
{
    LAMBDA_SCOPE_HELPER(VarDecl);

    if(const auto* decompDecl = dyn_cast_or_null<DecompositionDecl>(stmt)) {
        InsertArg(decompDecl);
    } else if(IsTrivialStaticClassVarDecl(*stmt)) {
        HandleLocalStaticNonTrivialClass(stmt);

    } else {
        mOutputFormatHelper.Append(GetQualifiers(*stmt));

        if(const auto type = stmt->getType(); type->isFunctionPointerType()) {
            const auto        lineNo = GetSM(*stmt).getSpellingLineNumber(stmt->getSourceRange().getBegin());
            const std::string funcPtrName{StrCat("FuncPtr_", std::to_string(lineNo), " ")};

            mOutputFormatHelper.AppendNewLine("using ", funcPtrName, "= ", GetName(type), ";");
            mOutputFormatHelper.Append(funcPtrName, GetName(*stmt));
        } else {
            mOutputFormatHelper.Append(GetTypeNameAsParameter(stmt->getType(), GetName(*stmt)));
        }

        if(stmt->hasInit()) {
            mOutputFormatHelper.Append(" = ");

            InsertArg(stmt->getInit());
        };

        if(stmt->isNRVOVariable()) {
            mOutputFormatHelper.Append(" /* NRVO variable */");
        }

        mOutputFormatHelper.AppendNewLine(';');
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const FunctionDecl* stmt)
{
    //    LAMBDA_SCOPE_HELPER(VarDecl);

    InsightsBase::GenerateFunctionPrototype(mOutputFormatHelper, *stmt);

    // defaulted
    // isConstexpr
    // isExternC
    // isNoReturn

    if(stmt->doesThisDeclarationHaveABody()) {
        InsertArg(stmt->getBody());
    } else {
        mOutputFormatHelper.AppendNewLine(';');
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const InitListExpr* stmt)
{
    WrapInParensOrCurlys(BraceKind::Curlys, [&]() {
        mOutputFormatHelper.IncreaseIndent();

        ForEachArg(stmt->inits(), [&](const auto& init) { InsertArg(init); });
    });

    mOutputFormatHelper.DecreaseIndent();
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXDefaultInitExpr* stmt)
{
    InsertArg(stmt->getExpr());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXDeleteExpr* stmt)
{
    mOutputFormatHelper.Append("delete");

    if(stmt->isArrayForm()) {
        mOutputFormatHelper.Append("[]");
    }

    mOutputFormatHelper.Append(' ');

    InsertArg(stmt->getArgument());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXConstructExpr* stmt)
{
    mOutputFormatHelper.Append(GetName(GetDesugarType(stmt->getType()), Unqualified::Yes));

    const BraceKind braceKind = [&]() {
        if(stmt->isListInitialization()) {
            return BraceKind::Curlys;
        }
        return BraceKind::Parens;
    }();

    WrapInParensOrCurlys(braceKind, [&]() {
        if(stmt->getNumArgs()) {
            ForEachArg(stmt->arguments(), [&](const auto& arg) { InsertArg(arg); });
        }
    });
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXMemberCallExpr* stmt)
{
    LAMBDA_SCOPE_HELPER(MemberCallExpr);

    InsertArg(stmt->getCallee());

    WrapInParensOrCurlys(BraceKind::Parens,
                         [&]() { ForEachArg(stmt->arguments(), [&](const auto& arg) { InsertArg(arg); }); });
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const ParenExpr* stmt)
{
    WrapInParensOrCurlys(BraceKind::Parens, [&]() { InsertArg(stmt->getSubExpr()); });
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const UnaryOperator* stmt)
{
    const char* opCodeName = GetOpcodeName(stmt->getOpcode());
    const bool  insertBefore{!stmt->isPostfix()};

    if(insertBefore) {
        mOutputFormatHelper.Append(opCodeName);
    }

    InsertArg(stmt->getSubExpr());

    if(!insertBefore) {
        mOutputFormatHelper.Append(opCodeName);
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const StringLiteral* stmt)
{
    std::string              data{};
    llvm::raw_string_ostream stream{data};
    stmt->outputString(stream);

    mOutputFormatHelper.Append(stream.str());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const ArrayInitIndexExpr* stmt)
{
    Error(stmt, "ArrayInitIndexExpr should not be reached in CodeGenerator");
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const ArraySubscriptExpr* stmt)
{
    InsertArg(stmt->getLHS());

    mOutputFormatHelper.Append('[');
    InsertArg(stmt->getRHS());
    mOutputFormatHelper.Append(']');
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const ArrayInitLoopExpr* stmt)
{
    WrapInParensOrCurlys(BraceKind::Curlys, [&]() {
        const uint64_t size = stmt->getArraySize().getZExtValue();

        ForEachArg(NumberIterator(size), [&](const auto& i) {
            ArrayInitCodeGenerator codeGenerator{mOutputFormatHelper, i};
            codeGenerator.InsertArg(stmt->getSubExpr());
        });
    });
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const OpaqueValueExpr* stmt)
{
    InsertArg(stmt->getSourceExpr());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CallExpr* stmt)
{
    LAMBDA_SCOPE_HELPER(CallExpr);

    InsertArg(stmt->getCallee());

    if(isa<UserDefinedLiteral>(stmt)) {
        if(const auto* DRE = cast<DeclRefExpr>(stmt->getCallee()->IgnoreImpCasts())) {
            if(const TemplateArgumentList* Args = cast<FunctionDecl>(DRE->getDecl())->getTemplateSpecializationArgs()) {
                if(1 != Args->size()) {
                    InsertTemplateArgs(Args->asArray());
                } else {
                    mOutputFormatHelper.Append('<');

                    const TemplateArgument& Pack = Args->get(0);

                    ForEachArg(Pack.pack_elements(), [&](const auto& arg) {
                        char C{static_cast<char>(arg.getAsIntegral().getZExtValue())};
                        mOutputFormatHelper.Append("'", std::string{C}, "'");
                    });

                    mOutputFormatHelper.Append('>');
                }
            }
        }
    }

    WrapInParensOrCurlys(BraceKind::Parens,
                         [&]() { ForEachArg(stmt->arguments(), [&](const auto& arg) { InsertArg(arg); }); });
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXNamedCastExpr* stmt)
{
    const QualType castDestType = stmt->getType().getCanonicalType();
    const Expr*    subExpr      = stmt->getSubExpr();

    FormatCast(stmt->getCastName(), castDestType, subExpr, stmt->getCastKind());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const ImplicitCastExpr* stmt)
{
    const Expr* subExpr  = stmt->getSubExpr();
    const auto  castKind = stmt->getCastKind();

    if(!clang::ast_matchers::IsMatchingCast(castKind)) {
        InsertArg(subExpr);
        return;
    }

    if(isa<IntegerLiteral>(subExpr)) {
        InsertArg(stmt->IgnoreCasts());

    } else {
        const bool               isReinterpretCast{castKind == CastKind::CK_BitCast};
        static const std::string castName{isReinterpretCast ? "reinterpret_cast" : "static_cast"};
        const QualType           castDestType{stmt->getType().getCanonicalType()};
        const AsComment asComment = (!isReinterpretCast && isa<CXXThisExpr>(subExpr)) ? AsComment::Yes : AsComment::No;

        FormatCast(castName, castDestType, subExpr, castKind, asComment);
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const DeclRefExpr* stmt)
{
    mOutputFormatHelper.Append(GetName(*stmt));
    InsertTemplateArgs(*stmt);
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CompoundStmt* stmt)
{
    mOutputFormatHelper.OpenScope();

    HandleCompoundStmt(stmt);

    mOutputFormatHelper.CloseScope(OutputFormatHelper::NoNewLineBefore::Yes);
}
//-----------------------------------------------------------------------------

void CodeGenerator::HandleCompoundStmt(const CompoundStmt* stmt)
{
    for(const auto* item : stmt->body()) {
        InsertArg(item);

        if(!isa<IfStmt>(item) && !isa<ForStmt>(item) && !isa<DeclStmt>(item)) {
            mOutputFormatHelper.AppendNewLine(';');
        }
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const IfStmt* stmt)
{
    const std::string cexpr{stmt->isConstexpr() ? kwSpaceConstExpr : ""};
    const bool        hasInit{stmt->getInit() || stmt->getConditionVariable()};

    if(hasInit) {
        mOutputFormatHelper.OpenScope();

        if(const auto* conditionVar = stmt->getConditionVariable()) {
            InsertArg(conditionVar);
        }

        if(const auto* init = stmt->getInit()) {
            InsertArg(init);
        }
    }

    mOutputFormatHelper.Append("if", cexpr);

    WrapInParensOrCurlys(BraceKind::Parens, [&]() { InsertArg(stmt->getCond()); }, AddSpaceAtTheEnd::Yes);

    const auto* body = stmt->getThen();

    InsertArg(body);

    const bool isBodyBraced = isa<CompoundStmt>(body);

    if(!isBodyBraced && !isa<NullStmt>(body)) {
        mOutputFormatHelper.AppendNewLine(';');
    }

    // else
    if(const auto* elsePart = stmt->getElse()) {
        const std::string cexprElse{stmt->isConstexpr() ? StrCat("/* ", kwConstExprSpace, "*/ ") : ""};

        if(isBodyBraced) {
            mOutputFormatHelper.Append(' ');
        }

        mOutputFormatHelper.Append("else ", cexprElse);

        const bool needScope = isa<IfStmt>(elsePart);
        if(needScope) {
            mOutputFormatHelper.OpenScope();
        }

        InsertArg(elsePart);

        if(needScope) {
            mOutputFormatHelper.CloseScope();
        }
    }

    mOutputFormatHelper.AppendNewLine();

    if(hasInit) {
        mOutputFormatHelper.CloseScope();
        mOutputFormatHelper.AppendNewLine();
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const ForStmt* stmt)
{
    mOutputFormatHelper.Append("for");

    WrapInParensOrCurlys(BraceKind::Parens,
                         [&]() {
                             if(const auto* init = stmt->getInit()) {
                                 // the init-stmt carries a ; at the end
                                 InsertArg(init);
                             } else {
                                 mOutputFormatHelper.Append("; ");
                             }

                             InsertArg(stmt->getCond());
                             mOutputFormatHelper.Append("; ");

                             InsertArg(stmt->getInc());
                         },
                         AddSpaceAtTheEnd::Yes);

    mOutputFormatHelper.AppendNewLine();

    InsertArg(stmt->getBody());
    mOutputFormatHelper.AppendNewLine();
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CStyleCastExpr* stmt)
{
    static const std::string castName{"reinterpret_cast"};
    const QualType           castDestType = stmt->getType().getCanonicalType();

    FormatCast(castName, castDestType, stmt->getSubExpr(), stmt->getCastKind(), AsComment::No);
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXNewExpr* stmt)
{
    mOutputFormatHelper.Append("new ");

    if(stmt->getNumPlacementArgs()) {
        /* we have a placement new */

        WrapInParensOrCurlys(BraceKind::Parens, [&]() {
            ForEachArg(stmt->placement_arguments(), [&](const auto& placementArg) { InsertArg(placementArg); });
        });
    }

    Dump(stmt);
    Dump(stmt->getOperatorNew());

    if(const auto* ctorExpr = stmt->getConstructExpr()) {
        InsertArg(ctorExpr);

    } else {
        mOutputFormatHelper.Append(GetName(stmt->getAllocatedType()));

        if(stmt->isArray()) {
            mOutputFormatHelper.Append('[');
            InsertArg(stmt->getArraySize());
            mOutputFormatHelper.Append(']');
        }

        if(stmt->hasInitializer()) {
            InsertCurlysIfRequired(stmt->getInitializer());
        }
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const MaterializeTemporaryExpr* stmt)
{
    InsertArg(stmt->getTemporary());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXOperatorCallExpr* stmt)
{
    LAMBDA_SCOPE_HELPER(OperatorCallExpr);

    DPrint("args: %d\n", stmt->getNumArgs());

    Dump(stmt);

    const auto* callee      = dyn_cast_or_null<DeclRefExpr>(stmt->getCallee()->IgnoreImpCasts());
    const bool  isCXXMethod = [&]() { return (callee && isa<CXXMethodDecl>(callee->getDecl())); }();

    if(2 == stmt->getNumArgs()) {
        const auto* param1 = dyn_cast_or_null<DeclRefExpr>(stmt->getArg(0)->IgnoreImpCasts());
        const auto* param2 = dyn_cast_or_null<DeclRefExpr>(stmt->getArg(1)->IgnoreImpCasts());

        if(callee && param1 && param2) {

            const std::string replace = [&]() {
                if(isa<CXXMethodDecl>(callee->getDecl())) {
                    return StrCat(GetName(*param1), ".", GetName(*callee), "(", GetName(*param2), ")");
                } else {
                    return StrCat(GetName(*callee), "(", GetName(*param1), ", ", GetName(*param2), ")");
                }
            }();

            mOutputFormatHelper.Append(replace);

            return;
        }
    }

    auto        cb           = stmt->child_begin();
    const auto* fallbackArg0 = stmt->getArg(0);

    // arg0 := operator
    // skip arg0
    const auto* arg0 = cb->IgnoreImplicit();
    ++cb;

    const auto* arg1 = *cb;
    ++cb;

    if(const auto* dd = dyn_cast_or_null<DeclRefExpr>(arg0)) {
        const auto* decl = dd->getDecl();
        // at least std::cout boils down to a FunctionDecl at this point
        if(!isa<CXXMethodDecl>(decl) && !isa<FunctionDecl>(decl)) {
            // we have a global function not a member function operator. Skip this.
            return;
        }
    }

    // operators in a namespace but outside a class so operator goes first
    if(!isCXXMethod) {
        mOutputFormatHelper.Append(GetName(*callee), "(");
    }

    // insert the arguments
    if(isa<DeclRefExpr>(fallbackArg0)) {
        InsertArgWithParensIfNeeded(fallbackArg0);

    } else {
        InsertArgWithParensIfNeeded(arg1);
    }

    // if it is a class operator the operator follows now
    if(isCXXMethod) {
        const OverloadedOperatorKind opKind = stmt->getOperator();

        mOutputFormatHelper.Append(".operator", getOperatorSpelling(opKind), "(");
    }

    // consume all remaining arguments
    const auto childRange = llvm::make_range(cb, stmt->child_end());

    // at least the call-operator can have more than 2 parameters
    ForEachArg(childRange, [&](const auto& child) {
        if(!isCXXMethod) {
            // in global operators we need to separate the two parameters by comma
            mOutputFormatHelper.Append(", ");
        }

        InsertArg(child);
    });

    mOutputFormatHelper.Append(')');
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const LambdaExpr* stmt)
{
    if(!mLambdaStack.empty()) {
        HandleLambdaExpr(stmt, mLambdaStack.back());
        mOutputFormatHelper.Append(GetLambdaName(*stmt));
    } else {
        LAMBDA_SCOPE_HELPER(LambdaExpr);
        HandleLambdaExpr(stmt, mLambdaStack.back());
    }

    if(!mLambdaStack.empty()) {
        mLambdaStack.back().insertInits(mOutputFormatHelper);
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXThisExpr* stmt)
{
    DPrint("thisExpr: imlicit=%d %s\n", stmt->isImplicit(), GetName(GetDesugarType(stmt->getType())));

    mOutputFormatHelper.Append("this");
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXBindTemporaryExpr* stmt)
{
    InsertArg(stmt->getSubExpr());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXFunctionalCastExpr* stmt)
{
    const bool isConstructor{isa<CXXConstructExpr>(stmt->getSubExpr())};
    const bool isStdListInit{isa<CXXStdInitializerListExpr>(stmt->getSubExpr())};
    const bool isListInitialization{[&]() { return stmt->getLParenLoc().isInvalid(); }()};
    const bool needsParens{!isConstructor && !isListInitialization && !isStdListInit};

    // If a constructor follows we do not need to insert the type name. This would insert it twice.
    if(!isConstructor && !isStdListInit) {
        mOutputFormatHelper.Append(GetName(stmt->getTypeAsWritten()));
    }

    if(needsParens) {
        mOutputFormatHelper.Append('(');
    }

    InsertArg(stmt->getSubExpr());

    if(needsParens) {
        mOutputFormatHelper.Append(')');
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXBoolLiteralExpr* stmt)
{
    mOutputFormatHelper.Append(stmt->getValue() ? "true" : "false");
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const GNUNullExpr* /*stmt*/)
{
    mOutputFormatHelper.Append("NULL");
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CharacterLiteral* stmt)
{
    HandleCharacterLiteral(*stmt);
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const PredefinedExpr* stmt)
{
    InsertArg(stmt->getFunctionName());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const ExprWithCleanups* stmt)
{
    InsertArg(stmt->getSubExpr());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const TypeAliasDecl* stmt)
{
    mOutputFormatHelper.AppendNewLine("using ", GetName(*stmt), " = ", GetName(stmt->getUnderlyingType()), ";");
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const TypedefDecl* stmt)
{
    /* function pointer typedefs are special. Ease up things using "using" */
    //    outputFormatHelper.AppendNewLine("typedef ", GetName(stmt->getUnderlyingType()), " ", GetName(*stmt), ";");
    mOutputFormatHelper.AppendNewLine("using ", GetName(*stmt), " = ", GetName(stmt->getUnderlyingType()), ";");
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXMethodDecl* stmt)
{
    InsertAccessModifierAndNameWithReturnType(mOutputFormatHelper, *stmt, SkipConstexpr::No, SkipAccess::Yes);

    if(stmt->isDefaulted()) {
        mOutputFormatHelper.AppendNewLine(" = default;");
    } else if(stmt->isDeleted()) {
        mOutputFormatHelper.AppendNewLine(" = delete;");
    }

    if(!stmt->isUserProvided()) {
        return;
    }

    if(const auto* ctor = dyn_cast_or_null<CXXConstructorDecl>(stmt)) {
        bool first = true;

        for(const auto* init : ctor->inits()) {
            mOutputFormatHelper.AppendNewLine();
            if(first) {
                first = false;
                mOutputFormatHelper.Append(": ");
            } else {
                mOutputFormatHelper.Append(", ");
            }

            // in case of delegating or base initializer there is no member.
            if(const auto* member = init->getMember()) {
                mOutputFormatHelper.Append(member->getNameAsString());
                InsertCurlysIfRequired(init->getInit());
            } else {
                InsertArg(init->getInit());
            }
        }
    }

    if(stmt->hasBody()) {
        mOutputFormatHelper.AppendNewLine();
        InsertArg(stmt->getBody());
        mOutputFormatHelper.AppendNewLine();
    } else {
        mOutputFormatHelper.AppendNewLine(';');
    }

    mOutputFormatHelper.AppendNewLine();
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const FieldDecl* stmt)
{
    mOutputFormatHelper.AppendNewLine(GetName(stmt->getType()), " ", GetName(*stmt), ";");
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const AccessSpecDecl* stmt)
{
    mOutputFormatHelper.AppendNewLine();
    mOutputFormatHelper.AppendNewLine(AccessToStringWithColon(stmt->getAccess()));
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const StaticAssertDecl* stmt)
{
    if(!stmt->isFailed()) {
        mOutputFormatHelper.Append("/* PASSED: ");
    } else {
        mOutputFormatHelper.Append("/* FAILED: ");
    }

    mOutputFormatHelper.Append("static_assert(");

    InsertArg(stmt->getAssertExpr());

    if(stmt->getMessage()) {
        mOutputFormatHelper.Append(", ");
        InsertArg(stmt->getMessage());
    }

    mOutputFormatHelper.AppendNewLine("); */");
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const UsingDecl* stmt)
{
    mOutputFormatHelper.Append("using ");

    if(const DeclContext* Ctx = stmt->getDeclContext()) {
        bool isFunctionOrMethod{false};

        if(!Ctx->isFunctionOrMethod()) {

            using ContextsTy = SmallVector<const DeclContext*, 8>;
            ContextsTy Contexts;

            while(Ctx) {
                if(isa<NamedDecl>(Ctx)) {
                    Contexts.push_back(Ctx);
                }
                Ctx = Ctx->getParent();
            }

            for(const auto* DC : llvm::reverse(Contexts)) {
                if(const auto* Spec = dyn_cast<ClassTemplateSpecializationDecl>(DC)) {
                    mOutputFormatHelper.Append(Spec->getName());
                    InsertTemplateArgs(*Spec);

                } else if(const auto* ND = dyn_cast<NamespaceDecl>(DC)) {
                    if(ND->isAnonymousNamespace() || ND->isInline()) {
                        continue;
                    }

                    mOutputFormatHelper.Append(ND->getNameAsString());

                } else if(const auto* RD = dyn_cast<RecordDecl>(DC)) {
                    if(RD->getIdentifier()) {
                        mOutputFormatHelper.Append(RD->getNameAsString());
                    }

                } else if(const auto* FD = dyn_cast<FunctionDecl>(DC)) {
                    InsightsBase::GenerateFunctionPrototype(mOutputFormatHelper, *FD);

                } else if(const auto* ED = dyn_cast<EnumDecl>(DC)) {
                    if(!ED->isScoped()) {
                        continue;
                    }

                    mOutputFormatHelper.Append(ED->getNameAsString());

                } else {
                    mOutputFormatHelper.Append(cast<NamedDecl>(DC)->getNameAsString());
                }

                mOutputFormatHelper.Append("::");
            }
        } else {
            isFunctionOrMethod = true;
        }

        if(isFunctionOrMethod || stmt->getDeclName() || isa<DecompositionDecl>(stmt)) {
            mOutputFormatHelper.Append(stmt->getNameAsString());
        }
    }

    mOutputFormatHelper.AppendNewLine(';');
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXRecordDecl* stmt)
{
    // skip classes/struct's without a definition
    if(!stmt->hasDefinition()) {
        return;
    }

    if(stmt->isClass()) {
        mOutputFormatHelper.Append(kwClassSpace);

    } else {
        mOutputFormatHelper.Append("struct ");
    }

    mOutputFormatHelper.Append(GetName(*stmt));

    if(const auto* clsTmpl = dyn_cast_or_null<ClassTemplateSpecializationDecl>(stmt)) {
        InsertTemplateArgs(*clsTmpl);
    }

    if(stmt->getNumBases()) {
        mOutputFormatHelper.Append(" : ");

        ForEachArg(stmt->bases(), [&](const auto& base) {
            mOutputFormatHelper.Append(AccessToString(base.getAccessSpecifier()), " ", GetName(base.getType()));
        });
    }

    mOutputFormatHelper.AppendNewLine();

    mOutputFormatHelper.OpenScope();

    bool firstRecordDecl{true};
    for(const auto* d : stmt->decls()) {
        if(isa<CXXRecordDecl>(d) && firstRecordDecl) {
            firstRecordDecl = false;
            continue;
        }

        InsertArg(d);
    }

    mOutputFormatHelper.CloseScopeWithSemi();
    mOutputFormatHelper.AppendNewLine();
    mOutputFormatHelper.AppendNewLine();
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const DeclStmt* stmt)
{
    for(const auto* decl : stmt->decls()) {
        InsertArg(decl);
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const SubstNonTypeTemplateParmExpr* stmt)
{
    InsertArg(stmt->getReplacement());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const ReturnStmt* stmt)
{
    LAMBDA_SCOPE_HELPER(ReturnStmt);

    mOutputFormatHelper.Append("return");

    if(const auto* retVal = stmt->getRetValue()) {
        mOutputFormatHelper.Append(' ');
        InsertArg(retVal);
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const NullStmt* /*stmt*/)
{
    mOutputFormatHelper.AppendNewLine(';');
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXDefaultArgExpr* stmt)
{
    InsertArg(stmt->getExpr());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXStdInitializerListExpr* stmt)
{
    // No qualifiers like const or volatile here. This appears in  function calls or operators as a parameter. CV's are
    // not allowed there.
    mOutputFormatHelper.Append(GetName(stmt->getType(), Unqualified::Yes));
    InsertArg(stmt->getSubExpr());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const ExplicitCastExpr* stmt)
{
    InsertArg(stmt->getSubExpr());
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const CXXNullPtrLiteralExpr* /*stmt*/)
{
    mOutputFormatHelper.Append("nullptr");
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const Decl* stmt)
{
#define SUPPORTED_DECL(type)                                                                                           \
    if(isa<type>(stmt)) {                                                                                              \
        InsertArg(static_cast<const type*>(stmt));                                                                     \
        return;                                                                                                        \
    }

#define IGNORED_DECL SUPPORTED_DECL

#include "CodeGeneratorTypes.h"

    TODO(stmt, mOutputFormatHelper);
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArg(const Stmt* stmt)
{
    if(!stmt) {
        DPrint("Null stmt\n");
        return;
    }

#define SUPPORTED_STMT(type)                                                                                           \
    if(isa<type>(stmt)) {                                                                                              \
        InsertArg(dyn_cast_or_null<type>(stmt));                                                                       \
        return;                                                                                                        \
    }

#define IGNORED_STMT SUPPORTED_STMT

#include "CodeGeneratorTypes.h"

    TODO(stmt, mOutputFormatHelper);
}
//-----------------------------------------------------------------------------

void CodeGenerator::HandleCharacterLiteral(const CharacterLiteral& stmt)
{
    switch(stmt.getKind()) {
        case CharacterLiteral::Ascii: break;
        case CharacterLiteral::Wide: mOutputFormatHelper.Append('L'); break;
        case CharacterLiteral::UTF8: mOutputFormatHelper.Append("u8"); break;
        case CharacterLiteral::UTF16: mOutputFormatHelper.Append('u'); break;
        case CharacterLiteral::UTF32: mOutputFormatHelper.Append('U'); break;
    }

    switch(unsigned value = stmt.getValue()) {
        case '\\': mOutputFormatHelper.Append("'\\\\'"); break;
        case '\0': mOutputFormatHelper.Append("'\\0'"); break;
        case '\'': mOutputFormatHelper.Append("'\\''"); break;
        case '\a': mOutputFormatHelper.Append("'\\a'"); break;
        case '\b': mOutputFormatHelper.Append("'\\b'"); break;
        // FIXME: causes clang to report a non-standard escape sequence error
        // case '\e': mOutputFormatHelper.Append("'\\e'"); break;
        case '\f': mOutputFormatHelper.Append("'\\f'"); break;
        case '\n': mOutputFormatHelper.Append("'\\n'"); break;
        case '\r': mOutputFormatHelper.Append("'\\r'"); break;
        case '\t': mOutputFormatHelper.Append("'\\t'"); break;
        case '\v': mOutputFormatHelper.Append("'\\v'"); break;
        default:
            if((value & ~0xFFu) == ~0xFFu && stmt.getKind() == CharacterLiteral::Ascii) {
                value &= 0xFFu;
            }

            if(value < 256 && isPrintable(static_cast<unsigned char>(value))) {
                const std::string v{static_cast<char>(value)};
                mOutputFormatHelper.Append("'", v, "'");
            }
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::FormatCast(const std::string castName,
                               const QualType&   castDestType,
                               const Expr*       subExpr,
                               const CastKind&   castKind,
                               const AsComment   comment)
{
    const bool        isCastToBase{((castKind == CK_DerivedToBase) || (castKind == CK_UncheckedDerivedToBase)) &&
                            castDestType->isRecordType()};
    const std::string castDestTypeText{
        StrCat(GetName(castDestType), ((isCastToBase && !castDestType->isAnyPointerType()) ? "&" : ""))};

    if(AsComment::Yes == comment) {
        mOutputFormatHelper.Append("/*");
    }
    mOutputFormatHelper.Append(StrCat(castName, "<", castDestTypeText, ">("));
    InsertArg(subExpr);
    mOutputFormatHelper.Append(')');
    if(AsComment::Yes == comment) {
        mOutputFormatHelper.Append("*/");
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertArgWithParensIfNeeded(const Stmt* stmt)
{
    const bool needParens = [&]() {
        if(const auto* dest = dyn_cast_or_null<UnaryOperator>(stmt->IgnoreImplicit())) {
            if(dest->getOpcode() == clang::UO_Deref) {
                return true;
            }
        }

        return false;
    }();

    if(needParens) {
        mOutputFormatHelper.Append('(');
    }

    InsertArg(stmt);

    if(needParens) {
        mOutputFormatHelper.Append(')');
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertSuffix(const QualType& type)
{
    if(const auto* typePtr = type.getTypePtrOrNull()) {
        if(typePtr->isBuiltinType()) {
            if(const auto* bt = dyn_cast_or_null<BuiltinType>(typePtr)) {
                const auto kind = bt->getKind();

                mOutputFormatHelper.Append(GetBuiltinTypeSuffix(kind));
            }
        }
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertTemplateArgs(const ClassTemplateSpecializationDecl& clsTemplateSpe)
{
    if(const TypeSourceInfo* typeAsWritten = clsTemplateSpe.getTypeAsWritten()) {
        const TemplateSpecializationType* tmplSpecType = cast<TemplateSpecializationType>(typeAsWritten->getType());
        InsertTemplateArgs(tmplSpecType->template_arguments());
    } else {
        InsertTemplateArgs(clsTemplateSpe.getTemplateArgs().asArray());
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertTemplateArgs(const DeclRefExpr& stmt)
{
    if(stmt.getNumTemplateArgs()) {
        mOutputFormatHelper.Append('<');

        ForEachArg(stmt.template_arguments(), [&](const auto& arg) {
            const auto& targ = arg.getArgument();

            InsertTemplateArg(targ);
        });

        mOutputFormatHelper.Append('>');
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertTemplateArgs(const ArrayRef<TemplateArgument>& array)
{
    mOutputFormatHelper.Append('<');

    ForEachArg(array, [&](const auto& arg) { InsertTemplateArg(arg); });

    /* put as space between to closing brackets: >> -> > > */
    if(mOutputFormatHelper.GetString().back() == '>') {
        mOutputFormatHelper.Append(' ');
    }

    mOutputFormatHelper.Append('>');
}
//-----------------------------------------------------------------------------

void CodeGenerator::HandleTemplateParameterPack(const ArrayRef<TemplateArgument>& args)
{
    ForEachArg(args, [&](const auto& arg) { InsertTemplateArg(arg); });
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertTemplateArg(const TemplateArgument& arg)
{
    switch(arg.getKind()) {
        case TemplateArgument::Type: mOutputFormatHelper.Append(GetName(arg.getAsType())); break;
        case TemplateArgument::Declaration:
            mOutputFormatHelper.Append(GetNameAsFunctionPointer(arg.getAsDecl()->getType()));
            break;
        case TemplateArgument::NullPtr: mOutputFormatHelper.Append(GetName(arg.getNullPtrType())); break;
        case TemplateArgument::Integral: mOutputFormatHelper.Append(arg.getAsIntegral()); break;
        case TemplateArgument::Expression: InsertArg(arg.getAsExpr()); break;
        case TemplateArgument::Pack: HandleTemplateParameterPack(arg.pack_elements()); break;
        case TemplateArgument::Template:
            mOutputFormatHelper.Append(GetName(*arg.getAsTemplate().getAsTemplateDecl()));
            break;
        case TemplateArgument::TemplateExpansion:
            mOutputFormatHelper.Append(GetName(*arg.getAsTemplateOrTemplatePattern().getAsTemplateDecl()));
            break;
        case TemplateArgument::Null: mOutputFormatHelper.Append("null"); break;
    }
}
//-----------------------------------------------------------------------------

void CodeGenerator::HandleLocalStaticNonTrivialClass(const VarDecl* stmt)
{
    const auto*       cxxRecordDecl = stmt->getType()->getAsCXXRecordDecl();
    const std::string internalVarName{BuildInternalVarName(GetName(*stmt))};
    const std::string compilerBoolVarName{StrCat(internalVarName, "B")};
    const std::string typeName{GetName(*cxxRecordDecl)};

    // insert compiler bool to track init state
    mOutputFormatHelper.AppendNewLine("static bool ", compilerBoolVarName, ";");

    // insert compiler memory place holder
    mOutputFormatHelper.AppendNewLine("static char ", internalVarName, "[sizeof(", typeName, ")];");

    // insert compiler init if
    mOutputFormatHelper.AppendNewLine();

    mOutputFormatHelper.AppendNewLine("if( ! ", compilerBoolVarName, " )");
    mOutputFormatHelper.OpenScope();

    mOutputFormatHelper.AppendNewLine("new (&", internalVarName, ") ", typeName, ";");

    mOutputFormatHelper.AppendNewLine(compilerBoolVarName, " = true;");
    mOutputFormatHelper.CloseScope(OutputFormatHelper::NoNewLineBefore::Yes);
    mOutputFormatHelper.AppendNewLine();
}
//-----------------------------------------------------------------------------

const char* CodeGenerator::GetKind(const UnaryExprOrTypeTraitExpr& uk)
{
    switch(uk.getKind()) {
        case UETT_SizeOf: return "sizeof";
        case UETT_AlignOf: return "alignof";
        default: break;
    };

    return "unknown";
}
//-----------------------------------------------------------------------------

const char* CodeGenerator::GetOpcodeName(const int kind)
{
    switch(kind) {
        default: return "???";

#define UNARY_OPERATION(Name, Spelling)                                                                                \
    case UO_##Name:                                                                                                    \
        return Spelling;

#include "clang/AST/OperationKinds.def"

#undef UNARY_OPERATION
    }
    // llvm_unreachable("Not an overloaded allocation operator");
}
//-----------------------------------------------------------------------------

const char* CodeGenerator::GetBuiltinTypeSuffix(const BuiltinType& type)
{
#define CASE(K, retVal)                                                                                                \
    case BuiltinType::K: return retVal
    switch(type.getKind()) {
        CASE(Bool, "");
        CASE(Char_U, "");
        CASE(UChar, "");
        CASE(Char16, "");
        CASE(Char32, "");
        CASE(UShort, "");
        CASE(UInt, "u");
        CASE(ULong, "ul");
        CASE(ULongLong, "ull");
        CASE(UInt128, "ulll");
        CASE(Char_S, "");
        CASE(SChar, "");
        CASE(Short, "");
        CASE(Int, "");
        CASE(Long, "l");
        CASE(LongLong, "ll");
        CASE(Int128, "");
        CASE(Float, "f");
        CASE(Double, "");
        CASE(LongDouble, "L");
        CASE(WChar_S, "");
        CASE(WChar_U, "");
        default: return "";
    }
#undef BTCASE
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertMethod(const Decl*          d,
                                 OutputFormatHelper&  outputFormatHelper,
                                 const CXXMethodDecl& md,
                                 bool /*skipConstexpr*/)
{
    if(const auto* m = dyn_cast_or_null<CXXMethodDecl>(d)) {
        InsertAccessModifierAndNameWithReturnType(outputFormatHelper, *m, SkipConstexpr::Yes);
        outputFormatHelper.AppendNewLine();

        LambdaCodeGenerator lambdaCodeGenerator{outputFormatHelper, mLambdaStack};
        CodeGenerator&      codeGenerator{lambdaCodeGenerator};
        codeGenerator.InsertArg(md.getBody());
        outputFormatHelper.AppendNewLine();
    }
}
//-----------------------------------------------------------------------------

/// \brief Get a correct type for an array.
///
/// This is a special case for lambdas. The QualType of the VarDecl we are looking at could be a plain type. But if we
/// capture via reference, obviously we need to a a reference. This is why the more general version does not work here.
/// Probably needs improvement.
static std::string GetCaptureTypeNameAsParameter(const QualType& t, const std::string& varName)
{
    std::string typeName = GetName(t);

    if(t->isArrayType()) {
        InsertBefore(typeName, "[", StrCat("(&", varName, ")"));
    }

    return typeName;
}
//-----------------------------------------------------------------------------

void CodeGenerator::HandleLambdaExpr(const LambdaExpr* lambda, LambdaHelper& lambdaHelper)
{
    const LambdaCallerType lambdaCallerType   = lambdaHelper.callerType();
    OutputFormatHelper&    outputFormatHelper = lambdaHelper.buffer();

    outputFormatHelper.AppendNewLine();

    const std::string lambdaTypeName{GetLambdaName(*lambda->getLambdaClass())};
    outputFormatHelper.AppendNewLine(kwClassSpace, lambdaTypeName);
    outputFormatHelper.OpenScope();

    const auto& callOp      = *lambda->getCallOperator();
    const auto& lambdaClass = *lambda->getLambdaClass();

    if(lambda->isGenericLambda()) {
        bool       haveConversionOperator{false};
        const auto conversions = llvm::make_range(lambdaClass.conversion_begin(), lambdaClass.conversion_end());
        for(auto&& conversion : conversions) {
            for(const auto* s : conversion->getAsFunction()->getDescribedFunctionTemplate()->specializations()) {
                if(const auto* cxxmd = dyn_cast_or_null<CXXMethodDecl>(s)) {
                    haveConversionOperator = true;
                    InsertMethod(s, outputFormatHelper, *cxxmd, false);
                }
            }

            DPrint("-----\n");
        }

        for(const auto* o : lambdaClass.getLambdaCallOperator()->getDescribedFunctionTemplate()->specializations()) {
            InsertMethod(o, outputFormatHelper, *lambdaClass.getLambdaCallOperator(), false);
        }

        if(haveConversionOperator && lambdaClass.getLambdaStaticInvoker()) {
            for(const auto* iv :
                lambdaClass.getLambdaStaticInvoker()->getDescribedFunctionTemplate()->specializations()) {
                DPrint("invoker:\n");

                InsertMethod(iv, outputFormatHelper, *lambdaClass.getLambdaCallOperator(), false);
            }
        }

    } else {
        bool       haveConversionOperator{false};
        const auto conversions = llvm::make_range(lambdaClass.conversion_begin(), lambdaClass.conversion_end());
        for(auto&& conversion : conversions) {
            const auto* func = conversion->getAsFunction();

            if(const auto* cxxmd = dyn_cast_or_null<CXXMethodDecl>(func)) {
                /* looks like a conversion operator is (often) there but sometimes undeduced. e.g. still has return
                 * type auto and no body. We do not want these functions. */
                if(cxxmd->hasBody()) {
                    haveConversionOperator = true;
                    InsertMethod(func, outputFormatHelper, *cxxmd, false);
                }
            }

            DPrint("-----\n");
        }

        InsertMethod(&callOp, outputFormatHelper, callOp, false);

        if(haveConversionOperator && lambdaClass.getLambdaStaticInvoker()) {
            InsertMethod(
                lambdaClass.getLambdaStaticInvoker(), outputFormatHelper, *lambdaClass.getLambdaCallOperator(), false);
        }
    }

    /*
     *   class xx
     *   {
     *      x _var1{var1}
     *      ...
     *
     *      RET operator()() MUTABLE
     *      {
     *        BODY
     *      }
     *
     *   };
     *
     */

    std::string ctor{StrCat("public: ", lambdaTypeName, "(")};
    std::string ctorInits{": "};
    std::string inits("{");

    if(0 != lambda->capture_size()) {
        outputFormatHelper.AppendNewLine();
        outputFormatHelper.Append("private:");
    }

    DPrint("captures\n");
    bool        first{true};
    bool        ctorRequired{false};
    const auto* captureInits = lambda->capture_init_begin();
    for(const auto& c : lambda->captures()) {
        const auto* captureInit = *captureInits;
        ++captureInits;
        ctorRequired = true;

        if(!c.capturesVariable() && !c.capturesThis()) {
            // This also catches VLA captures
            if(!c.capturesVLAType()) {
                Error(captureInit, "no capture var\n");
            }
            continue;
        }

        if(first) {
            first = false;
            outputFormatHelper.AppendNewLine();
        } else {
            ctor.append(", ");
            inits.append(", ");
            ctorInits.append("\n, ");
        }

        const auto* capturedVar = c.getCapturedVar();
        const auto& varType     = [&]() {
            if(c.capturesThis()) {
                return captureInit->getType();
            }

            return capturedVar->getType();
        }();

        const std::string varNamePlain = [&]() {
            if(c.capturesThis()) {
                return std::string{"this"};
            }

            return GetName(*capturedVar);
        }();

        DPrint("plain name: %s\n", varNamePlain);

        const std::string varName = [&]() {
            if(c.capturesThis()) {
                return StrCat("__", varNamePlain);
            }

            return varNamePlain;
        }();

        const std::string varTypeName     = GetCaptureTypeNameAsParameter(varType, varNamePlain);
        const std::string ctorVarTypeName = GetCaptureTypeNameAsParameter(varType, StrCat("_", varNamePlain));

        DPrint("%s\n", varTypeName);

        ctor.append(ctorVarTypeName);

        outputFormatHelper.Append(varTypeName);

        const auto captureKind = c.getCaptureKind();
        switch(captureKind) {
            case LCK_This: break;
            case LCK_StarThis: break;
            case LCK_ByCopy: break;
            case LCK_VLAType: break;  //  unreachable
            case LCK_ByRef:
                /* varTypeName already carries the & in case we capture a reference by reference, we need to skip it in
                 * case of an array */
                if(!varType->isReferenceType() && !varType->isArrayType()) {
                    ctor.append("&");
                    outputFormatHelper.Append("&");
                }
                break;
        }

        // If we initialize by copy we can assign a variable: [a=b[1]], get this assigned variable (b[1]) and not a in
        // this case.
        if(!c.capturesThis() && capturedVar->hasInit() && (captureKind == LCK_ByCopy)) {
            OutputFormatHelper ofm{};
            CodeGenerator      codeGenerator{ofm, mLambdaStack};
            codeGenerator.InsertArg(captureInit);
            inits.append(ofm.GetString());
        } else {
            inits.append(StrCat(((c.getCaptureKind() == LCK_StarThis) ? "*" : ""), varNamePlain));
        }

        if(!varType->isArrayType()) {
            ctor.append(StrCat(" _", varName));
            outputFormatHelper.AppendNewLine(" ", varName, ";");
        } else {
            outputFormatHelper.AppendNewLine(';');
        }

        ctorInits.append(StrCat(varName, "{_", varName, "}"));
    }

    ctor.append(")");
    inits.append("}");

    if(ctorRequired) {
        outputFormatHelper.AppendNewLine("");
        outputFormatHelper.AppendNewLine(ctor);
        outputFormatHelper.AppendNewLine(ctorInits);
        outputFormatHelper.AppendNewLine("{}");
    }

    // close the class scope
    outputFormatHelper.CloseScope();

    if((LambdaCallerType::VarDecl != lambdaCallerType) && (LambdaCallerType::CallExpr != lambdaCallerType)) {
        outputFormatHelper.Append(" ", GetLambdaName(*lambda), inits);
    } else {
        mLambdaStack.back().inits().append(inits);
    }

    outputFormatHelper.AppendNewLine(';');
    outputFormatHelper.AppendNewLine();
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertAccessModifierAndNameWithReturnType(OutputFormatHelper&  outputFormatHelper,
                                                              const CXXMethodDecl& decl,
                                                              const SkipConstexpr  skipConstexpr,
                                                              const SkipAccess     skipAccess)
{
    if(SkipAccess::No == skipAccess) {
        outputFormatHelper.Append(AccessToStringWithColon(decl));
    }

    // types of conversion decls can be invalid to type at this place. So introduce a using
    if(isa<CXXConversionDecl>(decl)) {
        outputFormatHelper.AppendNewLine("using retType = ", GetName(GetDesugarReturnType(decl)), ";");
    }

    if(decl.isInlined()) {
        outputFormatHelper.Append(kwInlineSpace);
    }

    if(decl.isStatic()) {
        outputFormatHelper.Append(kwStaticSpace);
    }

    if(decl.isVirtual()) {
        outputFormatHelper.Append(kwVirtualSpace);
    }

    if(decl.isVolatile()) {
        outputFormatHelper.Append(kwVolatileSpace);
    }

    if(decl.isConstexpr()) {
        if(SkipConstexpr::Yes == skipConstexpr) {
            outputFormatHelper.Append("/*");
        }

        outputFormatHelper.Append(kwConstExprSpace);

        if(SkipConstexpr::Yes == skipConstexpr) {
            outputFormatHelper.Append("*/ ");
        }
    }

    if(!isa<CXXConstructorDecl>(decl) && !isa<CXXDestructorDecl>(decl)) {
        if(isa<CXXConversionDecl>(decl)) {
            outputFormatHelper.Append("operator retType (");
        } else {
            outputFormatHelper.Append(GetName(GetDesugarReturnType(decl)), " ");
        }
    }

    if(!isa<CXXConversionDecl>(decl)) {
        outputFormatHelper.Append(GetName(decl), "(");
    }

    outputFormatHelper.AppendParameterList(decl.parameters(), OutputFormatHelper::WithParameterName::Yes);
    outputFormatHelper.Append(")", GetConst(decl), GetNoExcept(decl));
}
//-----------------------------------------------------------------------------

void CodeGenerator::InsertCurlysIfRequired(const Stmt* stmt)
{
    const bool requiresCurlys{!isa<InitListExpr>(stmt) && !isa<ParenExpr>(stmt) && !isa<CXXDefaultInitExpr>(stmt)};

    if(requiresCurlys) {
        mOutputFormatHelper.Append('{');
    }

    InsertArg(stmt);

    if(requiresCurlys) {
        mOutputFormatHelper.Append('}');
    }
}
//-----------------------------------------------------------------------------

template<typename T>
void CodeGenerator::WrapInParensOrCurlys(const BraceKind braceKind, T&& lambda, const AddSpaceAtTheEnd addSpaceAtTheEnd)
{
    if(BraceKind::Curlys == braceKind) {
        mOutputFormatHelper.Append('{');
    } else {
        mOutputFormatHelper.Append('(');
    }

    lambda();

    if(BraceKind::Curlys == braceKind) {
        mOutputFormatHelper.Append('}');
    } else {
        mOutputFormatHelper.Append(')');
    }

    if(AddSpaceAtTheEnd::Yes == addSpaceAtTheEnd) {
        mOutputFormatHelper.Append(' ');
    }
}
//-----------------------------------------------------------------------------

void StructuredBindingsCodeGenerator::InsertArg(const DeclRefExpr* stmt)
{
    const auto name = GetName(*stmt);
    mOutputFormatHelper.Append(name);

    if(name.empty() || EndsWith(name, "::")) {
        mOutputFormatHelper.Append(mVarName);
    } else {
        InsertTemplateArgs(*stmt);
    }
}
//-----------------------------------------------------------------------------

void LambdaCodeGenerator::InsertArg(const CXXThisExpr* stmt)
{
    DPrint("thisExpr: imlicit=%d %s\n", stmt->isImplicit(), GetName(GetDesugarType(stmt->getType())));

    mOutputFormatHelper.Append("__this");
}
//-----------------------------------------------------------------------------

}  // namespace clang::insights
