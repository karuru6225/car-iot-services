#pragma once

void        pathReset();
bool        pathIsRoot();
const char *pathGet();
void        pathPush(const char *label);
void        pathPop();
const char *pathTitle();
