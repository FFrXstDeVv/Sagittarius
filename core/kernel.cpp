// Sagittarius SGS-1 — Kernel v2
// Консоль + меню выбора + аварийный режим

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

// ════════════════════════════════════════════════════════
// VGA
// ════════════════════════════════════════════════════════
namespace VGA {
    static volatile uint16_t* const BUF = (volatile uint16_t*)0xB8000;
    static const int COLS = 80;
    static const int ROWS = 25;
    enum Color : uint8_t {
        BLACK=0,BLUE=1,GREEN=2,CYAN=3,RED=4,
        MAGENTA=5,BROWN=6,GRAY=7,YELLOW=14,WHITE=15,
    };
    static int col=0, row=0;

    static void clear() {
        volatile uint16_t* p = BUF;
        volatile uint16_t* e = BUF + COLS*ROWS;
        while(p<e) *p++ = (uint16_t)' '|((uint16_t)((BLACK<<4)|GRAY)<<8);
        col=row=0;
    }
    static void scroll() {
        volatile uint16_t* p = BUF;
        volatile uint16_t* e = BUF + (ROWS-1)*COLS;
        while(p<e){ *p = *(p+COLS); p++; }
        volatile uint16_t* last = BUF + (ROWS-1)*COLS;
        volatile uint16_t* lend = last + COLS;
        while(last<lend) *last++ = (uint16_t)' '|((uint16_t)((BLACK<<4)|GRAY)<<8);
        row=ROWS-1;
    }
    static void putchar(char c, Color fg=GRAY, Color bg=BLACK) {
        if(c=='\n'){col=0;if(++row>=ROWS)scroll();return;}
        if(c=='\r'){col=0;return;}
        if(c=='\b'){if(col>0){col--;BUF[row*COLS+col]=(uint16_t)' '|((uint16_t)((bg<<4)|fg)<<8);}return;}
        BUF[row*COLS+col]=(uint16_t)c|((uint16_t)((bg<<4)|fg)<<8);
        if(++col>=COLS){col=0;if(++row>=ROWS)scroll();}
    }
    static void print(const char* s,Color fg=GRAY,Color bg=BLACK){
        while(*s) putchar(*s++,fg,bg);
    }
    static void println(const char* s,Color fg=GRAY,Color bg=BLACK){
        print(s,fg,bg); putchar('\n',fg,bg);
    }
    static void fill_row(int r, char c, Color fg, Color bg){
        volatile uint16_t* p = BUF + r*COLS;
        volatile uint16_t* e = p + COLS;
        while(p<e) *p++ = (uint16_t)c|((uint16_t)((bg<<4)|fg)<<8);
    }
    static void print_at(int r, int c, const char* s, Color fg, Color bg=BLACK){
        volatile uint16_t* p = BUF + r*COLS + c;
        while(*s && (p < BUF + r*COLS + COLS)) *p++ = (uint16_t)*s++|((uint16_t)((bg<<4)|fg)<<8);
    }
}

// ════════════════════════════════════════════════════════
// Порты
// ════════════════════════════════════════════════════════
static inline uint8_t inb(uint16_t port){uint8_t v;asm volatile("inb %1,%0":"=a"(v):"Nd"(port));return v;}
static inline void outb(uint16_t port,uint8_t v){asm volatile("outb %0,%1"::"a"(v),"Nd"(port));}

// ════════════════════════════════════════════════════════
// Клавиатура
// ════════════════════════════════════════════════════════
static const char KEYMAP[128]={
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};
static char read_key(){
    while(true){if(inb(0x64)&1){uint8_t sc=inb(0x60);if(sc&0x80)continue;if(sc<128&&KEYMAP[sc])return KEYMAP[sc];}}
}

// ════════════════════════════════════════════════════════
// Утилиты
// ════════════════════════════════════════════════════════
static bool streq(const char* a,const char* b){while(*a&&*b)if(*a++!=*b++)return false;return *a==*b;}

static void reboot(){
    while(inb(0x64)&2); outb(0x64,0xFE);
    asm volatile("lidt 0; int $0");
}

