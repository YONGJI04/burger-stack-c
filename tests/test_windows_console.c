/* Native Windows console integration test. Run on Windows 10 1809 or later. */
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000006 /* Windows 10 1809: ConPTY declarations. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HANDLE input_pipe, output_pipe;

/* ConPTY can repaint unrelated cells between two letters of a heading.
 * Track the visible cells instead of searching its raw escape stream. */
#define SCREEN_ROWS 32
#define SCREEN_COLS 96
static char screen[SCREEN_ROWS][SCREEN_COLS + 1];
static int screen_row, screen_col, saved_row, saved_col;
static int parser_state, unicode_left;
static unsigned unicode_value;
static char sequence[64];
static size_t sequence_length;

static void clear_row(int row)
{
    memset(screen[row], ' ', SCREEN_COLS);
    screen[row][SCREEN_COLS] = '\0';
}

static void reset_screen(void)
{
    for (int row = 0; row < SCREEN_ROWS; ++row) clear_row(row);
    screen_row = screen_col = saved_row = saved_col = 0;
    parser_state = unicode_left = 0;
    sequence_length = 0;
}

static void next_row(void)
{
    if (++screen_row < SCREEN_ROWS) return;
    memmove(screen, screen + 1, (SCREEN_ROWS - 1) * sizeof(screen[0]));
    clear_row(SCREEN_ROWS - 1);
    screen_row = SCREEN_ROWS - 1;
}

static int bounded(int value, int maximum)
{
    return value < 0 ? 0 : value > maximum ? maximum : value;
}

static void control_sequence(char command)
{
    if (sequence[0] == '?') return; /* Cursor visibility and alternate screen. */
    int first = 0, second = 0;
    (void)sscanf(sequence, "%d;%d", &first, &second);
    int count = first > 0 ? first : 1;
    switch (command) {
    case 'H': case 'f':
        screen_row = bounded(count - 1, SCREEN_ROWS - 1);
        screen_col = bounded((second > 0 ? second : 1) - 1, SCREEN_COLS - 1);
        break;
    case 'G': screen_col = bounded(count - 1, SCREEN_COLS - 1); break;
    case 'd': screen_row = bounded(count - 1, SCREEN_ROWS - 1); break;
    case 'A': screen_row = bounded(screen_row - count, SCREEN_ROWS - 1); break;
    case 'B': screen_row = bounded(screen_row + count, SCREEN_ROWS - 1); break;
    case 'C': screen_col = bounded(screen_col + count, SCREEN_COLS - 1); break;
    case 'D': screen_col = bounded(screen_col - count, SCREEN_COLS - 1); break;
    case 'J':
        for (int row = 0; row < SCREEN_ROWS; ++row)
            for (int col = 0; col < SCREEN_COLS; ++col)
                if (first == 2 || (first == 0 && (row > screen_row || (row == screen_row && col >= screen_col)))
                    || (first == 1 && (row < screen_row || (row == screen_row && col <= screen_col))))
                    screen[row][col] = ' ';
        break;
    case 'K':
        for (int col = 0; col < SCREEN_COLS; ++col)
            if (first == 2 || (first == 0 && col >= screen_col) || (first == 1 && col <= screen_col))
                screen[screen_row][col] = ' ';
        break;
    case 'X':
        for (int col = screen_col; col < SCREEN_COLS && col - screen_col < count; ++col)
            screen[screen_row][col] = ' ';
        break;
    case 's': saved_row = screen_row; saved_col = screen_col; break;
    case 'u': screen_row = saved_row; screen_col = saved_col; break;
    default: break; /* Colors do not affect the ASCII headings we check. */
    }
}

static void screen_byte(unsigned char byte)
{
    if (parser_state == 3) { /* OSC title, terminated by BEL or ESC-backslash. */
        if (byte == 7) parser_state = 0;
        else if (byte == 27) parser_state = 4;
        return;
    }
    if (parser_state == 4) { parser_state = byte == '\\' ? 0 : 3; return; }
    if (parser_state == 1) {
        parser_state = byte == '[' ? 2 : byte == ']' ? 3 : 0;
        sequence_length = 0;
        return;
    }
    if (parser_state == 2) {
        if (byte >= 0x40 && byte <= 0x7e) {
            sequence[sequence_length] = '\0';
            control_sequence((char)byte);
            parser_state = 0;
        } else if (sequence_length + 1 < sizeof(sequence)) sequence[sequence_length++] = (char)byte;
        return;
    }
    if (byte == 27) { parser_state = 1; unicode_left = 0; return; }
    if (byte == '\r') { screen_col = 0; return; }
    if (byte == '\n') { next_row(); return; }
    if (byte == '\b') { screen_col = bounded(screen_col - 1, SCREEN_COLS - 1); return; }
    if (byte < 32) return;
    unsigned cp = byte;
    if (unicode_left) {
        if ((byte & 0xc0) != 0x80) { unicode_left = 0; return; }
        unicode_value = (unicode_value << 6) | (byte & 0x3f);
        if (--unicode_left) return;
        cp = unicode_value;
    } else if (byte >= 0xc2) {
        unicode_left = byte < 0xe0 ? 1 : byte < 0xf0 ? 2 : 3;
        unicode_value = byte & (unicode_left == 1 ? 0x1f : unicode_left == 2 ? 0x0f : 0x07);
        return;
    } else if (byte >= 0x80) return;
    /* Game headings are ASCII; Hangul occupies two cells, block art one. */
    int cells = cp >= 0xac00 && cp <= 0xd7a3 ? 2 : 1;
    for (int i = 0; i < cells; ++i) {
        if (screen_col == SCREEN_COLS) { screen_col = 0; next_row(); }
        screen[screen_row][screen_col++] = cp < 128 ? (char)cp : '?';
    }
}

