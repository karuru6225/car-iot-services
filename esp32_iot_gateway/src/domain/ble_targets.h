#pragma once
#include "../config.h"

class BleTargets
{
public:
  char data[MAX_TARGETS][18];
  int  count = 0;

  void load();
  void save();
  void clear();
  bool isTarget(const char *addr) const;
  void add(const char *addr);
  void remove(const char *addr);
};

extern BleTargets bleTargets;
