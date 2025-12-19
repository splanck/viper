set(VIPERLANG_SOURCES
    Lexer.cpp
    Parser.cpp
    Types.cpp
    Sema.cpp
    Lowerer.cpp
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
    fe_common
)
