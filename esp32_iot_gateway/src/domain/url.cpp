#include "url.h"
#include <cstring>

bool parseUrl(const char *url, char *host, int hostSize, char *path, int pathSize)
{
  const char *p = url;
  if (strncmp(p, "https://", 8) == 0)
    p += 8;
  else if (strncmp(p, "http://", 7) == 0)
    p += 7;
  else
    return false;

  const char *slash = strchr(p, '/');
  if (!slash)
  {
    strncpy(host, p, hostSize - 1);
    host[hostSize - 1] = '\0';
    strncpy(path, "/", pathSize - 1);
    path[pathSize - 1] = '\0';
  }
  else
  {
    int len = (int)(slash - p);
    if (len >= hostSize)
      return false;
    strncpy(host, p, len);
    host[len] = '\0';
    strncpy(path, slash, pathSize - 1);
    path[pathSize - 1] = '\0';
  }
  return true;
}
