#include "infra__logger.h"
#include <stdarg.h>

Logger logger;

void Logger::init()
{
  Serial.begin(115200);
}

void Logger::print(const char *msg)
{
  Serial.print(msg);
}

void Logger::println(const char *msg)
{
  Serial.println(msg);
}

void Logger::printf(const char *fmt, ...)
{
  char buf[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
}
