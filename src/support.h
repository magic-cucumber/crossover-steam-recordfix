#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct GUID {
  unsigned a;
  unsigned short b, c;
  unsigned char d[8];
};
static void copy(void *o, const void *i, SIZE_T n) {
  char *a = o;
  const char *b = i;
  for (SIZE_T k = 0; k < n; k++)
    a[k] = b[k];
}
static unsigned len(const char *s) {
  unsigned n = 0;
  while (s[n])
    n++;
  return n;
}
static void put(HANDLE h, const char *s) {
  DWORD n;
  WriteFile(h, s, len(s), &n, 0);
}
static void hex(HANDLE h, const char *s, SIZE_T v) {
  char b[19] = "0x0000000000000000";
  put(h, s);
  for (int i = 0; i < 16; i++)
    b[17 - i] = "0123456789abcdef"[(v >> (4 * i)) & 15];
  put(h, b);
  put(h, "\r\n");
}

#if defined(_MSC_VER)
#include <intrin.h>
#define RF_RETURN_ADDRESS() _ReturnAddress()
#else
#define RF_RETURN_ADDRESS() __builtin_return_address(0)
#endif
