add_library(viper_il_io STATIC
        FormatUtils.cpp
        FunctionParser.cpp
        InstrParser.cpp
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

target_link_libraries(viper_il_io PRIVATE viper_il_core)

target_include_directories(viper_il_io PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)

install(FILES ${PROJECT_SOURCE_DIR}/include/viper/il/io/FormatUtils.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/il/io)
