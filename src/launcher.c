#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdlib.h>
#include "config.h"

/* GUI subsystem: no console allocation, including during Steam launches.
   Automated callers may suppress modal UI while retaining stderr and exit codes. */
static int fatal(int status, const wchar_t *detail, DWORD error) {
  wchar_t message[4096], system[1024] = L"";
  if (error)
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, error, 0, system, 1024, NULL);
  if (error)
    swprintf(message, 4096, L"%ls\n\nWin32 error %lu: %ls", detail, error, system);
  else
    swprintf(message, 4096, L"%ls", detail);
  fwprintf(stderr, L"%ls\n", message);
  wchar_t suppress[2];
  if (!(GetEnvironmentVariableW(L"RECORDFIX_NO_ERROR_DIALOG", suppress, 2) == 1 &&
        suppress[0] == L'1'))
    MessageBoxW(NULL, message, L"Steam RecordFix - Launch failed",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
  return status;
}

static HMODULE remote_module(DWORD pid, const wchar_t *name) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
  if (snap == INVALID_HANDLE_VALUE)
    return NULL;
  MODULEENTRY32W entry = {0};
  entry.dwSize = sizeof(entry);
  HMODULE result = NULL;
  if (Module32FirstW(snap, &entry))
    do {
      if (!_wcsicmp(entry.szModule, name)) {
        result = entry.hModule;
        break;
      }
    } while (Module32NextW(snap, &entry));
  CloseHandle(snap);
  return result;
}
static int call(HANDLE process, LPTHREAD_START_ROUTINE function, void *argument,
                DWORD *result, DWORD timeout_ms) {
  HANDLE thread =
      CreateRemoteThread(process, NULL, 0, function, argument, 0, NULL);
  if (!thread)
    return 0;
  DWORD wait = WaitForSingleObject(thread, timeout_ms);
  int ok = wait == WAIT_OBJECT_0 && GetExitCodeThread(thread, result);
  DWORD error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
  CloseHandle(thread);
  if (!ok)
    SetLastError(error);
  return ok;
}

