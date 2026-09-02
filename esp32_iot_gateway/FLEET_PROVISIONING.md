# Fleet Provisioning 移行検討（キッティング省略）

## 概要

現行の `ops/provision_device.ps1` は「PCに1台ずつUSB接続 → AWS CLIで証明書発行 → provisioning用ファームを書き込み」という手作業。台数が増えた場合の量産キッティング工数を削減する手段として、AWS IoT **Fleet Provisioning** への移行を検討中。Fleet Provisioningには **by Claim Certificate** と **by Trusted User** の2方式があり、両方を評価中。

**現時点では未実装・設計検討のみ**。実装するかどうかは未決定。

---

## 検討の経緯

### 却下した方式: MACアドレスからのクライアント側鍵導出

デバイスのMACアドレスから証明書（鍵）を決定的に導出し、サーバー側は事前登録済みMACから証明書をIoT Coreに登録する、という方式を最初に検討したが、以下の理由でセキュリティ的に不可:

- ESP32のMACはOUI（ベンダー識別子）部分が固定で、実質のエントロピーは24bit程度しかない
- WiFi/BLEのプローブ要求・アドバタイズで平文のまま常時ブロードキャストされている
- 現行のMQTT deviceId自体が `esp32-gw-<mac>` の形式で、MACはすでに公開情報になっている（[provision_device.ps1](../ops/provision_device.ps1)参照）
- 「秘密鍵の元になる値」が最初から誰でも見える状態にあるため、MACを拾われるだけで秘密鍵を再現でき、なりすましが可能になる

### 検討中の方式1: Fleet Provisioning by Claim

- 全台に**同一の** claim証明書＋「フリートプロビジョニングAPIのみ許可」の極小権限ポリシーを焼き込む（量産ファームウェアが1種類になる）
- 起動時にデバイス側で**ローカルに本物の鍵ペアを生成**しCSRを作成、MQTT経由でAWSに証明書発行・Thing登録を依頼する
- サーバー側はpre-provisioning hook（Lambda）でMACアドレスを事前登録リストと照合し、未登録なら拒否するゲート役に回る
- 真の秘密鍵は一切デバイス外に出ない（claim証明書は全台共有だが権限が極小なので、漏洩時の被害が「なりすまし登録の試行」程度に限定される）
- claim証明書は**長期間有効**（AWS公式ドキュメントの表現では「provisioning claim private keys should be secured at all times, including on the device」＝出荷後もデバイス上に残る前提）。漏洩時はclaim証明書自体を無効化するしかなく、無効化すると**それ以降の新規プロビジョニングが全台止まる**（ただし既にプロビジョニング済みのデバイスは無効化の影響を受けない）

### 検討中の方式2: Fleet Provisioning by Trusted User

