set(ZIA_SOURCES
        Lexer.cpp
        ImportResolver.cpp
        Parser_Tokens.cpp
        Parser_Expr.cpp
        Parser_Stmt.cpp
        Parser_Type.cpp
        Parser_Decl.cpp
        Types.cpp
        RuntimeAdapter.cpp
        Sema.cpp
        Sema_Generics.cpp
        Sema_TypeResolution.cpp
        Sema_Runtime.cpp
        Sema_Completion.cpp
        Sema_Decl.cpp
        Sema_Stmt.cpp
        Sema_Expr.cpp
        Sema_Expr_Ops.cpp
        Sema_Expr_Call.cpp
        Sema_Expr_Advanced.cpp
        Lowerer.cpp
        Lowerer_Decl.cpp
        Lowerer_Stmt.cpp
        Lowerer_Expr.cpp
        Lowerer_Expr_Complex.cpp
        Lowerer_Expr_Literals.cpp
        Lowerer_Expr_Binary.cpp
        Lowerer_Expr_Call.cpp
        Lowerer_Expr_Method.cpp
        Lowerer_Expr_Collections.cpp
        Lowerer_Expr_Match.cpp
        Lowerer_Dispatch.cpp
        Lowerer_Emit.cpp
        Compiler.cpp
        ZiaCompletion.cpp
        rt_zia_completion.cpp
)

add_library(fe_zia ${ZIA_SOURCES})

target_include_directories(fe_zia PUBLIC
        ${CMAKE_BINARY_DIR}/generated
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(fe_zia PUBLIC
        viper_support
        viper_il_core
        il_build
        il_runtime
        il_transform
        fe_common
)
add_dependencies(fe_zia generate_runtime_registry)