static int inject(HANDLE process, DWORD pid, const struct preload_dll *d,
                  DWORD timeout) {
  SIZE_T size = (wcslen(d->path) + 1) * sizeof(wchar_t);
  void *remote = VirtualAllocEx(process, NULL, size, MEM_COMMIT | MEM_RESERVE,
                                PAGE_READWRITE);
  if (!remote)
    return 0;
  DWORD result;
  int ok = 0;
  if (!WriteProcessMemory(process, remote, d->path, size, NULL))
    goto finished;
  if (!call(process,
            (LPTHREAD_START_ROUTINE)GetProcAddress(
                GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"),
            remote, &result, timeout))
    goto finished;
  const wchar_t *name = wcsrchr(d->path, L'\\');
  name = name ? name + 1 : d->path;
  HMODULE loaded = remote_module(pid, name);
  if (!loaded) {
    SetLastError(ERROR_MOD_NOT_FOUND);
    goto finished;
  }
  if (d->initialize) {
    HMODULE local = LoadLibraryExW(d->path, NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (!local)
      goto finished;
    FARPROC init = GetProcAddress(local, d->initialize);
    SIZE_T offset = init ? (SIZE_T)init - (SIZE_T)local : 0;
    FreeLibrary(local);
    if (!offset) {
      SetLastError(ERROR_PROC_NOT_FOUND);
      goto finished;
    }
    if (!call(process, (LPTHREAD_START_ROUTINE)((SIZE_T)loaded + offset), NULL,
              &result, timeout))
      goto finished;
    if (result) {
      fprintf(stderr, "Initialization export failed: %s (%lu)\n", d->initialize,
              result);
      SetLastError(ERROR_DLL_INIT_FAILED);
      goto finished;
    }
  }
  ok = 1;
finished:
  /* A timed-out remote thread could still read this allocation. It will be
     reclaimed when the newly created child is terminated on failure. */
  if (ok)
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
  return ok;
}
static int steam_command(struct launch_config *config, int argc,
                         wchar_t **argv) {
  if (argc < 1 || argc - 1 > RF_MAX_ARGS)
    return 0;
  wchar_t path[RF_PATH_CAP];
  DWORD n = GetFullPathNameW(argv[0], RF_PATH_CAP, path, NULL), type;
  if (!n || n >= RF_PATH_CAP || !GetBinaryTypeW(path, &type) ||
      type != SCS_64BIT_BINARY) {
    fwprintf(stderr, L"Steam command must name an existing Windows x64 EXE\n");
    return 0;
  }
  wchar_t self[RF_PATH_CAP];
  if (!GetModuleFileNameW(NULL, self, RF_PATH_CAP) || !_wcsicmp(self, path))
    return 0;
  config->executable = _wcsdup(path);
  for (int i = 1; i < argc; i++) {
    wchar_t *arg = _wcsdup(argv[i]);
    if (!arg)
      return 0;
    config->arguments[config->argument_count++] = arg;
  }
  /* NULL lpCurrentDirectory preserves the directory Steam chose. The default
     NULL environment block also retains Steam's launch context and IDs. */
  return config->executable != NULL;
}
static int launcher_main(int argc, wchar_t **argv) {
  int validate = argc == 3 && !wcscmp(argv[1], L"--validate");
  int steam = argc >= 2 && !wcscmp(argv[1], L"--steam");
  if ((steam && (argc < 5 || wcscmp(argv[3], L"--"))) ||
      (!steam && !validate && argc != 2)) {
    fwprintf(stderr, L"Usage: recordfix_preload.exe [--validate] CONFIG.yaml\n"
                     L"       recordfix_preload.exe --steam CONFIG.yaml -- "
                     L"GAME.exe [args...]\n");
    return fatal(2, L"Invalid command line. Expected:\nrecordfix_preload.exe CONFIG.yaml\nor --steam CONFIG.yaml -- GAME.exe [args...]", 0);
  }
  struct launch_config config;
  if (!(steam ? read_steam_config(argv[2], &config)
              : read_config(argv[validate ? 2 : 1], &config))) {
    wchar_t detail[2048] = L"Invalid configuration";
    MultiByteToWideChar(CP_UTF8, 0, last_config_error(), -1, detail, 2048);
    return fatal(3, detail, 0);
  }
  if (steam && !steam_command(&config, argc - 4, argv + 4)) {
    free_config(&config);
    return fatal(3, L"Steam command must name an existing Windows x64 EXE and must not launch this launcher recursively.", 0);
  }
  wchar_t *command = build_command(&config);
  if (!command) {
    free_config(&config);
    return fatal(4, L"Cannot build game command line.", ERROR_NOT_ENOUGH_MEMORY);
  }
  if (validate) {
    wprintf(L"Configuration valid: %ls (%u preload DLLs)\n", config.executable,
            config.dll_count);
    free(command);
    free_config(&config);
    return 0;
  }
  for (unsigned i = 0; i < config.environment_count; i++)
    if (!SetEnvironmentVariableW(config.environment[i].name,
                                 config.environment[i].value)) {
      DWORD error = GetLastError();
      free(command);
      free_config(&config);
      return fatal(5, L"Cannot set game environment variable.", error);
    }
  STARTUPINFOW startup = {0};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION child = {0};
  if (!CreateProcessW(config.executable, command, NULL, NULL, FALSE,
                      CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, config.directory, &startup,
                      &child)) {
    DWORD error = GetLastError();
    free(command);
    free_config(&config);
    return fatal(6, L"Cannot start game process.", error);
  }
  free(command);
  int ok = 1;
  DWORD failure = 0;
  wchar_t detail[2048] = L"Cannot resume game process.";
  for (unsigned i = 0; i < config.dll_count; i++)
    if (!inject(child.hProcess, child.dwProcessId, &config.dlls[i],
                config.timeout_ms)) {
      failure = GetLastError();
      swprintf(detail, 2048, L"Cannot preload or initialize DLL:\n%ls", config.dlls[i].path);
      ok = 0;
      break;
    }
  if (ok) {
    ok = ResumeThread(child.hThread) != (DWORD)-1;
    if (!ok) failure = GetLastError();
  }
  CloseHandle(child.hThread);
  free_config(&config);
  if (!ok) {
    TerminateProcess(child.hProcess, ERROR_DLL_INIT_FAILED);
    WaitForSingleObject(child.hProcess, 30000);
    CloseHandle(child.hProcess);
    return fatal(7, detail, failure);
  }
  DWORD result = 0;
  WaitForSingleObject(child.hProcess, INFINITE);
  GetExitCodeProcess(child.hProcess, &result);
  CloseHandle(child.hProcess);
  return (int)result;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
                    wchar_t *command, int show) {
  (void)instance; (void)previous; (void)command; (void)show;
  int argc;
  wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv)
    return fatal(2, L"Cannot parse command line.", GetLastError());
  int status = launcher_main(argc, argv);
  LocalFree(argv);
  return status;
}
