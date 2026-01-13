set(VIPERLANG_SOURCES
        Lexer.cpp
        ImportResolver.cpp
        Parser_Tokens.cpp
        Parser_Expr.cpp
        Parser_Stmt.cpp
        Parser_Type.cpp
        Parser_Decl.cpp
        Types.cpp
        Sema.cpp
        Sema_Runtime.cpp
        Sema_Decl.cpp
        Sema_Stmt.cpp
        Sema_Expr.cpp
        Lowerer.cpp
        Lowerer_Decl.cpp
        Lowerer_Stmt.cpp
        Lowerer_Expr.cpp
        Lowerer_Expr_Literals.cpp
        Lowerer_Expr_Binary.cpp
        Lowerer_Expr_Call.cpp
        Lowerer_Expr_Collections.cpp
        Lowerer_Expr_Match.cpp
        Lowerer_Dispatch.cpp
        Lowerer_Emit.cpp
        Compiler.cpp
)

add_library(fe_viperlang ${VIPERLANG_SOURCES})

target_include_directories(fe_viperlang PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(fe_viperlang PUBLIC
        viper_support
        viper_il_core
        il_build
        il_runtime
        il_transform
        fe_common
)
