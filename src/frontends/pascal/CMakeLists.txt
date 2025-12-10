# SPDX-License-Identifier: GPL-3.0-only
# Viper Pascal frontend library

add_library(fe_pascal STATIC)

target_sources(fe_pascal PRIVATE
        AST.cpp
        BuiltinRegistry.cpp
        Lexer.cpp
        Parser.cpp
        Parser_Decl.cpp
        Parser_Expr.cpp
        Parser_OOP.cpp
        Parser_Stmt.cpp
        Parser_Type.cpp
        Parser_Unit.cpp
        SemanticAnalyzer.cpp
        SemanticAnalyzer_Body.cpp
        SemanticAnalyzer_Class.cpp
        SemanticAnalyzer_Decl.cpp
        SemanticAnalyzer_Expr.cpp
        SemanticAnalyzer_Stmt.cpp
        SemanticAnalyzer_Type.cpp
        SemanticAnalyzer_Util.cpp
        Lowerer.cpp
        Lowerer_Decl.cpp
        Lowerer_Emit.cpp
        Lowerer_Expr.cpp
        Lowerer_OOP.cpp
        Lowerer_Stmt.cpp
        Compiler.cpp
)

target_link_libraries(fe_pascal PUBLIC viper_il_full il_build il_runtime fe_common)

target_include_directories(fe_pascal PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)

target_compile_features(fe_pascal PUBLIC cxx_std_17)
