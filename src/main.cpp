#include "engine.h"

// Enable colors in window console
#ifdef _WIN32
#include <windows.h>
void enableVTMode() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
}
#endif

int main(int argc, char* argv[]) try {
    enableVTMode();
    cpt::engine().start();
    return 0;
} catch (const char* &error) {
    printf(error);
}