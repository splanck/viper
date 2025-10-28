// File: src/frontends/basic/Parser.hpp
// Purpose: Declares BASIC parser producing Program with separate procedure and
//          main statement lists.
// Key invariants: Maintains token lookahead buffer.
// Ownership/Lifetime: Parser owns lexer and token buffer.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lexer.hpp"
#include "frontends/basic/StatementSequencer.hpp"
#include "frontends/basic/ast/DeclNodes.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"
#include "frontends/basic/ast/StmtNodesAll.hpp"
#include "support/diag_expected.hpp"
#include <array>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace il::frontends::basic
{

class Parser
{
  public:
    /// @brief Construct a parser over a BASIC source buffer.
    /// @param src Source code to parse.
    /// @param file_id Identifier used for diagnostics.
    /// @param emitter Optional diagnostic emitter; not owned.
    Parser(std::string_view src, uint32_t file_id, DiagnosticEmitter *emitter = nullptr);

    /// @brief Parse the entire BASIC program.
    /// @return Program AST on success or nullptr on failure.
    std::unique_ptr<Program> parseProgram();

  private:
    template <class T> using ErrorOr = il::support::Expected<T>;

    friend class StatementSequencer;

    /// @brief Create a statement sequencer bound to this parser instance.
    /// @return StatementSequencer referencing the parser's token stream.
    StatementSequencer statementSequencer();

    /// @brief Consume optional line labels that follow a line break.
    /// @param ctx Statement sequencer providing newline skipping helpers.
    /// @param followerKinds When non-empty, only consume the label when the
    ///        subsequent token is one of the specified kinds.
    void skipOptionalLineLabelAfterBreak(StatementSequencer &ctx,
                                         std::initializer_list<TokenKind> followerKinds = {});

    /// @brief Parse the body of a single IF-related branch.
    /// @param line Line number propagated to the branch statement.
    /// @param ctx Statement sequencer for separator management.
    /// @return Parsed statement belonging to the branch body.
    StmtPtr parseIfBranchBody(int line, StatementSequencer &ctx);

    mutable Lexer lexer_;                             ///< Provides tokens from the source buffer.
    mutable std::vector<Token> tokens_;               ///< Lookahead token buffer.
    DiagnosticEmitter *emitter_ = nullptr;            ///< Diagnostic sink; not owned.
    std::unordered_set<std::string> arrays_;          ///< Names of arrays declared via DIM.
    std::unordered_set<std::string> knownProcedures_; ///< Procedure identifiers seen so far.
    std::unordered_set<int> usedLabelNumbers_;        ///< Numeric labels already assigned.

    struct NamedLabelEntry
    {
        int number = 0;                        ///< Synthesised numeric identifier for the label.
        bool defined = false;                  ///< True once the label definition has been seen.
        il::support::SourceLoc definitionLoc;  ///< Location of the defining identifier.
        bool referenced = false;               ///< True when the label was referenced in source.
        il::support::SourceLoc referenceLoc;   ///< First location the label was referenced.
    };

    int allocateSyntheticLabelNumber();
    int ensureLabelNumber(const std::string &name);
    bool hasLabelName(const std::string &name) const;
    std::optional<int> lookupLabelNumber(const std::string &name) const;
    void noteNamedLabelDefinition(const Token &tok, int labelNumber);
    void noteNamedLabelReference(const Token &tok, int labelNumber);
    void noteNumericLabelUsage(int labelNumber);

    std::unordered_map<std::string, NamedLabelEntry> namedLabels_; ///< Mapping from label names to ids.
    int nextSyntheticLabel_ = 1'000'000;                           ///< Next synthesised label id candidate.

    /// @brief Registry that maps statement-leading tokens to parser callbacks.
    class StatementParserRegistry
    {
      public:
        using NoArgHandler = StmtPtr (Parser::*)();
        using WithLineHandler = StmtPtr (Parser::*)(int);

        /// @brief Register handler without an explicit line parameter.
        void registerHandler(TokenKind kind, NoArgHandler handler);

        /// @brief Register handler that requires the originating line number.
        void registerHandler(TokenKind kind, WithLineHandler handler);

        /// @brief Lookup registered handler for @p kind.
        /// @return Pointer pair containing callbacks if present.
        [[nodiscard]] std::pair<NoArgHandler, WithLineHandler> lookup(TokenKind kind) const;

        /// @brief Check whether @p kind begins a statement according to the registry.
        [[nodiscard]] bool contains(TokenKind kind) const;

      private:
        std::array<std::pair<NoArgHandler, WithLineHandler>,
                   static_cast<std::size_t>(TokenKind::Count)>
            entries_{};
    };

    static const StatementParserRegistry &statementRegistry();
    static StatementParserRegistry buildStatementRegistry();
    static void registerControlFlowParsers(StatementParserRegistry &registry);
    static void registerRuntimeParsers(StatementParserRegistry &registry);
    static void registerIoParsers(StatementParserRegistry &registry);
    static void registerCoreParsers(StatementParserRegistry &registry);
    static void registerOopParsers(StatementParserRegistry &registry);
    StmtPtr parseClassDecl();
    StmtPtr parseTypeDecl();
    StmtPtr parseDeleteStatement();

#include "frontends/basic/Parser_Token.hpp"

    /// @brief Parse a single BASIC statement at the given line number.
    /// @param line Line number associated with the statement.
    /// @return Parsed statement or nullptr on error.
    StmtPtr parseStatement(int line);

    /// @brief Identify whether the lookahead token begins a new statement.
    /// @param kind Token kind to classify.
    /// @return True when a handler or structural keyword marks the start of a new statement.
    bool isStatementStart(TokenKind kind) const;

    /// @brief Parse a PRINT statement.
    /// @return PRINT statement node.
    StmtPtr parsePrintStatement();

    /// @brief Parse a WRITE # statement.
    /// @return WRITE statement node targeting a file channel.
    StmtPtr parseWriteStatement();

    /// @brief Parse a LET assignment statement.
    /// @return LET statement node.
    StmtPtr parseLetStatement();

    /// @brief Parse an IF statement starting at @p line.
    /// @param line Line number of the IF keyword.
    /// @return IF statement node.
    StmtPtr parseIfStatement(int line);

    /// @brief Parse a WHILE loop.
    /// @return WHILE statement node.
    StmtPtr parseWhileStatement();

    /// @brief Parse a SELECT CASE statement with optional CASE ELSE.
    /// @return SELECT CASE statement node.
    StmtPtr parseSelectCaseStatement();

    /// @brief Status information returned by CASE parsing helpers.
    struct SelectHandlerResult
    {
        bool handled = false;           ///< True when helper consumed tokens.
        bool emittedDiagnostic = false; ///< True when helper reported errors.
    };

    /// @brief Aggregates CASE body collection results.
    struct SelectBodyResult
    {
        std::vector<StmtPtr> body;                     ///< Statements collected.
        StatementSequencer::TerminatorInfo terminator; ///< Terminator metadata.
        bool emittedDiagnostic = false;                ///< Diagnostics emitted.
    };

    /// @brief Captures inline CASE body statements gathered after a colon.
    struct SelectInlineBodyResult
    {
        std::vector<StmtPtr> body; ///< Statements parsed on the same source line.
        Token terminator;          ///< End-of-line token that closed the inline body.
    };

    using SelectDiagnoseFn =
        std::function<void(il::support::SourceLoc, uint32_t, std::string_view, std::string_view)>;

    struct SelectParseState
    {
        std::unique_ptr<SelectCaseStmt> stmt;
        SelectDiagnoseFn diagnose;
        il::support::SourceLoc selectLoc;
        bool sawCaseArm = false;
        bool sawCaseElse = false;
        bool expectEndSelect = true;
    };

    enum class SelectDispatchAction
    {
        None,
        Continue,
        Terminate,
    };

    SelectParseState parseSelectHeader();
    void parseSelectArms(SelectParseState &state);
    bool parseSelectElse(SelectParseState &state);
    SelectDispatchAction dispatchSelectDirective(SelectParseState &state);

    /// @brief Collect a CASE/CASE ELSE body until the next arm or END SELECT.
    /// @return Aggregated statements and terminator metadata.
    SelectBodyResult collectSelectBody();

    /// @brief Parse colon-terminated statements that immediately follow a CASE header.
    /// @return Statements parsed before the end-of-line terminator.
    SelectInlineBodyResult collectInlineSelectBody();

    /// @brief Handle END SELECT terminator encountered while parsing.
    /// @param stmt Statement under construction whose range gets extended.
    /// @param sawCaseArm Whether a CASE arm has been parsed so far.
    /// @param expectEndSelect Flag tracking whether END SELECT is still required.
    /// @param diagnose Diagnostic callback mirroring parser emission.
    /// @return Helper status describing token consumption and diagnostics.
    SelectHandlerResult handleEndSelect(SelectCaseStmt &stmt,
                                        bool sawCaseArm,
                                        bool &expectEndSelect,
                                        const SelectDiagnoseFn &diagnose);

    /// @brief Parse CASE ELSE arm when encountered at the current position.
    /// @param stmt Statement receiving the CASE ELSE body.
    /// @param sawCaseArm Whether at least one CASE arm preceded the ELSE.
    /// @param sawCaseElse Tracks whether a CASE ELSE has already appeared.
    /// @param diagnose Diagnostic callback mirroring parser emission.
    /// @return Helper status describing token consumption and diagnostics.
    SelectHandlerResult consumeCaseElse(SelectCaseStmt &stmt,
                                        bool sawCaseArm,
                                        bool &sawCaseElse,
                                        const SelectDiagnoseFn &diagnose);

    struct Cursor;
    struct CaseArmSyntax;

    il::support::Expected<CaseArmSyntax> parseCaseArmSyntax(Cursor &cursor);
    il::support::Expected<CaseArm> lowerCaseArm(const CaseArmSyntax &syntax);
    ErrorOr<void> validateCaseArm(const CaseArm &arm);

    /// @brief Parse a CASE arm including label list and statement body.
    /// @return Parsed CASE arm.
    CaseArm parseCaseArm();

    /// @brief Parse the CASE ELSE body until END SELECT.
    /// @return Statements contained within CASE ELSE and the location of the
    ///         terminating end-of-line.
    std::pair<std::vector<StmtPtr>, il::support::SourceLoc> parseCaseElseBody();

    struct IfParseState
    {
        std::unique_ptr<IfStmt> stmt;
        int line = 0;
        il::support::SourceLoc loc;
    };

    IfParseState parseIfHeader(int line);
    void parseIfBlock(IfParseState &state);
    void parseElseChain(IfParseState &state);

    /// @brief Parse a DO ... LOOP statement.
    /// @return DO statement node with optional tests.
    StmtPtr parseDoStatement();

    /// @brief Parse a FOR loop.
    /// @return FOR statement node.
    StmtPtr parseForStatement();

    /// @brief Parse a NEXT statement closing a loop.
    /// @return NEXT statement node.
    StmtPtr parseNextStatement();

    /// @brief Parse an EXIT statement identifying the loop kind.
    /// @return EXIT statement node.
    StmtPtr parseExitStatement();

    /// @brief Parse a GOTO statement.
    /// @return GOTO statement node.
    StmtPtr parseGotoStatement();

    /// @brief Parse a GOSUB statement.
    /// @return GOSUB statement node.
    StmtPtr parseGosubStatement();

    /// @brief Parse an OPEN statement configuring file I/O.
    /// @return OPEN statement node.
    StmtPtr parseOpenStatement();

    /// @brief Parse a CLOSE statement releasing a channel.
    /// @return CLOSE statement node.
    StmtPtr parseCloseStatement();

    /// @brief Parse a SEEK statement repositioning a channel.
    /// @return SEEK statement node.
    StmtPtr parseSeekStatement();

    /// @brief Parse an ON ERROR GOTO statement.
    /// @return ON ERROR statement node.
    StmtPtr parseOnErrorGotoStatement();

    /// @brief Parse an END statement.
    /// @return END statement node.
    StmtPtr parseEndStatement();

    /// @brief Parse an INPUT statement.
    /// @return INPUT statement node.
    StmtPtr parseInputStatement();

    /// @brief Parse a LINE INPUT # statement.
    /// @return LINE INPUT statement node.
    StmtPtr parseLineInputStatement();

    /// @brief Parse a RESUME statement.
    /// @return RESUME statement node.
    StmtPtr parseResumeStatement();

    /// @brief Parse a DIM statement defining arrays.
    /// @return DIM statement node.
    StmtPtr parseDimStatement();

    /// @brief Parse a REDIM statement resizing arrays.
    /// @return REDIM statement node.
    StmtPtr parseReDimStatement();

    /// @brief Parse a RANDOMIZE statement.
    /// @return RANDOMIZE statement node.
    StmtPtr parseRandomizeStatement();

    /// @brief Parse a CLS statement clearing the display.
    /// @return CLS statement node.
    StmtPtr parseClsStatement();

    /// @brief Parse a COLOR statement adjusting the palette.
    /// @return COLOR statement node.
    StmtPtr parseColorStatement();

    /// @brief Parse a LOCATE statement moving the cursor.
    /// @return LOCATE statement node.
    StmtPtr parseLocateStatement();

    /// @brief Parse a FUNCTION definition including body.
    /// @return FUNCTION statement node.
    StmtPtr parseFunctionStatement();

    /// @brief Parse the header of a FUNCTION without its body.
    /// @return Newly allocated function declaration.
    std::unique_ptr<FunctionDecl> parseFunctionHeader();

    /// @brief Parse the body of a function and attach statements to @p fn.
    /// @param fn Function declaration to populate.
    void parseFunctionBody(FunctionDecl *fn);

    /// @brief Parse a sequence of statements for a procedure-like declaration.
    /// @param endKind Token that must follow END to terminate the body.
    /// @param body Destination vector receiving parsed statements.
    /// @return Location of the END keyword; invalid if the keyword is absent.
    il::support::SourceLoc parseProcedureBody(TokenKind endKind, std::vector<StmtPtr> &body);

    /// @brief Remember a procedure name for later diagnostics.
    /// @param name BASIC identifier of the procedure.
    void noteProcedureName(std::string name);

    /// @brief Check whether @p name has been seen as a procedure declaration.
    /// @param name Identifier to test.
    /// @return True when @p name is a known procedure.
    bool isKnownProcedureName(const std::string &name) const;

    /// @brief Parse a SUB definition including body.
    /// @return SUB statement node.
    StmtPtr parseSubStatement();

    /// @brief Parse a RETURN statement.
    /// @return RETURN statement node.
    StmtPtr parseReturnStatement();

    /// @brief Parse a comma-separated parameter list inside parentheses.
    /// @return Vector of parsed parameters.
    std::vector<Param> parseParamList();

    /// @brief Determine BASIC type from identifier suffix.
    /// @param name Identifier possibly carrying a type suffix ($, %, &, !, #).
    /// @return Resolved type.
    Type typeFromSuffix(std::string_view name);

    /// @brief Parse a BASIC type keyword following AS.
    /// @return Resolved BASIC type, defaults to I64 on mismatch.
    Type parseTypeKeyword();

    /// @brief Parse an expression using precedence climbing.
    /// @param min_prec Minimum precedence to enforce.
    /// @return Parsed expression node.
    ExprPtr parseExpression(int min_prec = 0);

    /// @brief Parse unary operators and delegate to primaries when absent.
    /// @return Parsed unary expression node.
    ExprPtr parseUnary();

    /// @brief Parse infix operators with precedence handling.
    /// @param min_prec Minimum precedence to enforce.
    /// @return Parsed expression node.
    ExprPtr parseBinary(int min_prec);

    /// @brief Parse a primary expression such as literals or parenthesized forms.
    /// @return Parsed primary expression node.
    ExprPtr parsePrimary();

    /// @brief Parse postfix member access or method invocation chains.
    /// @param expr Expression to extend.
    /// @return Expression with postfix operations applied.
    ExprPtr parsePostfix(ExprPtr expr);

    /// @brief Parse a NEW expression allocating a class instance.
    /// @return Newly allocated expression node.
    ExprPtr parseNewExpression();

    /// @brief Parse LBOUND/UBOUND intrinsics.
    /// @param keyword Token identifying which bound to read.
    /// @return Parsed intrinsic expression node.
    ExprPtr parseBoundIntrinsic(TokenKind keyword);

    /// @brief Parse LOF/EOF/LOC file channel intrinsics.
    /// @param keyword Token identifying the intrinsic.
    /// @return Parsed intrinsic expression node.
    ExprPtr parseChannelIntrinsic(TokenKind keyword);

    /// @brief Parse a numeric literal expression.
    /// @return Parsed number expression node.
    ExprPtr parseNumber();

    /// @brief Parse a string literal expression.
    /// @return Parsed string expression node.
    ExprPtr parseString();

    /// @brief Parse a call to a builtin function.
    /// @param builtin Which builtin is being invoked.
    /// @param loc Source location of the call.
    /// @return Parsed builtin call expression node.
    ExprPtr parseBuiltinCall(BuiltinCallExpr::Builtin builtin, il::support::SourceLoc loc);

    /// @brief Parse a reference to a variable.
    /// @param name Variable identifier.
    /// @param loc Source location of the identifier.
    /// @return Variable reference expression node.
    ExprPtr parseVariableRef(std::string name, il::support::SourceLoc loc);

    /// @brief Parse a reference to an array element.
    /// @param name Array identifier.
    /// @param loc Source location of the identifier.
    /// @return Array reference expression node.
    ExprPtr parseArrayRef(std::string name, il::support::SourceLoc loc);

    /// @brief Parse either an array or variable reference based on lookahead.
    /// @return Parsed reference expression node.
    ExprPtr parseArrayOrVar();

    /// @brief Return the precedence value for operator token @p k.
    /// @param k Operator token kind.
    /// @return Numeric precedence used by the expression parser.
    int precedence(TokenKind k);
};

} // namespace il::frontends::basic
