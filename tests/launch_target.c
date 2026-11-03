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
