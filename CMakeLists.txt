# Sagittarius SGS-1 — Kernel Build
# Отдельный CMakeLists для ядра — тут другие флаги компилятора

cmake_minimum_required(VERSION 3.20)
project(SagittariusKernel LANGUAGES CXX ASM_NASM)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Флаги для ядра — НЕ как обычная программа ────────────
# -ffreestanding  = нет стандартной библиотеки (нет libc!)
# -fno-exceptions = нет исключений C++ (нет рантайма)
# -fno-rtti       = нет RTTI (нет typeid, dynamic_cast)
# -mno-red-zone   = отключить red zone (нужно для прерываний)
# -m32            = 32-bit (multiboot2 входит в 32-bit режиме)
set(KERNEL_CXX_FLAGS
        -ffreestanding
        -fno-exceptions
        -fno-rtti
        -fno-stack-protector
        -mno-red-zone
        -m32
        -O2
        -Wall
        -Wextra
)

# NASM флаги
set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <FLAGS> -f elf32 -o <OBJECT> <SOURCE>")

# ── Исходники ─────────────────────────────────────────────
set(KERNEL_ASM_SOURCES
        boot/start.asm
)

set(KERNEL_CPP_SOURCES
        core/kernel.cpp
        core/vga.cpp
)

# ── Сборка ────────────────────────────────────────────────
add_executable(sagittarius.elf
        boot/start.asm
        core/kernel.cpp
)

target_include_directories(sagittarius.elf PRIVATE core)
target_compile_options(sagittarius.elf PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:${KERNEL_CXX_FLAGS}>
)

# Линкер скрипт — разложить секции правильно
target_link_options(sagittarius.elf PRIVATE
        -T ${CMAKE_CURRENT_SOURCE_DIR}/kernel/linker.ld
        -nostdlib
        -Wl,-melf_i386
)

# ── ISO образ для QEMU ────────────────────────────────────
# grub.cfg — отдельным файлом, не через echo
file(WRITE ${CMAKE_BINARY_DIR}/grub.cfg
        "set timeout=5
set default=0
menuentry \"Sagittarius SGS-1\" {
    multiboot2 /boot/kernel.elf
    boot
}
")

add_custom_command(TARGET sagittarius.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory iso/boot/grub
        COMMAND ${CMAKE_COMMAND} -E copy sagittarius.elf iso/boot/kernel.elf
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/grub.cfg iso/boot/grub/
        COMMAND /usr/bin/grub2-mkrescue -o sagittarius.iso iso/
        COMMENT "Creating bootable ISO..."
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
message(STATUS "")
message(STATUS "  Sagittarius Kernel")
message(STATUS "  Target: x86 protected mode -> x86_64 long mode")
message(STATUS "  После сборки: qemu-system-x86_64 -cdrom sagittarius.iso")
message(STATUS "")