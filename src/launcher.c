#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <wchar.h>
#include <stdlib.h>
#include "config.h"

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
  if (!loaded)
    goto finished;
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
int wmain(int argc, wchar_t **argv) {
  int validate = argc == 3 && !wcscmp(argv[1], L"--validate");
  if ((!validate && argc != 2) || (argc == 3 && !validate)) {
    fwprintf(stderr,
             L"Usage: recordfix_preload.exe [--validate] CONFIG.yaml\n");
    return 2;
  }
  struct launch_config config;
  if (!read_config(argv[validate ? 2 : 1], &config))
    return 3;
  wchar_t *command = build_command(&config);
  if (!command) {
    free_config(&config);
    return 4;
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
      free(command);
      free_config(&config);
      return 5;
    }
  STARTUPINFOW startup = {0};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION child = {0};
  if (!CreateProcessW(config.executable, command, NULL, NULL, FALSE,
                      CREATE_SUSPENDED, NULL, config.directory, &startup,
                      &child)) {
    fwprintf(stderr, L"CreateProcess failed: %lu\n", GetLastError());
    free(command);
    free_config(&config);
    return 6;
  }
  free(command);
  int ok = 1;
  for (unsigned i = 0; i < config.dll_count; i++)
    if (!inject(child.hProcess, child.dwProcessId, &config.dlls[i],
                config.timeout_ms)) {
      fwprintf(stderr, L"Preload failed: %ls (Win32 error %lu)\n",
               config.dlls[i].path, GetLastError());
      ok = 0;
      break;
    }
  if (ok)
    ok = ResumeThread(child.hThread) != (DWORD)-1;
  CloseHandle(child.hThread);
  free_config(&config);
  if (!ok) {
    TerminateProcess(child.hProcess, ERROR_DLL_INIT_FAILED);
    WaitForSingleObject(child.hProcess, 30000);
    CloseHandle(child.hProcess);
    return 7;
  }
  DWORD result = 0;
  WaitForSingleObject(child.hProcess, INFINITE);
  GetExitCodeProcess(child.hProcess, &result);
  CloseHandle(child.hProcess);
  return (int)result;
}
