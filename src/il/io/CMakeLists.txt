add_library(il_io STATIC
  Parser.cpp
  ParserUtil.cpp
  Serializer.cpp
  TypeParser.cpp
)

target_link_libraries(il_io PUBLIC il_core)

target_include_directories(il_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
