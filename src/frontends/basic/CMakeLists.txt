add_library(fe_basic STATIC
  Token.cpp
  Lexer.cpp
  AST.cpp
  AstPrinter.cpp
  Parser.cpp
  Parser_Stmt.cpp
  Parser_Expr.cpp
  Parser_Token.cpp
  NameMangler.cpp
  TypeSuffix.cpp
  LoweringContext.cpp
  LoweringPipeline.cpp
  BuiltinRegistry.cpp
  Lowerer.cpp
  LowerExpr.cpp
  LowerStmt.cpp
  LowerRuntime.cpp
  LowerScan.cpp
  LowerEmit.cpp
  SemanticAnalyzer.cpp
  SemanticAnalyzer.Procs.cpp
  SemanticAnalyzer.Stmts.cpp
  SemanticAnalyzer.Exprs.cpp
  SemanticAnalyzer.Builtins.cpp
  ScopeTracker.cpp
  ConstFolder.cpp
  Intrinsics.cpp
  DiagnosticEmitter.cpp
  ProcRegistry.cpp
  SemanticDiagnostics.cpp
)

target_link_libraries(fe_basic PUBLIC support il_build il_core il_runtime)
target_include_directories(fe_basic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