AWS公式ドキュメント（[Provisioning devices that don't have device certificates](https://docs.aws.amazon.com/iot/latest/developerguide/provision-wo-cert.html)）に基づく、by Claimとは別の方式。想定ユースケースは「エンドユーザーやインストーラーがモバイルアプリで初期設定する」ケースだが、キッティング作業者自身を「trusted user」と見なせば流用できる。

**事前準備**（サーバー側、1回限り）:

1. Provisioning Templateを作成
2. `iot:CreateProvisioningClaim` のみを許可するIAMロール（trusted user用）を作成
3. `AWSIoTThingsRegistration` を付与したprovisioning role（by Claim方式と共通）を用意

**初期化フロー**（キッティング時、デバイスごと）:

1. trusted user（キッティング作業者）が自分のPC/アプリでサインインし、そのIAMロールで `CreateProvisioningClaim` を呼ぶ → **5分間だけ有効な一時claim証明書**を取得
2. その一時証明書をデバイスに渡す（Wi-Fi設定情報などと一緒に、シリアル経由で書き込む想定）
3. デバイスが一時claim証明書でAWS IoTに接続
4. **5分以内に** デバイスが`CreateCertificateFromCsr`（またはCreateKeysAndCertificate）→ `RegisterThing` を呼び、永続証明書を取得してThingを作成
5. デバイスは永続証明書で再接続。以降は通常運用

一時claim証明書は`CreateProvisioningClaim`が返す一過性のものであり、**アカウントの証明書一覧には現れない**（by Claim方式の共有claim証明書とは違い、恒久的に存在する秘密ではない）。

### by Claim と by Trusted User の比較

| 観点 | by Claim | by Trusted User |
| --- | --- | --- |
| claim証明書の性質 | 全台共有・長期間有効・ファームウェアに焼き込み | 1台ごとに`CreateProvisioningClaim`で発行・5分で失効 |
| キッティング時にPC接続が必要か | **不要**（同一ファームを書くだけ。初回LTE接続時に自動プロビジョニング可能） | **必要**（trusted userが都度claimを取得しデバイスに渡す＝現行フローとほぼ同じ手間） |
| 漏洩時の被害範囲 | 共有claim証明書が漏洩すると、無効化するまで悪用され続けるリスク。無効化は全台の新規プロビジョニングを止める | 漏洩しても5分で自動失効。被害範囲が1台・1回に限定される |
| AWS側の追加実装 | Provisioning Template, claim証明書, claim用ポリシー, pre-provisioning hook | Provisioning Template, trusted user用IAMロール, pre-provisioning hook |
| デバイス側の追加実装 | CSR生成 + `CreateCertificateFromCsr`/`CreateKeysAndCertificate` + `RegisterThing` のMQTT実装（共通） | 同上（共通）＋一時claim証明書を受け取るインターフェース（シリアル等） |
| **このプロジェクトのキッティング省略目標への適合** | ◎ 目標に直接合致。手作業ゼロを実現できる | △ 「PCに繋いで作業する」手順自体は現行方式と変わらない。得られるのは主に**セキュリティ面の改善**（恒久的な共有秘密をファームに残さない） |

**現時点の評価**: 「キッティング作業そのものを省略する」という当初の目的には **by Claim** の方が直接的に効く。**by Trusted User** は「PCでの1台ずつの作業」を許容できるなら、恒久的な共有claim証明書をファームウェアに残さずに済む分セキュリティ的には優れる。両者は選択式ではなくハイブリッドも可能（例: 工場出荷時はby Claim、フィールドでの再登録・回収品の再キッティングはby Trusted Userなど）。

---

## アーキテクチャ

以下は主に **by Claim** 方式の構成。by Trusted Userの場合は「claim証明書をTerraformで作る」代わりに「trusted user用IAMロールを作る」点が異なるが、Provisioning Template・pre-provisioning hook・デバイス側MQTTシーケンスは共通。

### サーバー側（Terraformで構築可能）

| リソース | 役割 |
|---|---|
| `aws_iot_provisioning_template` | フリートプロビジョニングテンプレート本体。`name` / `provisioning_role_arn` / `template_body`（jsonencode） / `enabled` / `pre_provisioning_hook{target_arn}` |
| `aws_iam_role` + `AWSIoTThingsRegistration`（AWS管理ポリシー） | `provisioning_role_arn`用。trust policyは`iot.amazonaws.com`にassumeを許可。IoT Coreがこのロールでthing作成・policy付与などを代行する |
| `aws_iot_certificate`（`active=true`、csr/pem指定なし） | **claim証明書**。CSR等を渡さなければAWS側で鍵ペア＋証明書を自動生成し`private_key`が出力される。全台共通で1個だけ作る |
| `aws_iot_policy` + `aws_iot_policy_attachment` | claim証明書用の極小権限ポリシー。許可するのは `iot:Connect` と `$aws/certificates/create*` `$aws/provisioning-templates/<template>/provision/*` へのPublish/Subscribe/Receiveのみ。テレメトリ送信権限は含めない |
| `aws_lambda_function` + `aws_lambda_permission`（source_account指定） | pre-provisioning hook。MACホワイトリスト照合はここに実装 |
| `aws_iot_policy`（実運用ポリシー） | provisioning template内で`PolicyName`参照する、本番デバイス用の通常ポリシー（既存の[infra/iot.tf](../infra/iot.tf)のものを流用できる想定） |

**Terraformで作らない部分**: 個体別の証明書。実デバイス証明書はランタイムに動的発行されるため、tfstateに何百個も証明書を持たせる必要はない。

### template_body の構造（AWS仕様。Terraformでは`jsonencode`で包むだけ）

```json
{
  "Parameters": {
    "ThingName": { "Type": "String" },
    "SerialNumber": { "Type": "String" },
    "CSR": { "Type": "String" }
  },
  "Resources": {
    "thing": {
      "Type": "AWS::IoT::Thing",
      "Properties": {
        "ThingName": { "Ref": "{{ThingName}}" },
        "AttributePayload": { "mac": { "Ref": "SerialNumber" } },
        "ThingGroups": ["ota-target-car-iot-gw-v1"]
      }
    },
    "certificate": {
      "Type": "AWS::IoT::Certificate",
      "Properties": {
        "CertificateSigningRequest": { "Ref": "{{CSR}}" },
        "Status": "ACTIVE"
      }
    },
    "policy": {
      "Type": "AWS::IoT::Policy",
      "Properties": { "PolicyName": "car-iot-gw-device-policy" }
    }
  }
}
```

`ThingGroups`のようにボードバージョン別に変える値は、pre-provisioning hookの`parameterOverrides`で埋めるか、`Fn::FindInMap`で分岐させる想定。

### pre-provisioning hook（Lambda）の入出力

**入力**（IoT Coreが送る）:

```json
{
  "claimCertificateId": "...",
  "certificateId": "...",
  "certificatePem": "...",
  "templateArn": "...",
  "clientId": "...",
  "parameters": { "SerialNumber": "<デバイスが送ったMAC>" }
}
```

**出力**:

```json
{ "allowProvisioning": true, "parameterOverrides": { "BoardVersion": "1" } }
```

`parameters.SerialNumber`（MAC）を事前登録リスト（DynamoDBなど）と照合し、未登録MACなら`allowProvisioning: false`で拒否する。**5秒以内にレスポンスを返す必要がある**制約あり。

### デバイス側のMQTTシーケンス

claim証明書で接続後、以下の3往復を実装する必要がある（現行の[provision.cpp](src/provision.cpp)は全面的に置き換えになる想定）:

1. ローカルで鍵ペア生成 → CSR作成
2. `$aws/certificates/create-from-csr/json` にPublish → `.../accepted`をSubscribeして`certificateOwnershipToken`を受け取る
3. `$aws/provisioning-templates/{template}/provision/json` にMAC等の`parameters`と`certificateOwnershipToken`を含めてPublish → `.../accepted`で`thingName`を受け取る

---

## TODO

- [ ] **by Claim / by Trusted User のどちらを採用するか決める**（上記比較表を参照。ハイブリッド運用にするかも含めて）
- [ ] ESP32 Arduino core（mbedTLS）でCSR生成が可能か検証（未検証。両方式で共通の前提）
- [ ] pre-provisioning hook Lambdaの実装（両方式で共通）
- [ ] `infra/iot.tf` への`aws_iot_provisioning_template`等の追加
- [ ] `esp32_iot_gateway/src/provision.cpp` のMQTTシーケンス実装（CSR/CreateCertificateFromCsr/RegisterThing。両方式で共通）
- [ ] `RELEASE.md`「新規デバイスのprovisioning」節の更新
- [ ] 既存稼働中デバイス（現行方式で登録済み）との混在期間の扱いを決める

### by Claim を採る場合のTODO

- [ ] claim証明書の配布方法を決める（ファームウェアへの焼き込み方法。ビルド時にCI経由で注入するか、リポジトリに暗号化して置くか）
- [ ] MAC事前登録リストの管理方法を決める（DynamoDB / SSM Parameter / S3のいずれか）
- [ ] claim証明書の監視・漏洩検知方法（CloudWatch Logs/Metricsでの異常なプロビジョニング試行の検知）
- [ ] `ops/provision_device.ps1` の位置づけ整理（廃止するか、claim証明書配布用に役割変更するか）

### by Trusted User を採る場合のTODO

- [ ] `CreateProvisioningClaim`のみを許可するIAMロールの設計（作業者用）
- [ ] 一時claim証明書をデバイスへ渡す手段の実装（シリアル経由でよいか、5分のタイムリミットに現行の書き込み〜起動フローが収まるか検証）
- [ ] `ops/provision_device.ps1` を「一時claim証明書取得＋転送」役に書き換える設計

---

## 参考

- [Provisioning templates - AWS IoT Core](https://docs.aws.amazon.com/iot/latest/developerguide/provision-template.html)
- [Pre-provisioning hooks - AWS IoT Core](https://docs.aws.amazon.com/iot/latest/developerguide/pre-provisioning-hook.html)
- [Device provisioning MQTT API - AWS IoT Core](https://docs.aws.amazon.com/iot/latest/developerguide/fleet-provision-api.html)
- [Device provisioning - AWS IoT Core](https://docs.aws.amazon.com/iot/latest/developerguide/iot-provision.html)
- [Provisioning devices that don't have device certificates using fleet provisioning - AWS IoT Core](https://docs.aws.amazon.com/iot/latest/developerguide/provision-wo-cert.html)
- [aws_iot_provisioning_template (Terraform AWS Provider)](https://registry.terraform.io/providers/hashicorp/aws/latest/docs/resources/iot_provisioning_template)
- [aws_iot_certificate (Terraform AWS Provider)](https://registry.terraform.io/providers/hashicorp/aws/latest/docs/resources/iot_certificate)
