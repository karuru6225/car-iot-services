"""
BOM チェックツール
LCSC Part # から EasyEDA/LCSC の部品説明を取得して bom_check.csv に出力する。

使い方:
  ~/.platformio/penv/Scripts/python.exe ops/bom_check.py <input_bom.csv> [output.csv]

例:
  ~/.platformio/penv/Scripts/python.exe ops/bom_check.py m5atom_power_adc/production/bom.csv
"""

import csv
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

EASYEDA_SEARCH_URL = "https://pro.easyeda.com/api/eda/product/search"
REQUEST_INTERVAL = 0.5  # API リクエスト間隔（秒）

HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
    "Accept": "application/json, text/plain, */*",
    "Referer": "https://pro.easyeda.com/",
}


def _extract_category(url_path: str) -> str:
    """URL パス (/product-detail/Category_MPN_LCSC.html) からカテゴリを取り出す。"""
    if not url_path:
        return ""
    import re
    part = url_path.lstrip("/").removeprefix("product-detail/")
    raw = part.split("_")[0].replace("-", " ")
    return re.sub(r" {2,}", " ", raw).strip()


def fetch_lcsc_info(lcsc_part: str) -> str:
    """EasyEDA API から LCSC パーツの説明文を取得する。"""
    url = f"{EASYEDA_SEARCH_URL}?keyword={lcsc_part}&currentPage=1&pageSize=20"
    req = urllib.request.Request(url, headers=HEADERS)
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read())

        products = data.get("result", {}).get("productList", [])
        if not products:
            return "NOT FOUND"

        # LCSC 番号が一致するものを探す（ページ内で完全一致優先）
        target = None
        for p in products:
            num = str(p.get("number", ""))
            if num.upper() == lcsc_part.upper():
                target = p
                break
        if target is None:
            # 数字部分で照合（"C1017" vs number="1017" 形式への対応）
            lcsc_num = lcsc_part.lstrip("Cc")
            for p in products:
                if str(p.get("number", "")).lstrip("Cc") == lcsc_num:
                    target = p
                    break
        if target is None:
            got = products[0].get("number", "?")
            return f"MISMATCH: got {got}"

        mpn = target.get("mpn", "")
        manufacturer = target.get("manufacturer", "")
        package = target.get("package", "")
        category = _extract_category(target.get("url", ""))

        # device_info.Description に仕様パラメータが入っている
        di = target.get("device_info") or {}
        spec_desc = di.get("Description") or di.get("description") or ""

        # 出力: "MPN [Manufacturer] | Category | Package | Spec..."
        parts = [f"{mpn} [{manufacturer}]", category, package]
        base = " | ".join(p for p in parts if p)
        if spec_desc:
            return f"{base} | {spec_desc}"
        return base

    except urllib.error.HTTPError as e:
        return f"HTTP ERROR {e.code}"
    except Exception as e:
        return f"ERROR: {e}"


def process_bom(input_path: Path, output_path: Path) -> None:
    with input_path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames is None:
            print("ERROR: CSVが空またはヘッダーがない")
            sys.exit(1)
        fieldnames = list(reader.fieldnames) + ["LCSC Description"]
        rows = list(reader)

    total = len(rows)
    for i, row in enumerate(rows):
        lcsc = row.get("LCSC Part #", "").strip()
        if lcsc:
            print(f"[{i+1}/{total}] {lcsc} ...", end=" ", flush=True)
            desc = fetch_lcsc_info(lcsc)
            print(desc[:100] if len(desc) > 100 else desc)
            row["LCSC Description"] = desc
            time.sleep(REQUEST_INTERVAL)
        else:
            row["LCSC Description"] = ""
            print(f"[{i+1}/{total}] LCSC番号なし ({row.get('Designator', '')})")

    with output_path.open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"\n完了: {output_path}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    input_path = Path(sys.argv[1])
    if not input_path.exists():
        print(f"ERROR: ファイルが見つからない: {input_path}")
        sys.exit(1)

    output_path = Path(sys.argv[2]) if len(sys.argv) >= 3 else input_path.parent / "bom_check.csv"

    process_bom(input_path, output_path)
