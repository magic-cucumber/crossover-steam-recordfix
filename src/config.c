#include "config.h"
#include "yaml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int error(const char *s) {
  fprintf(stderr, "Configuration: %s\n", s);
  return 0;
}
static int scalar(yaml_node_t *n) {
  return n && n->type == YAML_SCALAR_NODE &&
         !memchr(n->data.scalar.value, 0, n->data.scalar.length);
}
static int key(yaml_node_t *n, const char *s) {
  return scalar(n) && !strcmp((char *)n->data.scalar.value, s);
}
static wchar_t *wide(yaml_node_t *n) {
  if (!scalar(n))
    return NULL;
  int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                 (char *)n->data.scalar.value, -1, NULL, 0);
  if (!size)
    return NULL;
  wchar_t *s = calloc((size_t)size, sizeof(wchar_t));
  if (s && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                (char *)n->data.scalar.value, -1, s, size)) {
    free(s);
    return NULL;
  }
  return s;
}
static wchar_t *resolve(const wchar_t *base, const wchar_t *path) {
  if (!path || !*path)
    return NULL;
  size_t n = wcslen(base) + wcslen(path) + 2;
  if (n >= RF_PATH_CAP)
    return NULL;
  wchar_t *joined = calloc(n, sizeof(wchar_t)),
          *out = calloc(RF_PATH_CAP, sizeof(wchar_t));
  if (!joined || !out) {
    free(joined);
    free(out);
    return NULL;
  }
  if (path[0] == L'\\' || path[0] == L'/' || (path[0] && path[1] == L':'))
    wcscpy(joined, path);
  else {
    wcscpy(joined, base);
    wcscat(joined, L"\\");
    wcscat(joined, path);
  }
  DWORD count = GetFullPathNameW(joined, RF_PATH_CAP, out, NULL);
  free(joined);
  if (!count || count >= RF_PATH_CAP) {
    free(out);
    return NULL;
  }
  return out;
}
static wchar_t *path_node(yaml_document_t *d, int index, const wchar_t *base) {
  wchar_t *s = wide(yaml_document_get_node(d, index));
  if (!s)
    return NULL;
  wchar_t *out = resolve(base, s);
  free(s);
  return out;
}
static int dlls(yaml_document_t *d, yaml_node_t *n, const wchar_t *base,
                struct launch_config *c) {
  if (!n || n->type != YAML_SEQUENCE_NODE)
    return error("preload must be a sequence");
  for (yaml_node_item_t *i = n->data.sequence.items.start;
       i < n->data.sequence.items.top; i++) {
    if (c->dll_count == RF_MAX_DLLS)
      return error("too many preload DLLs");
    struct preload_dll *p = &c->dlls[c->dll_count++];
    yaml_node_t *item = yaml_document_get_node(d, *i);
    if (item->type != YAML_MAPPING_NODE)
      return error(
          "each preload entry must contain path and optional initialize");
    for (yaml_node_pair_t *q = item->data.mapping.pairs.start;
         q < item->data.mapping.pairs.top; q++) {
      yaml_node_t *k = yaml_document_get_node(d, q->key),
                  *v = yaml_document_get_node(d, q->value);
      if (key(k, "path")) {
        if (p->path)
          return error("duplicate preload path");
        p->path = path_node(d, q->value, base);
        if (!p->path)
          return error("invalid DLL path");
      } else if (key(k, "initialize")) {
        if (p->initialize || !scalar(v))
          return error("invalid initialize export");
        p->initialize = _strdup((char *)v->data.scalar.value);
        if (!p->initialize || !*p->initialize)
          return error("empty initialize export");
      } else
        return error("unknown preload field");
    }
    if (!p->path || GetFileAttributesW(p->path) == INVALID_FILE_ATTRIBUTES)
      return error("preload DLL not found");
  }
  return 1;
}
int read_config(const wchar_t *path, struct launch_config *c) {
  memset(c, 0, sizeof(*c));
  c->timeout_ms = 30000;
  wchar_t base[RF_PATH_CAP];
  DWORD size = GetFullPathNameW(path, RF_PATH_CAP, base, NULL);
  if (!size || size >= RF_PATH_CAP)
    return error("invalid config path");
  FILE *f = _wfopen(base, L"rb");
  if (!f)
    return error("cannot open YAML file");
  wchar_t *slash = wcsrchr(base, L'\\');
  if (!slash) {
    fclose(f);
    return error("config directory missing");
  }
  *slash = 0;
  yaml_parser_t parser;
  yaml_document_t doc;
  if (!yaml_parser_initialize(&parser)) {
    fclose(f);
    return error("cannot allocate YAML parser");
  }
  yaml_parser_set_input_file(&parser, f);
  if (!yaml_parser_load(&parser, &doc)) {
    fprintf(stderr, "YAML line %zu: %s\n", parser.problem_mark.line + 1,
            parser.problem ? parser.problem : "parse error");
    yaml_parser_delete(&parser);
    fclose(f);
    return 0;
  }
  int ok = 0;
  yaml_node_t *root = yaml_document_get_root_node(&doc);
  unsigned seen = 0;
  if (!root || root->type != YAML_MAPPING_NODE) {
    error("root must be a mapping");
    goto finished;
  }
  for (yaml_node_pair_t *p = root->data.mapping.pairs.start;
       p < root->data.mapping.pairs.top; p++) {
    yaml_node_t *k = yaml_document_get_node(&doc, p->key),
                *v = yaml_document_get_node(&doc, p->value);
    unsigned bit = 0;
    const char *names[] = {"executable", "arguments",   "working_directory",
                           "preload",    "environment", "injection_timeout_ms"};
    for (unsigned i = 0; i < 6; i++)
      if (key(k, names[i]))
        bit = 1u << i;
    if (!bit || (seen & bit)) {
      error("unknown or duplicate root field");
      goto finished;
    }
    seen |= bit;
    if (bit == 1) {
      c->executable = path_node(&doc, p->value, base);
      if (!c->executable) {
        error("invalid executable path");
        goto finished;
      }
    } else if (bit == 2) {
      if (v->type != YAML_SEQUENCE_NODE) {
        error("arguments must be a sequence of strings");
        goto finished;
      }
      for (yaml_node_item_t *i = v->data.sequence.items.start;
           i < v->data.sequence.items.top; i++) {
        if (c->argument_count == RF_MAX_ARGS) {
          error("too many arguments");
          goto finished;
        }
        wchar_t *s = wide(yaml_document_get_node(&doc, *i));
        if (!s) {
          error("invalid argument");
          goto finished;
        }
        c->arguments[c->argument_count++] = s;
      }
    } else if (bit == 4) {
      c->directory = path_node(&doc, p->value, base);
      if (!c->directory) {
        error("invalid working_directory");
        goto finished;
      }
    } else if (bit == 8) {
      if (!dlls(&doc, v, base, c))
        goto finished;
    } else if (bit == 16) {
      if (v->type != YAML_MAPPING_NODE) {
        error("environment must be a mapping");
        goto finished;
      }
      for (yaml_node_pair_t *q = v->data.mapping.pairs.start;
           q < v->data.mapping.pairs.top; q++) {
        if (c->environment_count == RF_MAX_ENV) {
          error("too many environment entries");
          goto finished;
        }
        struct environment_entry *e = &c->environment[c->environment_count++];
        e->name = wide(yaml_document_get_node(&doc, q->key));
        e->value = wide(yaml_document_get_node(&doc, q->value));
        if (!e->name || !e->value || !*e->name || wcschr(e->name, L'=')) {
          error("invalid environment entry");
          goto finished;
        }
        for (unsigned j = 0; j + 1 < c->environment_count; j++)
          if (!_wcsicmp(c->environment[j].name, e->name)) {
            error("duplicate environment variable");
            goto finished;
          }
      }
    } else {
      if (!scalar(v)) {
        error("invalid injection timeout");
        goto finished;
      }
      char *end;
      unsigned long n = strtoul((char *)v->data.scalar.value, &end, 10);
      if (*end || n < 1000 || n > 300000) {
        error("timeout must be 1000..300000 milliseconds");
        goto finished;
      }
      c->timeout_ms = (DWORD)n;
    }
  }
  if (!c->executable) {
    error("executable is required");
    goto finished;
  }
  DWORD type;
  if (!GetBinaryTypeW(c->executable, &type) || type != SCS_64BIT_BINARY) {
    error("executable must be a Windows x64 program");
    goto finished;
  }
  if (!c->directory) {
    c->directory = _wcsdup(c->executable);
    if (!c->directory)
      goto finished;
    wchar_t *s = wcsrchr(c->directory, L'\\');
    if (s) {
      if (s == c->directory + 2 && c->directory[1] == L':')
        s[1] = 0;
      else
        *s = 0;
    }
  }
  DWORD attrs = GetFileAttributesW(c->directory);
  if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    error("working directory does not exist");
    goto finished;
  }
  /* Reject a second YAML document rather than silently ignoring it. */
  yaml_document_t extra;
  if (!yaml_parser_load(&parser, &extra)) {
    error("invalid trailing YAML document");
    goto finished;
  }
  int extra_root = yaml_document_get_root_node(&extra) != NULL;
  yaml_document_delete(&extra);
  if (extra_root) {
    error("only one YAML document is supported");
    goto finished;
  }
  ok = 1;
