#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <dxgi.h>
static void line(FILE *f, const wchar_t *s) {
  int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
  char *b = malloc(n);
  WideCharToMultiByte(CP_UTF8, 0, s, -1, b, n, NULL, NULL);
  fwrite(b, 1, n - 1, f);
  fputc('\n', f);
  free(b);
}
int wmain(int argc, wchar_t **argv) {
  if (argc == 3 && !wcscmp(argv[1], L"--check-error-dialog")) {
    /* Exercise real modal UI, dismiss it, and verify the fatal exit code. */
    SetEnvironmentVariableW(L"RECORDFIX_NO_ERROR_DIALOG", NULL);
    wchar_t command[32768];
    swprintf(command, 32768, L"\"%ls\"", argv[2]);
    STARTUPINFOW startup = {0};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child = {0};
    if (!CreateProcessW(argv[2], command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &startup, &child))
      return 80;
    int found = 0;
    for (int i = 0; i < 300; i++) {
      HWND dialog = FindWindowW(L"#32770", L"Steam RecordFix - Launch failed");
      DWORD owner = 0;
      if (dialog) GetWindowThreadProcessId(dialog, &owner);
      if (owner == child.dwProcessId && IsWindowVisible(dialog) &&
          IsWindowEnabled(dialog)) {
        found = 1;
        PostMessageW(dialog, WM_CLOSE, 0, 0);
        break;
      }
      if (WaitForSingleObject(child.hProcess, 100) == WAIT_OBJECT_0) break;
    }
    DWORD status = 0;
    if (!found || WaitForSingleObject(child.hProcess, 10000) != WAIT_OBJECT_0) {
      TerminateProcess(child.hProcess, 81);
      WaitForSingleObject(child.hProcess, 10000);
    } else {
      GetExitCodeProcess(child.hProcess, &status);
    }
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return found && status == 2 ? 0 : 81;
  }
  if (argc == 2 && !wcscmp(argv[1], L"--probe-cwd")) {
    wchar_t cwd[32768];
    GetCurrentDirectoryW(32768, cwd);
    line(stdout, cwd);
    return 0;
  }
  if (GetConsoleWindow())
    return 77; /* launcher must not allocate a child console */
  HMODULE plugin = GetModuleHandleW(L"steam_recordfix.dll");
  if (!plugin)
    return 70;
  DWORD(WINAPI * ready)(void) =
      (void *)GetProcAddress(plugin, "RecordFixIsInitialized");
  if (!ready || !ready())
    return 76;
  HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
  if (!dxgi)
    return 71;
  typedef HRESULT(WINAPI * F)(REFIID, void **);
  F create = (void *)GetProcAddress(dxgi, "CreateDXGIFactory");
  GUID iid = {0x7b7166ec,
              0x21c7,
              0x44ae,
              {0xb2, 0x1a, 0xc9, 0xae, 0x32, 0x1a, 0xe3, 0x69}};
  void *factory = NULL;
  if (FAILED(create(&iid, &factory)))
    return 72;
  void **v = *(void ***)factory;
  HMODULE owner = NULL;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)v[10], &owner) ||
      owner != dxgi)
    return 73;
  typedef ULONG(WINAPI * R)(void *);
  ((R)v[2])(factory);
  wchar_t out[32768], env[1024], cwd[32768];
  if (!GetEnvironmentVariableW(L"RF_TEST_OUTPUT", out, 32768))
    return 74;
  GetEnvironmentVariableW(L"RF_TEST_VALUE", env, 1024);
  GetCurrentDirectoryW(32768, cwd);
  FILE *f = _wfopen(out, L"wb");
  if (!f)
    return 75;
  line(f, env);
  line(f, cwd);
  for (int i = 1; i < argc; i++)
    line(f, argv[i]);
  fclose(f);
  return 0;
}
