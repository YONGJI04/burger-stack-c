/* Native Windows console integration test. Run on Windows 10 1809 or later. */
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000006 /* Windows 10 1809: ConPTY declarations. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HANDLE input_pipe, output_pipe;
static char output[262144];
static size_t output_length;

static void receive(void)
{
    DWORD available = 0, count = 0;
    if (!PeekNamedPipe(output_pipe, NULL, 0, NULL, &available, NULL) || available == 0) return;
    if (output_length > sizeof(output) / 2) {
        memmove(output, output + output_length / 2, output_length - output_length / 2);
        output_length -= output_length / 2;
    }
    DWORD room = (DWORD)(sizeof(output) - output_length - 1);
    if (available > room) available = room;
    if (ReadFile(output_pipe, output + output_length, available, &count, NULL)) {
        output_length += count;
        output[output_length] = '\0';
    }
}

static int expect(const char *text)
{
    ULONGLONG end = GetTickCount64() + 5000;
    while (GetTickCount64() < end) {
        receive();
        if (strstr(output, text) != NULL) return 1;
        Sleep(10);
    }
    fprintf(stderr, "Missing output: %s\n%s\n", text, output);
    return 0;
}

static void send_keys(const char *keys)
{
    DWORD written;
    output_length = 0;
    output[0] = '\0';
    if (!WriteFile(input_pipe, keys, (DWORD)strlen(keys), &written, NULL)) {
        fputs("Could not write console input\n", stderr);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) { fputs("Usage: test_windows_console.exe path-to-burger.exe\n", stderr); return 1; }
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
