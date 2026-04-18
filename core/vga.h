#pragma once
// Sagittarius SGS-1 — VGA Text Mode Driver
// Пишем прямо в 0xB8000 — никаких зависимостей

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

namespace VGA {

static uint16_t* const BUFFER = (uint16_t*)0xB8000;
constexpr int COLS = 80;
constexpr int ROWS = 25;

// Цвета
enum Color : uint8_t {
    BLACK   = 0,
    BLUE    = 1,
    GREEN   = 2,
    CYAN    = 3,
    RED     = 4,
    MAGENTA = 5,
    BROWN   = 6,
    WHITE   = 7,
    YELLOW  = 14,
    BRIGHT_WHITE = 15,
};

// Глобальное состояние курсора
inline int col = 0;
inline int row = 0;

inline uint16_t make_entry(char c, Color fg, Color bg = BLACK) {
    return static_cast<uint16_t>(c) | (static_cast<uint16_t>((bg << 4) | fg) << 8);
}

inline void clear() {
    for (int i = 0; i < COLS * ROWS; i++)
        BUFFER[i] = make_entry(' ', WHITE);
    col = 0; row = 0;
}

inline void scroll() {
    // Сдвигаем все строки вверх на одну
    for (int r = 0; r < ROWS - 1; r++)
        for (int c = 0; c < COLS; c++)
            BUFFER[r * COLS + c] = BUFFER[(r+1) * COLS + c];
    // Очищаем последнюю строку
    for (int c = 0; c < COLS; c++)
        BUFFER[(ROWS-1) * COLS + c] = make_entry(' ', WHITE);
    row = ROWS - 1;
}

inline void putchar(char c, Color color = WHITE) {
    if (c == '\n') {
        col = 0;
        row++;
        if (row >= ROWS) scroll();
        return;
    }
    if (c == '\r') { col = 0; return; }

    BUFFER[row * COLS + col] = make_entry(c, color);
    col++;
    if (col >= COLS) { col = 0; row++; }
    if (row >= ROWS) scroll();
}

inline void print(const char* str, Color color = WHITE) {
    for (int i = 0; str[i]; i++)
        putchar(str[i], color);
}

inline void println(const char* str, Color color = WHITE) {
    print(str, color);
    putchar('\n', color);
}

// Вывод числа (hex)
inline void print_hex(uint64_t val, Color color = WHITE) {
    const char* hex = "0123456789ABCDEF";
    print("0x", color);
    bool leading = true;
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        if (nibble || !leading || i == 0) {
            putchar(hex[nibble], color);
            leading = false;
        }
    }
}

// Вывод числа (decimal)
inline void print_dec(uint64_t val, Color color = WHITE) {
    if (val == 0) { putchar('0', color); return; }
    char buf[20];
    int i = 0;
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) putchar(buf[--i], color);
}

} // namespace VGA