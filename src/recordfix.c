#include "MinHook.h"
#include "support.h"
void *targets[256];
extern void *thunks[256];
struct Rec {
  void *obj;
  void **original, **wrapped;
  unsigned count, kind;
};
static struct Rec records[128];
static unsigned nr, nt;
static HANDLE logfile;
static void *factory_entries[2], *factory_originals[2];
static long lock;
long _InterlockedCompareExchange(long volatile *, long, long);
long _InterlockedExchange(long volatile *, long);
static void enter(void) {
  while (_InterlockedCompareExchange(&lock, 1, 0)) {
  }
}
static void leave(void) { _InterlockedExchange(&lock, 0); }
static void rf_log(const char *s) {
  if (logfile)
    put(logfile, s);
}
static struct Rec *rec(void *o) {
  for (unsigned i = 0; i < nr; i++)
    if (records[i].obj == o && *(void ***)o == records[i].wrapped)
      return &records[i];
  return 0;
}
static void *thunk(void *p) {
  for (unsigned i = 0; i < nt; i++)
    if (targets[i] == p)
      return thunks[i];
  if (nt == 256)
    return p;
  targets[nt] = p;
  return thunks[nt++];
}
static void wrap(void *, unsigned);
/* D3DMetal 4.0b1 aborts on Steam's NV12 render-target allocation.
   Returning a normal allocation error selects Steam's supported RGB/CPU path.
 */
