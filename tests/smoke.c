#define COBJMACROS
#include <dxgi.h>
#include <stdio.h>
#include <windows.h>
typedef HRESULT(WINAPI *CreateFactory)(REFIID, void **);
static const GUID factory_iid = {
    0x7b7166ec,
    0x21c7,
    0x44ae,
    {0xb2, 0x1a, 0xc9, 0xae, 0x32, 0x1a, 0xe3, 0x69}};
int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: recordfix_smoke.exe DLL_PATH\n");
    return 2;
  }
  fprintf(stderr,"smoke: loading DLL\n");
  HMODULE plugin = LoadLibraryA(argv[1]);
  if (!plugin) {
    fprintf(stderr, "LoadLibrary: %lu\n", GetLastError());
    return 3;
  }
  DWORD(WINAPI * init)(void) =
      (void *)GetProcAddress(plugin, "RecordFixInitialize");
  DWORD status=init?init():ERROR_PROC_NOT_FOUND;
  fprintf(stderr,"smoke: initialize returned %lu\n",status);
  if(status)return 4;
  HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
  CreateFactory create = (void *)GetProcAddress(dxgi, "CreateDXGIFactory");
  IDXGIFactory *f = 0;
  fprintf(stderr,"smoke: creating factory\n");
  HRESULT hr = create(&factory_iid, (void **)&f);
  fprintf(stderr,"smoke: factory HRESULT %08lx\n",hr);
  if (FAILED(hr))
    return 5;
  void *entry = (void *)f->lpVtbl->CreateSwapChain;
  MEMORY_BASIC_INFORMATION mbi;
  DWORD old, ignored;
  if (!VirtualQuery(entry, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
      !VirtualProtect(entry, 1, PAGE_EXECUTE_READWRITE, &old))
    return 6;
  VirtualProtect(entry, 1, old, &ignored);
  fprintf(stderr,"smoke: executable PE entry verified; calling Release\n");
  IDXGIFactory_Release(f);
  fprintf(stderr,"smoke: Release completed\n");
  return 0;
}