// ════════════════════════════════════════════════════════
// Ввод строки
// ════════════════════════════════════════════════════════
static char cmd[256];
static int  cmd_len;

static void read_line(const char* prompt, VGA::Color pc=VGA::CYAN){
    VGA::print(prompt,pc);
    cmd_len=0;
    // Вместо range-for — через указатель
    volatile char* p = cmd;
    volatile char* e = cmd + 256;
    while(p<e) *p++ = 0;

    while(true){
        char c=read_key();
        if(c=='\n'){VGA::putchar('\n');break;}
        if(c=='\b'){if(cmd_len>0){cmd[--cmd_len]=0;VGA::putchar('\b');}continue;}
        if(cmd_len<255){cmd[cmd_len++]=c;VGA::putchar(c,VGA::WHITE);}
    }
}


// ════════════════════════════════════════════════════════
// Аварийный shell
// ════════════════════════════════════════════════════════
static void emergency_shell(){
    VGA::clear();
    VGA::fill_row(0,' ',VGA::WHITE,VGA::RED);
    VGA::print_at(0,2,"SAGITTARIUS EMERGENCY SHELL",VGA::WHITE,VGA::RED);
    VGA::col=0; VGA::row=2;
    VGA::println("Emergency mode. Type 'help' for commands.",VGA::YELLOW);
    VGA::println("",VGA::GRAY);

    while(true){
        read_line("emergency> ",VGA::RED);
        if(!cmd_len) continue;
        if(streq(cmd,"help")){
            VGA::println("  help     - this message",  VGA::GRAY);
            VGA::println("  reboot   - reboot",        VGA::GRAY);
            VGA::println("  halt     - halt",          VGA::GRAY);
            VGA::println("  clear    - clear screen",  VGA::GRAY);
            VGA::println("  info     - system info",   VGA::GRAY);
            VGA::println("  boot     - continue boot", VGA::GRAY);
        }
        else if(streq(cmd,"reboot")) reboot();
        else if(streq(cmd,"halt")){VGA::println("Halted.",VGA::RED);asm volatile("cli;hlt");}
        else if(streq(cmd,"clear")) VGA::clear();
        else if(streq(cmd,"info")){
            VGA::println("  Sagittarius SGS-1 v0.1",VGA::CYAN);
            VGA::println("  Mode: Emergency Shell",   VGA::GRAY);
            VGA::println("  VGA:  0xB8000 80x25",     VGA::GRAY);
        }
        else if(streq(cmd,"boot")) return;
        else{VGA::print("Unknown: ",VGA::RED);VGA::println(cmd,VGA::GRAY);}
    }
}

