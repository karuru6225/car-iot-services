#pragma once
#include <functional>
#include <stdint.h>

class Https
{
public:
  // AT+SH* スタックを使った HTTPS GET ストリーミングダウンロード
  // onChunk: データチャンクを受け取るたびに呼ばれる。false を返すと中断
  // 戻り値: HTTP ステータスコード（200=成功、0=接続/パース失敗）
  int get(const char *url,
          std::function<bool(const uint8_t *, size_t)> onChunk);

  // AT+SH* スタックを使った HTTPS ダウンロードでファイル保存
  // 成功時は ファイルサイズ、失敗時は-1 を返す
  int download(const char *url, const char *filename);
};

extern Https https;
