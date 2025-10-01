// File: src/frontends/basic/Parser.hpp
// Purpose: Declares BASIC parser producing Program with separate procedure and
//          main statement lists.
// Key invariants: Maintains token lookahead buffer.
// Ownership/Lifetime: Parser owns lexer and token buffer.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lexer.hpp"
#include <array>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
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
    /// @brief Helper that manages statement parsing loops and shared token patterns.
    class StatementContext
    {
      public:
        /// @brief Aggregated information about a terminating keyword.
        struct TerminatorInfo
        {
            int line = 0; ///< Optional line number that preceded the terminator.
            il::support::SourceLoc
                loc{}; ///< Source location where the terminator keyword appeared.
        };

        /// @brief Classification of the last separator consumed by the context.
        enum class SeparatorKind
        {
            None,      ///< No separator consumed since the previous statement.
            Colon,     ///< A colon separated the previous and next statements.
            LineBreak, ///< A line break separated the previous and next statements.
        };

        using TerminatorPredicate =
            std::function<bool(int)>; ///< Predicate identifying terminator tokens.
        using TerminatorConsumer = std::function<void(
            int, TerminatorInfo &)>; ///< Callback consuming the terminator tokens.

        /// @brief Construct a context bound to @p parser.
        /// @param parser Owning parser providing token accessors.
        explicit StatementContext(Parser &parser);

        /// @brief Consume a single leading colon or end-of-line if present.
        void skipLeadingSeparator();

        /// @brief Consume all consecutive end-of-line tokens.
        /// @return True when at least one line break was consumed.
        bool skipLineBreaks();

        /// @brief Consume a colon or end-of-line token if immediately present.
        void skipStatementSeparator();

        /// @brief Invoke @p fn with an optional numeric line label.
        /// @param fn Callback receiving the parsed line number (zero when absent).
        void withOptionalLineNumber(const std::function<void(int)> &fn);

        /// @brief Remember a line label that should seed the next statement.
        /// @param line Parsed line number to reuse on the next call.
        void stashPendingLine(int line);

        /// @brief Report which separator most recently separated statements.
        /// @return Classification of the most recent separator.
        SeparatorKind lastSeparator() const;

        /// @brief Parse statements until @p isTerminator matches and populate @p dst.
        /// @param isTerminator Predicate determining when the body ends.
        /// @param onTerminator Callback consuming the terminator once detected.
        /// @param dst Destination vector receiving parsed statements.
        /// @return Line/location metadata of the terminating keyword.
        TerminatorInfo consumeStatementBody(const TerminatorPredicate &isTerminator,
                                            const TerminatorConsumer &onTerminator,
                                            std::vector<StmtPtr> &dst);

        /// @brief Parse statements until @p terminator is encountered.
        /// @param terminator Token that terminates the body.
        /// @param dst Destination vector receiving parsed statements.
        /// @return Line/location metadata of the terminator keyword.
        TerminatorInfo consumeStatementBody(TokenKind terminator, std::vector<StmtPtr> &dst);

      private:
        Parser &parser_;       ///< Parent parser that owns the token stream.
        int pendingLine_ = -1; ///< Deferred line label for the next statement.
        SeparatorKind lastSeparator_ =
            SeparatorKind::LineBreak; ///< Classification of the last separator consumed.
    };

    /// @brief Create a statement context bound to this parser instance.
    /// @return StatementContext referencing the parser's token stream.
    StatementContext statementContext();

    /// @brief Parse the next logical statement line into a single AST node.
    /// @param ctx Statement context that manages separators and line labels.
    /// @return Either a single statement or a StmtList when multiple statements share a line.
    StmtPtr parseStatementLine(StatementContext &ctx);

    /// @brief Consume optional line labels that follow a line break.
    /// @param ctx Statement context providing newline skipping helpers.
    /// @param followerKinds When non-empty, only consume the label when the
    ///        subsequent token is one of the specified kinds.
    void skipOptionalLineLabelAfterBreak(StatementContext &ctx,
                                         std::initializer_list<TokenKind> followerKinds = {});

    /// @brief Parse the body of a single IF-related branch.
    /// @param line Line number propagated to the branch statement.
    /// @param ctx Statement context for separator management.
    /// @return Parsed statement belonging to the branch body.
    StmtPtr parseIfBranchBody(int line, StatementContext &ctx);

    mutable Lexer lexer_;                    ///< Provides tokens from the source buffer.
    mutable std::vector<Token> tokens_;      ///< Lookahead token buffer.
    DiagnosticEmitter *emitter_ = nullptr;   ///< Diagnostic sink; not owned.
    std::unordered_set<std::string> arrays_; ///< Names of arrays declared via DIM.

    /// @brief Mapping entry for statement parsers.
    struct StmtHandler
    {
        StmtPtr (Parser::*no_arg)() = nullptr;       ///< Handler without line parameter.
        StmtPtr (Parser::*with_line)(int) = nullptr; ///< Handler requiring line number.
    };

    std::array<StmtHandler, static_cast<std::size_t>(TokenKind::Count)>
        stmtHandlers_{}; ///< Token to parser mapping.

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
    StmtPtr parsePrint();

    /// @brief Parse a LET assignment statement.
    /// @return LET statement node.
    StmtPtr parseLet();

    /// @brief Parse an IF statement starting at @p line.
    /// @param line Line number of the IF keyword.
    /// @return IF statement node.
    StmtPtr parseIf(int line);

    /// @brief Parse a WHILE loop.
    /// @return WHILE statement node.
    StmtPtr parseWhile();

    /// @brief Parse a DO ... LOOP statement.
    /// @return DO statement node with optional tests.
    StmtPtr parseDo();

    /// @brief Parse a FOR loop.
    /// @return FOR statement node.
    StmtPtr parseFor();

    /// @brief Parse a NEXT statement closing a loop.
    /// @return NEXT statement node.
    StmtPtr parseNext();

    /// @brief Parse an EXIT statement identifying the loop kind.
    /// @return EXIT statement node.
    StmtPtr parseExit();

    /// @brief Parse a GOTO statement.
    /// @return GOTO statement node.
    StmtPtr parseGoto();

    /// @brief Parse an OPEN statement configuring file I/O.
    /// @return OPEN statement node.
    StmtPtr parseOpen();

    /// @brief Parse a CLOSE statement releasing a channel.
    /// @return CLOSE statement node.
    StmtPtr parseClose();

    /// @brief Parse an ON ERROR GOTO statement.
    /// @return ON ERROR statement node.
    StmtPtr parseOnErrorGoto();

    /// @brief Parse an END statement.
    /// @return END statement node.
    StmtPtr parseEnd();

    /// @brief Parse an INPUT statement.
    /// @return INPUT statement node.
    StmtPtr parseInput();

    /// @brief Parse a LINE INPUT # statement.
    /// @return LINE INPUT statement node.
    StmtPtr parseLineInput();

    /// @brief Parse a RESUME statement.
    /// @return RESUME statement node.
    StmtPtr parseResume();

    /// @brief Parse a DIM statement defining arrays.
    /// @return DIM statement node.
    StmtPtr parseDim();

    /// @brief Parse a REDIM statement resizing arrays.
    /// @return REDIM statement node.
    StmtPtr parseReDim();

    /// @brief Parse a RANDOMIZE statement.
    /// @return RANDOMIZE statement node.
    StmtPtr parseRandomize();

    /// @brief Parse a FUNCTION definition including body.
    /// @return FUNCTION statement node.
    StmtPtr parseFunction();

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

    /// @brief Parse a SUB definition including body.
    /// @return SUB statement node.
    StmtPtr parseSub();

    /// @brief Parse a RETURN statement.
    /// @return RETURN statement node.
    StmtPtr parseReturn();

    /// @brief Parse a comma-separated parameter list inside parentheses.
    /// @return Vector of parsed parameters.
    std::vector<Param> parseParamList();

    /// @brief Determine BASIC type from identifier suffix.
    /// @param name Identifier possibly carrying a type suffix.
    /// @return Resolved type.
    Type typeFromSuffix(std::string_view name);

    /// @brief Parse a BASIC type keyword following AS.
    /// @return Resolved BASIC type, defaults to I64 on mismatch.
    Type parseTypeKeyword();

    /// @brief Parse an expression using precedence climbing.
    /// @param min_prec Minimum precedence to enforce.
    /// @return Parsed expression node.
    ExprPtr parseExpression(int min_prec = 0);

    /// @brief Parse a unary expression.
    /// @return Parsed unary expression node.
    ExprPtr parseUnaryExpression();

    /// @brief Parse the right-hand side of an infix expression.
    /// @param left Already parsed left-hand side.
    /// @param min_prec Minimum precedence to enforce.
    /// @return Combined expression node.
    ExprPtr parseInfixRhs(ExprPtr left, int min_prec);

    /// @brief Parse a primary expression such as literals or parenthesized forms.
    /// @return Parsed primary expression node.
    ExprPtr parsePrimary();

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
