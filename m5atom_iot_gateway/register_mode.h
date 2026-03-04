#pragma once
#include "config.h"

struct RegEntry
{
  char address[18];
  bool registered;
  bool cancel;
};

class RegisterMode
{
public:
  void run();
  bool isScanning() const;
  void foundDevice(const char *addr);

private:
  char          _found[MAX_FOUND][18];
  volatile int  _foundCount = 0;
  volatile bool _scanning   = false;
  RegEntry      _list[MAX_TARGETS + MAX_FOUND + 1];
  int           _listCount  = 0;
  int           _selectIdx  = 0;

  void buildList();
};

extern RegisterMode regMode;
