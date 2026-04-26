#pragma once
// ビルド時に埋め込む秘密情報はありません。
// すべてプロビジョニングスクリプトで書き込みます。
//
// NVS (namespace: "device"):
//   mqtt_host = "xxxxx-ats.iot.ap-northeast-1.amazonaws.com"
//
// SPIFFS:
//   /certs/ca.crt     - Amazon Root CA
//   /certs/device.crt - デバイス証明書
//   /certs/device.key - デバイス秘密鍵
