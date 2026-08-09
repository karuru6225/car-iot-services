#include <unity.h>
#include "domain/url.h"

void setUp(void) {}
void tearDown(void) {}

static void test_parses_https_with_path(void)
{
  char host[64], path[64];
  TEST_ASSERT_TRUE(parseUrl("https://example.com/foo/bar", host, sizeof(host), path, sizeof(path)));
  TEST_ASSERT_EQUAL_STRING("example.com", host);
  TEST_ASSERT_EQUAL_STRING("/foo/bar", path);
}

static void test_parses_http_with_path(void)
{
  char host[64], path[64];
  TEST_ASSERT_TRUE(parseUrl("http://example.com/foo", host, sizeof(host), path, sizeof(path)));
  TEST_ASSERT_EQUAL_STRING("example.com", host);
  TEST_ASSERT_EQUAL_STRING("/foo", path);
}

static void test_defaults_path_to_root_when_no_slash(void)
{
  char host[64], path[64];
  TEST_ASSERT_TRUE(parseUrl("https://example.com", host, sizeof(host), path, sizeof(path)));
  TEST_ASSERT_EQUAL_STRING("example.com", host);
  TEST_ASSERT_EQUAL_STRING("/", path);
}

static void test_rejects_unknown_scheme(void)
{
  char host[64], path[64];
  TEST_ASSERT_FALSE(parseUrl("ftp://example.com/foo", host, sizeof(host), path, sizeof(path)));
}

static void test_rejects_host_overflowing_buffer(void)
{
  char host[4], path[64]; // "example.com" (11文字) は hostSize=4 に収まらない
  TEST_ASSERT_FALSE(parseUrl("https://example.com/foo", host, sizeof(host), path, sizeof(path)));
}

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_parses_https_with_path);
  RUN_TEST(test_parses_http_with_path);
  RUN_TEST(test_defaults_path_to_root_when_no_slash);
  RUN_TEST(test_rejects_unknown_scheme);
  RUN_TEST(test_rejects_host_overflowing_buffer);
  return UNITY_END();
}
