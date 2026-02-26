add_library(viper_il_io STATIC
        FunctionParser.cpp
        FunctionParser_Prototype.cpp
        FunctionParser_Body.cpp
        FormatUtils.cpp
        InstrParser.cpp
        OperandParse_Label.cpp
        OperandParse_Const.cpp
        OperandParse_Type.cpp
        OperandParse_Value.cpp
        OperandParse_ValueDetail.cpp
        OperandParser.cpp
        ModuleParser.cpp
        Parser.cpp
        ParserState.cpp
        ParserUtil.cpp
        StringEscape.cpp
        Serializer.cpp
        TypeParser.cpp
        ../../parse/Cursor.cpp
)

target_link_libraries(viper_il_io PRIVATE viper_il_core viper_common_opts)

target_include_directories(viper_il_io PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)