static int screen_contains(const char *text)
{
    for (int row = 0; row < SCREEN_ROWS; ++row)
        if (strstr(screen[row], text)) return 1;
    return 0;
}

static void receive(void)
{
    DWORD available = 0, count = 0;
    if (!PeekNamedPipe(output_pipe, NULL, 0, NULL, &available, NULL) || available == 0) return;
    unsigned char chunk[8192];
    if (available > sizeof(chunk)) available = sizeof(chunk);
    if (ReadFile(output_pipe, chunk, available, &count, NULL))
        for (DWORD i = 0; i < count; ++i) screen_byte(chunk[i]);
}

static int expect(const char *text)
{
    ULONGLONG end = GetTickCount64() + 5000;
    while (GetTickCount64() < end) {
        receive();
        if (screen_contains(text)) return 1;
        Sleep(10);
    }
    fprintf(stderr, "Missing output: %s\n", text);
    for (int row = 0; row < SCREEN_ROWS; ++row) fprintf(stderr, "%s\n", screen[row]);
    return 0;
}

static void send_keys(const char *keys)
{
    DWORD written;
    if (!WriteFile(input_pipe, keys, (DWORD)strlen(keys), &written, NULL)) {
        fputs("Could not write console input\n", stderr);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) { fputs("Usage: test_windows_console.exe path-to-burger.exe\n", stderr); return 1; }
    reset_screen();
    /* Reproduce the interleaved heading/border writes observed on Windows CI. */
    const char *fragmented = "\033[2;4HHA\033[32;6H----------\033[m\033[2;6HLL OF FAME";
    for (const char *p = fragmented; *p; ++p) screen_byte((unsigned char)*p);
    if (!screen_contains("HALL OF FAME")) return 1;
    reset_screen();
    HANDLE console_input = NULL, console_output = NULL;
    HPCON console = NULL;
    PROCESS_INFORMATION process = {0};
    STARTUPINFOEXA startup = {0};
    char temporary[MAX_PATH], ranking[MAX_PATH], command[2048];
    int ok = 0;
    if (!GetTempPathA(sizeof(temporary), temporary)
        || !GetTempFileNameA(temporary, "bgr", 0, ranking)) return 1;
    if (!CreatePipe(&console_input, &input_pipe, NULL, 0)
        || !CreatePipe(&output_pipe, &console_output, NULL, 0)) goto cleanup;
    if (FAILED(CreatePseudoConsole((COORD){96, 32}, console_input, console_output, 0, &console))) goto cleanup;
    CloseHandle(console_input); console_input = NULL;
    CloseHandle(console_output); console_output = NULL;
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &bytes);
    startup.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!startup.lpAttributeList || !InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes)) goto cleanup;
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, console,
                                   sizeof(console), NULL, NULL)) goto cleanup;
    startup.StartupInfo.cb = sizeof(startup);
    /* Do not duplicate WSL/CI's redirected standard handles into the child.
     * Null handles with this flag let ConPTY attach its own console handles. */
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    snprintf(command, sizeof(command), "\"%s\" --seed 42 --ranking \"%s\"", argv[1], ranking);
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT,
                        NULL, NULL, &startup.StartupInfo, &process)) goto cleanup;
    if (!expect("H A M B U R G E R")) goto cleanup;
    send_keys("1");
    if (!expect("ORDER QUEUE")) goto cleanup;
    send_keys("1237s");
    if (!expect("ORDER UP!")) goto cleanup;
    send_keys("p");
    if (!expect("PAUSED")) goto cleanup;
    Sleep(100);
    send_keys("p");
    if (!expect("ORDER QUEUE")) goto cleanup;
    send_keys("q");
    if (!expect("FINAL SCORE")) goto cleanup;
    send_keys("김셰프X\b\r");
    if (!expect("HALL OF FAME")) goto cleanup;
    send_keys("\r");
    if (!expect("H A M B U R G E R")) goto cleanup;
    send_keys("q");
    for (int i = 0; i < 500 && WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0; ++i) {
        receive(); Sleep(10);
    }
    DWORD exit_code;
    if (!GetExitCodeProcess(process.hProcess, &exit_code) || exit_code != 0) goto cleanup;
    FILE *file = fopen(ranking, "rb");
    if (!file) goto cleanup;
    char record[256] = {0};
    size_t count = fread(record, 1, sizeof(record) - 1, file);
    fclose(file);
    if (count == 0 || strstr(record, "\t김셰프\n") == NULL) {
        fprintf(stderr, "Korean name was not saved correctly: %s\n", record);
        goto cleanup;
    }
    ok = 1;
cleanup:
    if (!ok) fprintf(stderr, "Windows console test failed (last error=%lu)\n", GetLastError());
    if (process.hProcess) {
        if (WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
    }
    if (startup.lpAttributeList) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, startup.lpAttributeList);
    }
    if (console) ClosePseudoConsole(console);
    if (input_pipe) CloseHandle(input_pipe);
    if (output_pipe) CloseHandle(output_pipe);
    if (console_input) CloseHandle(console_input);
    if (console_output) CloseHandle(console_output);
    DeleteFileA(ranking);
    if (ok) puts("PASS: native Windows console, serving, pause/resume, Korean name, ranking, clean exit");
    return ok ? 0 : 1;
}
