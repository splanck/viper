add_executable(tui_demo tui_demo.cpp)

target_link_libraries(tui_demo PRIVATE tui)

if (VIPER_INSTALL_TUI)
    install(TARGETS tui_demo
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif ()
