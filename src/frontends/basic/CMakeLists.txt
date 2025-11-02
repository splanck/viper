set(BASIC_DIAG_SPEC ${CMAKE_SOURCE_DIR}/scripts/spec/diagnostics.yaml)
set(BASIC_DIAG_HEADER ${CMAKE_BINARY_DIR}/generated/include/viper/diag/BasicDiag.hpp)
set(BASIC_DIAG_SOURCE ${CMAKE_BINARY_DIR}/generated/src/frontends/basic/diag/BasicDiag.cpp)

add_custom_command(
        OUTPUT ${BASIC_DIAG_HEADER} ${BASIC_DIAG_SOURCE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated/include/viper/diag
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated/src/frontends/basic/diag
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/gen_diagnostics.py
                --spec ${BASIC_DIAG_SPEC}
                --header ${BASIC_DIAG_HEADER}
                --source ${BASIC_DIAG_SOURCE}
        DEPENDS ${BASIC_DIAG_SPEC} ${CMAKE_SOURCE_DIR}/scripts/gen_diagnostics.py
        COMMENT "Generating BASIC diagnostics catalog"
        VERBATIM)

add_custom_target(basic_diagnostics_catalog
        DEPENDS ${BASIC_DIAG_HEADER} ${BASIC_DIAG_SOURCE})

add_library(fe_basic STATIC)

target_sources(fe_basic PRIVATE
        Token.cpp
        Lexer.cpp
        AST.cpp
        AstPrinter.cpp
        AstPrint_Expr.cpp
        AstPrint_Stmt.cpp
        print/Print_Stmt_Control.cpp
        print/Print_Stmt_Jump.cpp
        print/Print_Stmt_Decl.cpp
        print/Print_Stmt_IO.cpp
        AstWalkerUtils.cpp
        Parser.cpp
        Parser_Stmt.cpp
        Parser_Stmt_Core.cpp
        Parser_Stmt_Control.cpp
        Parser_Stmt_If.cpp
        Parser_Stmt_Select.cpp
        Parser_Stmt_Loop.cpp
        Parser_Stmt_Jump.cpp
        Parser_Stmt_IO.cpp
        Parser_Stmt_OOP.cpp
        Parser_Stmt_Runtime.cpp
        Parser_Expr.cpp
        Parser_Token.cpp
        StatementSequencer.cpp
        NameMangler.cpp
        NameMangler_OOP.cpp
        TypeSuffix.cpp
        TypeRules.cpp
        LoweringContext.cpp
        Lowerer.Program.cpp
        Lowerer.Procedure.cpp
        Lowerer.Statement.cpp
        BuiltinRegistry.cpp
        builtins/MathBuiltins.cpp
        builtins/StringBuiltins.cpp
        Lowerer.cpp
        EmitCommon.cpp
        SelectCaseLowering.cpp
        SelectModel.cpp
        LowerExprLogical.cpp
        LowerExprNumeric.cpp
        LowerExprBuiltin.cpp
        LowerExpr.cpp
        LowerStmt_Control.cpp
        LowerStmt_IO.cpp
        LowerStmt_Runtime.cpp
        LowerRuntime.cpp
        LowerScan.cpp
        lower/BuiltinCommon.cpp
        lower/builtins/Common.cpp
        lower/builtins/Math.cpp
        lower/builtins/String.cpp
        lower/builtins/Array.cpp
        lower/builtins/IO.cpp
        lower/common/CommonLowering.cpp
        lower/common/BuiltinUtils.cpp
        lower/Lowerer_Errors.cpp
        lower/Lowerer_Expr.cpp
        lower/Lowerer_Stmt.cpp
        lower/Scan_ExprTypes.cpp
        lower/Scan_RuntimeNeeds.cpp
        Lower_OOP_Scan.cpp
        LowerEmit.cpp
        lower/Emit_Control.cpp
        lower/Lower_If.cpp
        lower/Lower_Loops.cpp
        lower/Lower_Switch.cpp
        lower/Lower_TryCatch.cpp
        lower/Emitter.cpp
        lower/Emit_Expr.cpp
        lower/Emit_Builtin.cpp
        lower/Emit_OOP.cpp
        Lower_OOP_Emit.cpp
        SemanticAnalyzer.cpp
        SemanticAnalyzer.Procs.cpp
        SemanticAnalyzer.Stmts.cpp
        SemanticAnalyzer.Stmts.Shared.cpp
        SemanticAnalyzer.Stmts.Control.cpp
        sem/Check_If.cpp
        sem/Check_Loops.cpp
        sem/Check_Jumps.cpp
        sem/Check_Select.cpp
        sem/Check_Expr_Unary.cpp
        sem/Check_Expr_Binary.cpp
        sem/Check_Expr_Call.cpp
        SemanticAnalyzer.Stmts.IO.cpp
        SemanticAnalyzer.Stmts.Runtime.cpp
        Semantic_OOP.cpp
        SemanticAnalyzer.Exprs.cpp
        SemanticAnalyzer.Builtins.cpp
        ScopeTracker.cpp
        constfold/Dispatch.cpp
        constfold/FoldArith.cpp
        constfold/FoldLogical.cpp
        constfold/FoldCompare.cpp
        constfold/FoldStrings.cpp
        constfold/FoldCasts.cpp
        ConstFolder.cpp
        Intrinsics.cpp
        DiagnosticEmitter.cpp
        BasicCompiler.cpp
        Lower_OOP_Expr.cpp
        Lower_OOP_Stmt.cpp
        ProcRegistry.cpp
        SemanticDiagnostics.cpp
)

set_source_files_properties(${BASIC_DIAG_SOURCE} PROPERTIES GENERATED TRUE)
target_sources(fe_basic PRIVATE ${BASIC_DIAG_SOURCE})
add_dependencies(fe_basic basic_diagnostics_catalog)

target_link_libraries(fe_basic PUBLIC viper_il_full il_build il_runtime)
target_include_directories(fe_basic PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)
