#include <stdio.h>
#include <Windows.h>
#include "../include/hardware.h"
#include "../include/console.h"
/* Initialization: Cyan (1 line)
 * Ager/Trimmer: Magenta (1 line)
 * Modified Write Thread: Orange (1)
 * Virtual Memory Test: Blue (NUM_THREADS lines) (Thread colors should be Yellow)
 * Errors: Red (as many as it needs)
 */

CRITICAL_SECTION console_lock;

void position_cursor(SHORT row, SHORT col) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD pos = {col, row};
    SetConsoleCursorPosition(hConsole, pos);
}

void clear_line(int line) {
    EnterCriticalSection(&console_lock);
    position_cursor((SHORT)line, 0); // Position cursor at the specified line
    DWORD written;
    FillConsoleOutputCharacter(GetStdHandle(STD_OUTPUT_HANDLE), ' ', 80, (COORD){0, (SHORT)line}, &written); // Clear 80 characters
    position_cursor((SHORT)line, 0); // Reset cursor position
    LeaveCriticalSection(&console_lock);
}

void set_initialize_status(const char* func_name, const char* message) {
    clear_line(0); // First line for initialization
    EnterCriticalSection(&console_lock);
    printf(COLOR_CYAN "%s" COLOR_RESET " : %s\n", func_name, message);
    fflush(stdout);
    LeaveCriticalSection(&console_lock);
}

void set_modified_status(const char* message) {
    clear_line(1); // Second line for modified write thread
    EnterCriticalSection(&console_lock);
    printf(COLOR_MAGENTA "modified_write_thread" COLOR_RESET " : %s\n", message);
    fflush(stdout);
    LeaveCriticalSection(&console_lock);
}

void set_trim_status(const char* message) {
    clear_line(2); // Third line for trim thread
    EnterCriticalSection(&console_lock);
    printf(COLOR_ORANGE "trim_thread" COLOR_RESET " : %s\n", message);
    fflush(stdout);
    LeaveCriticalSection(&console_lock);
}

VOID print_fatal_error(const char* msg) {
    DWORD error_code = GetLastError();
    LPVOID error_msg;

    FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            error_code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR) &error_msg,
            0, NULL );

    EnterCriticalSection(&console_lock);
    position_cursor(NUMBER_OF_FAULTING_THREADS + 3, 0); // Compute the start line

    printf(COLOR_RED "fatal error" COLOR_RESET " : %s\n" COLOR_RED "%s" COLOR_RESET "\n", msg, (char*)error_msg);
    fflush(stdout);
    LeaveCriticalSection(&console_lock);

    LocalFree(error_msg);
}

void print_bar(int thread_id, double fraction, ULONG64 passthrough, ULONG64 total_passthroughs, ULONG64 faults) {
    EnterCriticalSection(&console_lock);
    int fill = (int)(fraction * BAR_WIDTH);
    char bar[BAR_WIDTH + 1];
    for (int i = 0; i < BAR_WIDTH; i++) {
        bar[i] = (i < fill) ? '#' : ' ';
    }
    bar[BAR_WIDTH] = '\0';

    position_cursor((SHORT)(thread_id + 3), 0); // Offset by 3 lines

    double percent = fraction * 100.0;

    printf(COLOR_BLUE "full_virtual_memory_test" COLOR_RESET " : " COLOR_YELLOW "Thread %d" COLOR_RESET " [", thread_id);
    for (int i = 0; i < BAR_WIDTH; i++) {
        if (bar[i] == '#') {
            printf(COLOR_GREEN "#" COLOR_RESET);
        } else {
            printf(" ");
        }
    }
    printf("] Pass %llu/%llu Faults: %llu (%.2f%%)", passthrough, total_passthroughs, faults, percent);
    fflush(stdout);
    LeaveCriticalSection(&console_lock);
}