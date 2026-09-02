#pragma once

// "https://host/path" 形式のURLを host/path に分解する。
// スキームが https:// / http:// 以外、または host が hostSize に収まらない場合は false。
// スラッシュなし（"https://host"）の場合、path には "/" を格納する。
bool parseUrl(const char *url, char *host, int hostSize, char *path, int pathSize);
