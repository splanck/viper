# ViperDOS minimal libc
# Provides basic C runtime functions for user-space programs

set(LIBC_C_SOURCES
        src/crt0.c
        src/string.c
        src/stdlib.c
        src/stdio.c
        src/ctype.c
        src/unistd.c
        src/errno.c
        src/time.c
        src/math.c
        src/dirent.c
        src/termios.c
        src/pthread.c
        src/signal.c
        src/stat.c
        src/inttypes.c
        src/setjmp.c
        src/setjmp.S
        src/wait.c
        src/poll.c
        src/socket.c
        src/netdb.c
        src/locale.c
        src/wchar.c
        src/mman.c
        src/utsname.c
        src/fenv.c
        src/resource.c
        src/syslog.c
        src/fnmatch.c
        src/getopt.c
        src/pwd.c
        src/grp.c
        src/libgen.c
        src/search.c
        src/glob.c
        src/ftw.c
        src/spawn.c
        src/sched.c
        src/wordexp.c
        src/ipc.c
        src/shm.c
        src/monetary.c
        src/iconv.c
        src/nl_types.c
        src/sem.c
        src/msg.c
        src/semaphore.c
        src/langinfo.c
        src/fmtmsg.c
        src/aio.c
        src/mqueue.c
        src/regex.c
        src/ndbm.c
        src/utmpx.c
        src/syscall.S
        src/fsd_stubs.c
)

set(LIBC_CXX_SOURCES
        src/new.cpp
        src/consoled_backend.cpp
)

# Create static library
add_library(viperlibc STATIC ${LIBC_C_SOURCES} ${LIBC_CXX_SOURCES})

target_include_directories(viperlibc PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/c++
        ${CMAKE_SOURCE_DIR}/include
)

# Freestanding compilation - no standard library dependencies
target_compile_options(viperlibc PRIVATE
        -ffreestanding
        -nostdinc
        -fno-builtin
        -nostdlib
        -Wall
        -Wextra
)

# libc is standalone - all I/O goes through kernel syscalls

# Set output directory
set_target_properties(viperlibc PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
