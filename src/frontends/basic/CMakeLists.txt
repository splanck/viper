add_library(basic_frontend STATIC
  Token.cpp
  Lexer.cpp
  AST.cpp
  AstPrinter.cpp
  Parser.cpp
)

target_link_libraries(basic_frontend PUBLIC support)

target_include_directories(basic_frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
