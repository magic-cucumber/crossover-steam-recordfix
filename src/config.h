#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define RF_MAX_ARGS 64
#define RF_MAX_DLLS 16
#define RF_MAX_ENV 32
#define RF_PATH_CAP 32768
struct preload_dll {
  wchar_t *path;
  char *initialize;
};
struct environment_entry {
  wchar_t *name, *value;
};
struct launch_config {
  wchar_t *executable, *directory;
  wchar_t *arguments[RF_MAX_ARGS];
  unsigned argument_count;
  struct preload_dll dlls[RF_MAX_DLLS];
  unsigned dll_count;
  struct environment_entry environment[RF_MAX_ENV];
  unsigned environment_count;
  DWORD timeout_ms;
};
int read_config(const wchar_t *path, struct launch_config *config);
void free_config(struct launch_config *config);
wchar_t *build_command(const struct launch_config *config);
