# ViperDOS user-space servers
# Microkernel service processes

add_subdirectory(blkd)
add_subdirectory(fsd)
add_subdirectory(netd)
# inputd removed - kernel handles keyboard input via TTY buffer
add_subdirectory(consoled)
add_subdirectory(displayd)