static HRESULT capture_resource(void *o, void *heap, unsigned flags, void *desc,
                                unsigned state, void *clear,
                                const struct GUID *i, void **out) {
  struct Rec *r = rec(o);
  SIZE_T overlay = (SIZE_T)GetModuleHandleA("gameoverlayrenderer64.dll");
  SIZE_T caller = (SIZE_T)RF_RETURN_ADDRESS();
  if (overlay && caller >= overlay && caller < overlay + 0x200000 &&
      *(unsigned *)desc == 3 && *(unsigned *)((char *)desc + 32) == 103 &&
      (*(unsigned *)((char *)desc + 48) & 1)) {
    if (out)
      *out = 0;
    rf_log("NV12 render target unsupported; selecting Steam RGB capture "
        "fallback\r\n");
    return 0x80070057;
  }
  typedef HRESULT (*F)(void *, void *, unsigned, void *, unsigned, void *,
                       const struct GUID *, void **);
  return ((F)r->original[27])(o, heap, flags, desc, state, clear, i, out);
}
static HRESULT query(void *o, const struct GUID *i, void **out) {
  struct Rec *r = rec(o);
  if (!r)
    return 0x80004005;
  typedef HRESULT (*F)(void *, const struct GUID *, void **);
  HRESULT hr = ((F)r->original[0])(o, i, out);
  return hr;
}
static void prepare_queue(void *o) {
  if (!o)
    return;
  struct GUID qi = {0x0ec870a6,
                    0x5d7e,
                    0x4c22,
                    {0x8c, 0xfc, 0x5b, 0xaa, 0xe0, 0x76, 0x16, 0xed}};
  struct GUID di = {0x189819f1,
                    0x1db6,
                    0x4b57,
                    {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};
  typedef HRESULT (*Q)(void *, const struct GUID *, void **);
  typedef unsigned (*R)(void *);
  void *q = 0, *d = 0;
  void **v = *(void ***)o;
  HRESULT hr = ((Q)v[0])(o, &qi, &q);
  if (hr < 0 || !q)
    return;
  v = *(void ***)q;
  hr = ((Q)v[7])(q, &di, &d);
  hex(logfile, "Queue.GetDevice.HRESULT=", (unsigned)hr);
  if (hr >= 0 && d) {
    wrap(d, 3);
    ((R)(*(void ***)d)[2])(d);
  }
  wrap(q, 4);
  ((R)(*(void ***)q)[2])(q);
}
static HRESULT commandqueue(void *o, void *desc, const struct GUID *i,
                            void **out) {
  struct Rec *r = rec(o);
  typedef HRESULT (*F)(void *, void *, const struct GUID *, void **);
  HRESULT h = ((F)r->original[8])(o, desc, i, out);
  if (h >= 0 && out && *out)
    wrap(*out, 4);
  return h;
}
static HRESULT create(void *o, void *d, void *desc, void **out) {
  prepare_queue(d);
  struct Rec *r = rec(o);
  typedef HRESULT (*F)(void *, void *, void *, void **);
  HRESULT h = ((F)r->original[10])(o, d, desc, out);
  if (h >= 0 && out && *out)
    wrap(*out, 2);
  rf_log("CreateSwapChain returned\r\n");
  return h;
}
static HRESULT hwnd(void *o, void *d, void *w, void *desc, void *full,
                    void *restrictedOutput, void **out) {
  prepare_queue(d);
  struct Rec *r = rec(o);
  typedef HRESULT (*F)(void *, void *, void *, void *, void *, void *, void **);
  HRESULT h = ((F)r->original[15])(o, d, w, desc, full, restrictedOutput, out);
  if (h >= 0 && out && *out)
    wrap(*out, 2);
  rf_log("CreateSwapChainForHwnd returned\r\n");
  return h;
}
static HRESULT core(void *o, void *d, void *w, void *desc,
                    void *restrictedOutput, void **out) {
  prepare_queue(d);
  struct Rec *r = rec(o);
  typedef HRESULT (*F)(void *, void *, void *, void *, void *, void **);
  HRESULT h = ((F)r->original[16])(o, d, w, desc, restrictedOutput, out);
  if (h >= 0 && out && *out)
    wrap(*out, 2);
  return h;
}
static HRESULT composition(void *o, void *d, void *desc, void *restrictedOutput,
                           void **out) {
  prepare_queue(d);
  struct Rec *r = rec(o);
  typedef HRESULT (*F)(void *, void *, void *, void *, void **);
  HRESULT h = ((F)r->original[24])(o, d, desc, restrictedOutput, out);
  if (h >= 0 && out && *out)
    wrap(*out, 2);
  return h;
}
static void wrap(void *o, unsigned kind) {
  if (!o)
    return;
  enter();
  if (rec(o) || nr == 128) {
    leave();
    return;
  }
  unsigned count = kind == 1 ? 32 : kind == 2 ? 41 : kind == 3 ? 83 : 19;
  struct Rec *r = &records[nr++];
  r->obj = o;
  void **raw = *(void ***)o;
  r->original = HeapAlloc(GetProcessHeap(), 0, sizeof(void *) * count);
  if (!r->original) {
    nr--;
    leave();
    return;
  }
  copy(r->original, raw, sizeof(void *) * count);
  if (kind == 1) {
    for (unsigned i = 0; i < 2; i++) {
      unsigned slot = i ? 15 : 10;
      if (r->original[slot] == factory_entries[i])
        r->original[slot] = factory_originals[i];
    }
  }
  r->count = count;
  r->kind = kind;
  hex(logfile, "original.method10=", (SIZE_T)r->original[10]);
  r->wrapped = HeapAlloc(GetProcessHeap(), 0, 8 * (count + 2));
  if (!r->wrapped) {
    HeapFree(GetProcessHeap(), 0, r->original);
    nr--;
    leave();
    return;
  }
  r->wrapped += 2;
  r->wrapped[-2] = raw[-2];
  r->wrapped[-1] = raw[-1];
  for (unsigned i = 0; i < count; i++)
    r->wrapped[i] = thunk(r->original[i]);
  r->wrapped[0] = query;
  if (kind == 1) {
    r->wrapped[10] = factory_entries[0];
    r->wrapped[15] = factory_entries[1];
    r->wrapped[16] = core;
    r->wrapped[24] = composition;
  }
  if (kind == 3) {
    r->wrapped[8] = commandqueue;
    r->wrapped[27] = capture_resource;
  }
  *(void ***)o = r->wrapped;
  leave();
  rf_log(kind == 1   ? "Wrapped factory\r\n"
      : kind == 2 ? "Wrapped swapchain\r\n"
      : kind == 3 ? "Wrapped D3D12 device\r\n"
                  : "Wrapped D3D12 queue\r\n");
}

/* MinHook operates on PE exports, never on D3DMetal Mach-O instructions. */
typedef HRESULT(WINAPI *FactoryFn)(const struct GUID *, void **);
typedef HRESULT(WINAPI *Factory2Fn)(unsigned, const struct GUID *, void **);
static FactoryFn original_factory, original_factory1;
static Factory2Fn original_factory2;
static HRESULT WINAPI hook_factory(const struct GUID *i, void **o) {
  HRESULT h = original_factory(i, o);
  if (h >= 0 && o)
    wrap(*o, 1);
  return h;
}
static HRESULT WINAPI hook_factory1(const struct GUID *i, void **o) {
  HRESULT h = original_factory1(i, o);
  if (h >= 0 && o)
    wrap(*o, 1);
  return h;
}
static HRESULT WINAPI hook_factory2(unsigned f, const struct GUID *i,
                                    void **o) {
  HRESULT h = original_factory2(f, i, o);
  if (h >= 0 && o)
    wrap(*o, 1);
  return h;
}
static HMODULE self;
static volatile LONG initialization;
static DWORD initialization_result;
__declspec(dllexport) DWORD WINAPI RecordFixInitialize(void) {
  LONG state = InterlockedCompareExchange(&initialization, 1, 0);
  if (state) {
    while (initialization == 1)
      Sleep(1);
    return initialization_result;
  }
  wchar_t path[MAX_PATH];
  DWORD size = GetModuleFileNameW(self, path, MAX_PATH);
  if (size && size < MAX_PATH - 5) {
    copy(path + size, L".log", 5 * sizeof(wchar_t));
    logfile =
        CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (logfile == INVALID_HANDLE_VALUE)
      logfile = 0;
  }
  rf_log("recordfix MinHook initialization\r\n");
  HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
  if (!dxgi)
    dxgi = LoadLibraryW(L"dxgi.dll");
  void *entries[5];
  void *hooks[3] = {(void *)hook_factory, (void *)hook_factory1,
                    (void *)hook_factory2};
  void **originals[3] = {(void **)&original_factory,
                         (void **)&original_factory1,
                         (void **)&original_factory2};
  const char *names[3] = {"CreateDXGIFactory", "CreateDXGIFactory1",
                          "CreateDXGIFactory2"};
  MH_STATUS status = MH_Initialize();
  unsigned made = 0;
  if (!dxgi || (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED))
    goto failed;
  for (unsigned i = 0; i < 3; i++) {
    entries[i] = (void *)GetProcAddress(dxgi, names[i]);
    status = MH_CreateHook(entries[i], hooks[i], originals[i]);
    if (status != MH_OK)
      goto failed;
    made++;
  }
  /* Steam requires factory creation entries to belong to dxgi.dll. Reuse
     its existing Windows PE entry points and detour them through MinHook;
     preserve each per-object native method behind our adapter. */
  struct GUID factory2_iid = {0x50c83a1c,
                              0xe072,
                              0x4c48,
                              {0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0}};
  void *probe = NULL;
  HRESULT probe_result = original_factory2(0, &factory2_iid, &probe);
  if (probe_result < 0 || !probe) {
    status = MH_ERROR_UNSUPPORTED_FUNCTION;
    goto failed;
  }
  void **probe_vtable = *(void ***)probe;
  factory_entries[0] = probe_vtable[10];
  factory_entries[1] = probe_vtable[15];
  typedef unsigned(WINAPI * ReleaseFn)(void *);
  ((ReleaseFn)probe_vtable[2])(probe);
  for (unsigned i = 0; i < 2; i++) {
    HMODULE owner = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)factory_entries[i], &owner) ||
        owner != dxgi) {
      status = MH_ERROR_UNSUPPORTED_FUNCTION;
      goto failed;
    }
    entries[3 + i] = factory_entries[i];
    status = MH_CreateHook(entries[3 + i], i ? (void *)hwnd : (void *)create,
                           &factory_originals[i]);
    if (status != MH_OK)
      goto failed;
    made++;
  }
  for (unsigned i = 0; i < 5; i++) {
    status = MH_EnableHook(entries[i]);
    if (status != MH_OK)
      goto failed;
  }
  /* Object adapters may outlive the caller's LoadLibrary reference. */
  HMODULE pinned;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_PIN,
                     (LPCWSTR)RecordFixInitialize, &pinned);
  rf_log("MinHook DXGI export hooks enabled\r\n");
  initialization_result = 0;
  InterlockedExchange(&initialization, 2);
  return 0;
failed:
  for (unsigned i = 0; i < made; i++) {
    MH_DisableHook(entries[i]);
    MH_RemoveHook(entries[i]);
  }
  hex(logfile, "MinHook initialization failed=", status);
  initialization_result = ERROR_DLL_INIT_FAILED;
  InterlockedExchange(&initialization, 3);
  return initialization_result;
}
__declspec(dllexport) DWORD WINAPI RecordFixIsInitialized(void) {
  return initialization == 2;
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
    self = module;
    DisableThreadLibraryCalls(module);
  }
  /* Initialization is explicit and runs outside the loader lock, before
     the launcher's suspended game thread is resumed. Never hot-unload. */
  return TRUE;
}
