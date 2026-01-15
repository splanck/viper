# ViperDOS SSH Library

set(LIBSSH_SOURCES
        ssh.c
        ssh_auth.c
        ssh_channel.c
        ssh_crypto.c
        sftp.c
)

add_library(viperssh STATIC ${LIBSSH_SOURCES})

target_compile_options(viperssh PRIVATE
        -ffreestanding
        -nostdinc
        -fno-stack-protector
        -fno-exceptions
        -fno-rtti
        -fno-threadsafe-statics
        -mcpu=cortex-a72
        -Wall
        -Wextra
        -O2
        -Wno-unused-function
)

target_include_directories(viperssh PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../libc/include
)