finished:
  yaml_document_delete(&doc);
  yaml_parser_delete(&parser);
  fclose(f);
  if (!ok)
    free_config(c);
  return ok;
}
void free_config(struct launch_config *c) {
  free(c->executable);
  free(c->directory);
  for (unsigned i = 0; i < c->argument_count; i++)
    free(c->arguments[i]);
  for (unsigned i = 0; i < c->dll_count; i++) {
    free(c->dlls[i].path);
    free(c->dlls[i].initialize);
  }
  for (unsigned i = 0; i < c->environment_count; i++) {
    free(c->environment[i].name);
    free(c->environment[i].value);
  }
  memset(c, 0, sizeof(*c));
}
/* CommandLineToArgvW / Microsoft CRT quoting: double trailing slashes and
   escape backslashes preceding a quote. No shell interpretation is involved. */
wchar_t *build_command(const struct launch_config *c) {
  size_t cap = wcslen(c->executable) * 2 + 4;
  for (unsigned i = 0; i < c->argument_count; i++)
    cap += wcslen(c->arguments[i]) * 2 + 4;
  if (cap > 32767)
    return NULL;
  wchar_t *out = calloc(cap, sizeof(wchar_t));
  if (!out)
    return NULL;
  wchar_t *p = out;
  for (unsigned a = 0; a <= c->argument_count; a++) {
    const wchar_t *s = a ? c->arguments[a - 1] : c->executable;
    if (a)
      *p++ = L' ';
    *p++ = L'"';
    while (*s) {
      size_t slashes = 0;
      while (*s == L'\\') {
        slashes++;
        s++;
      }
      if (!*s) {
        for (size_t n = 0; n < slashes * 2; n++)
          *p++ = L'\\';
        break;
      }
      if (*s == L'"') {
        for (size_t n = 0; n < slashes * 2 + 1; n++)
          *p++ = L'\\';
        *p++ = *s++;
      } else {
        for (size_t n = 0; n < slashes; n++)
          *p++ = L'\\';
        *p++ = *s++;
      }
    }
    *p++ = L'"';
  }
  *p = 0;
  return out;
}
