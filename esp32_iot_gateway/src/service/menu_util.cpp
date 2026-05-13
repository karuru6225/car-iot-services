#include "menu_util.h"
#include <cstring>
#include <cstdio>

static char s_path[64] = "/";

void pathReset()
{
  strcpy(s_path, "/");
}

bool pathIsRoot()
{
  return strcmp(s_path, "/") == 0;
}

const char *pathGet()
{
  return s_path;
}

void pathPush(const char *label)
{
  if (pathIsRoot())
    snprintf(s_path, sizeof(s_path), "/%s", label);
  else {
    size_t len = strlen(s_path);
    snprintf(s_path + len, sizeof(s_path) - len, "/%s", label);
  }
}

void pathPop()
{
  char *last = strrchr(s_path, '/');
  if (last == nullptr || last == s_path)
    strcpy(s_path, "/");
  else {
    *last = '\0';
    if (s_path[0] == '\0') strcpy(s_path, "/");
  }
}

const char *pathTitle()
{
  if (pathIsRoot()) return "Menu";
  const char *last = strrchr(s_path, '/');
  return last ? last + 1 : s_path;
}
