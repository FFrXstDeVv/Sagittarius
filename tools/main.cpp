// Sagittarius SGS-1 -- MyFS shell
#include "myfs.h"
#include <iostream>
#include <sstream>

int main(int argc, char* argv[]) {
    std::string imgPath = "myfs.img";
    bool doFormat = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--format") doFormat = true;
        else imgPath = argv[i];
    }

    MyFS fs;
    if (!fs.open(imgPath, doFormat)) {
        std::cout << "Запусти: ./myfs myfs.img --format\n";
        return 1;
    }

    if (doFormat) {
        fs.createDir("/home");
        fs.createDir("/home/user");
        fs.createDir("/home/user/docs");
        fs.createDir("/tmp");
        fs.createFile("/home/user/readme.txt",
            "Sagittarius SGS-1\nMyFS v0.2 -- CoW + zstd + FUSE\n");
        // Тест сжатия: повторяющиеся данные хорошо сжимаются
        fs.createFile("/home/user/compressme.txt",
            std::string(4096, 'A') + std::string(4096, 'B'));
    }

    fs.info();
    fs.printMap();

    std::cout << "Команды: ls [path] | cat <file> | write <file> <текст>\n"
              << "         mkdir <dir> | rm <path> | compact | map | info | exit\n\n";

    std::string line;
    while (true) {
        std::cout << "sgs1> ";
        if (!std::getline(std::cin, line) || line == "exit") break;
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string cmd; iss >> cmd;

        if      (cmd == "ls")      { std::string p="/"; iss>>p; fs.ls(p); }
        else if (cmd == "cat")     { std::string p; iss>>p; auto c=fs.readFile(p); if(!c.empty()) std::cout<<c<<"\n"; }
        else if (cmd == "mkdir")   { std::string p; iss>>p; fs.createDir(p); }
        else if (cmd == "rm")      { std::string p; iss>>p; fs.remove(p); }
        else if (cmd == "compact") { fs.compact(); fs.printMap(); }
        else if (cmd == "map")     { fs.printMap(); }
        else if (cmd == "info")    { fs.info(); }
        else if (cmd == "write")   {
            std::string p, content;
            iss >> p; std::getline(iss, content);
            if (!content.empty() && content[0]==' ') content=content.substr(1);
            fs.writeFile(p, content);
        }
        else std::cout << "Неизвестная команда\n";
    }

    fs.close();
    return 0;
}