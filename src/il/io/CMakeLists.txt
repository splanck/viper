add_library(viper_il_io STATIC
  FunctionParser.cpp
  InstrParser.cpp
  OperandParser.cpp
  ModuleParser.cpp
  Parser.cpp
  ParserState.cpp
  ParserUtil.cpp
  Serializer.cpp
  TypeParser.cpp
)

target_link_libraries(viper_il_io PRIVATE viper_il_core)

target_include_directories(viper_il_io PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)