// ════════════════════════════════════════════════════════
// Основной shell
// ════════════════════════════════════════════════════════
static void main_shell(){
    VGA::clear();
    VGA::fill_row(0,' ',VGA::WHITE,VGA::BLUE);
    VGA::print_at(0,2,"Sagittarius SGS-1",VGA::WHITE,VGA::BLUE);
    VGA::print_at(0,60,"SGS shell v0.1",VGA::CYAN,VGA::BLUE);
    VGA::fill_row(24,' ',VGA::WHITE,VGA::BLUE);
    VGA::print_at(24,2,"Type 'help' for commands",VGA::YELLOW,VGA::BLUE);
    VGA::col=0; VGA::row=2;
    VGA::println("Welcome to Sagittarius SGS-1!",VGA::CYAN);
    VGA::println("",VGA::GRAY);

    while(true){
        read_line("sgs1> ");
        if(!cmd_len) continue;
        if(streq(cmd,"help")){
            VGA::println("  help       - this message",      VGA::GRAY);
            VGA::println("  clear      - clear screen",      VGA::GRAY);
            VGA::println("  reboot     - reboot",            VGA::GRAY);
            VGA::println("  halt       - halt",              VGA::GRAY);
            VGA::println("  emergency  - emergency shell",   VGA::GRAY);
            VGA::println("  version    - version info",      VGA::GRAY);
            VGA::println("  about      - about Sagittarius", VGA::GRAY);
        }
        else if(streq(cmd,"clear")){
            VGA::clear();
            VGA::fill_row(0,' ',VGA::WHITE,VGA::BLUE);
            VGA::print_at(0,2,"Sagittarius SGS-1",VGA::WHITE,VGA::BLUE);
            VGA::col=0; VGA::row=2;
        }
        else if(streq(cmd,"reboot"))    reboot();
        else if(streq(cmd,"halt")){VGA::println("System halted.",VGA::RED);asm volatile("cli;hlt");}
        else if(streq(cmd,"emergency")) emergency_shell();
        else if(streq(cmd,"version")){
            VGA::println("  Sagittarius SGS-1 v0.1",        VGA::CYAN);
            VGA::println("  Kernel: C++26, bare metal x86", VGA::GRAY);
            VGA::println("  FS:     MyFS v0.2 CoW+zstd",    VGA::GRAY);
            VGA::println("  Boot:   Multiboot2 + GRUB2",    VGA::GRAY);
        }
        else if(streq(cmd,"about")){
            VGA::println("  Sagittarius -- independent OS", VGA::CYAN);
            VGA::println("  No Linux. No Windows.",         VGA::GRAY);
            VGA::println("  Built from scratch.",           VGA::GRAY);
        }
        else{
            VGA::print("Unknown: ",VGA::RED);
            VGA::println(cmd,VGA::GRAY);
        }
    }
}

// ════════════════════════════════════════════════════════
// Экран загрузки
// ════════════════════════════════════════════════════════
static void boot_screen(){
    VGA::clear();
    VGA::fill_row(0,' ',VGA::WHITE,VGA::BLUE);
    VGA::print_at(0,2,"Sagittarius SGS-1",VGA::WHITE,VGA::BLUE);
    VGA::print_at(0,60,"v0.1 / 2026",VGA::CYAN,VGA::BLUE);

    VGA::col=0; VGA::row=2;
    VGA::println("",VGA::WHITE);
    VGA::println("    #####    ###    #####   #####  ####  ####  #####  #####  #   #  #####",VGA::CYAN);
    VGA::println("   #       #   #  #       #         #    #       #      #   # # #  #    ",VGA::CYAN);
    VGA::println("    ###    #####  #  ###  #  ###    #    ###     #      #   #   #  #####",VGA::CYAN);
    VGA::println("       #   #   #  #    #  #    #    #    #       #      #   #   #      #",VGA::CYAN);
    VGA::println("   #####   #   #   #####   #####  ####  #       #    #####  #   #  #####",VGA::CYAN);
    VGA::println("",VGA::WHITE);
    VGA::println("                    SGS-1  |  MyFS  |  C++26",VGA::GRAY);
    VGA::println("",VGA::WHITE);

    VGA::println("  [OK] Multiboot2",              VGA::GREEN);
    VGA::println("  [OK] VGA 80x25",               VGA::GREEN);
    VGA::println("  [OK] Keyboard PS/2",           VGA::GREEN);
    VGA::println("  [OK] Kernel at 0x100000",      VGA::GREEN);
    VGA::println("",VGA::WHITE);

    VGA::fill_row(24,' ',VGA::WHITE,VGA::BLUE);
    VGA::print_at(24,2,"ENTER = Boot  |  E = Emergency Shell",VGA::YELLOW,VGA::BLUE);
}

// ════════════════════════════════════════════════════════
// kernel_main
// ════════════════════════════════════════════════════════
extern "C" void kernel_main(uint32_t /*mb_magic*/, uint32_t /*mb_info*/){
    boot_screen();

    while(true){
        if(inb(0x64)&1){
            uint8_t sc=inb(0x60);
            if(sc&0x80) continue;
            if(sc==0x1C){ main_shell(); break; }    // Enter
            if(sc==0x12){ emergency_shell(); main_shell(); break; } // E
        }
    }
    while(true) asm volatile("hlt");
}