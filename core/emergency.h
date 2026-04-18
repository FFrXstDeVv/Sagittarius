#pragma once
// Sagittarius SGS-1 — Emergency Shell
// Работает без ядра, без драйверов, без ФС
// Читает клавиатуру через порты напрямую, выводит через VGA

#include "vga.h"
#include <cstdint>

// Чтение/запись портов — напрямую в железо
inline uint8_t inb(uint16_t port) {
    uint8_t val;
    asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

// Клавиатурные скан-коды → ASCII (упрощённо)
static const char KEYMAP[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' '
};

inline char read_key() {
    while (true) {
        // Ждём пока клавиатура будет готова (бит 0 статусного порта)
        if (inb(0x64) & 1) {
            uint8_t scan = inb(0x60);
            // Игнорируем release (старший бит установлен)
            if (scan & 0x80) continue;
            if (scan < 128 && KEYMAP[scan])
                return KEYMAP[scan];
        }
    }
}

// Буфер команды
static char cmd_buf[256];
static int  cmd_len = 0;

inline void cmd_clear() {
    cmd_len = 0;
    for (auto& c : cmd_buf) c = 0;
}

inline void emergency_help() {
    VGA::println("", VGA::WHITE);
    VGA::println("Emergency Shell Commands:", VGA::YELLOW);
    VGA::println("  help     - this message", VGA::WHITE);
    VGA::println("  reboot   - reboot the system", VGA::WHITE);
    VGA::println("  halt     - halt the system", VGA::WHITE);
    VGA::println("  memtest  - test memory", VGA::WHITE);
    VGA::println("  clear    - clear screen", VGA::WHITE);
    VGA::println("  info     - system info", VGA::WHITE);
    VGA::println("", VGA::WHITE);
}

inline void do_reboot() {
    VGA::println("Rebooting...", VGA::YELLOW);
    // Сброс через PS/2 контроллер клавиатуры
    while (inb(0x64) & 2);
    outb(0x64, 0xFE);
    // Если не сработало — тройной fault
    asm volatile("lidt 0");
    asm volatile("int $0");
}

inline void do_halt() {
    VGA::println("System halted.", VGA::RED);
    asm volatile("cli; hlt");
}

inline void do_info() {
    VGA::println("", VGA::WHITE);
    VGA::print  ("System: ", VGA::WHITE);
    VGA::println("Sagittarius SGS-1", VGA::CYAN);
    VGA::print  ("Mode:   ", VGA::WHITE);
    VGA::println("Emergency Shell (protected mode)", VGA::YELLOW);
    VGA::print  ("VGA:    ", VGA::WHITE);
    VGA::println("0xB8000 text mode 80x25", VGA::WHITE);
    VGA::println("", VGA::WHITE);
}

inline bool streq(const char* a, const char* b) {
    while (*a && *b) if (*a++ != *b++) return false;
    return *a == *b;
}

extern "C" void emergency_main() {
    VGA::println("", VGA::WHITE);
    VGA::println("Something went wrong. You are in Emergency Shell.", VGA::RED);
    VGA::println("Type 'help' for available commands.", VGA::YELLOW);
    VGA::println("", VGA::WHITE);

    while (true) {
        VGA::print("emergency> ", VGA::CYAN);
        cmd_clear();

        // Читаем команду
        while (true) {
            char c = read_key();
            if (c == '\n') break;
            if (c == '\b') {
                if (cmd_len > 0) {
                    cmd_len--;
                    cmd_buf[cmd_len] = 0;
                    // Стереть символ на экране
                    VGA::col--;
                    VGA::putchar(' ', VGA::WHITE);
                    VGA::col--;
                }
                continue;
            }
            if (cmd_len < 255) {
                cmd_buf[cmd_len++] = c;
                VGA::putchar(c, VGA::WHITE);
            }
        }
        VGA::putchar('\n', VGA::WHITE);

        // Выполняем команду
        if (cmd_len == 0) continue;
        else if (streq(cmd_buf, "help"))    emergency_help();
        else if (streq(cmd_buf, "reboot"))  do_reboot();
        else if (streq(cmd_buf, "halt"))    do_halt();
        else if (streq(cmd_buf, "clear"))   VGA::clear();
        else if (streq(cmd_buf, "info"))    do_info();
        else {
            VGA::print("Unknown command: ", VGA::RED);
            VGA::println(cmd_buf, VGA::WHITE);
        }
    }
}