add_library(il_io STATIC
  FunctionParser.cpp
  InstrParser.cpp
  ModuleParser.cpp
  Parser.cpp
  ParserState.cpp
  ParserUtil.cpp
  Serializer.cpp
  TypeParser.cpp
)

target_link_libraries(il_io PUBLIC il_core)

target_include_directories(il_io PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)
