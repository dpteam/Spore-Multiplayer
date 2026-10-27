"""
Downloads real creations (Model-in-Picture PNG files) from the Sporepedia so the
avatar sharing can be tested without creating creatures by hand.

The search uses the same public DWR endpoint as https://github.com/LEv145/sporepedia.py
and the files come from the Sporepedia thumbnail CDN: those thumbnails *are* the
creation PNGs, with the creation data hidden inside the image.

Usage:
    python Tools/download-test-creations.py --count 2
    python Tools/download-test-creations.py --count 4 --function UFO
"""
import argparse
import os
import re
import sys

import requests

SEARCH_URL = "https://www.spore.com/jsserv/call/plaincall/searchService.searchAssetsDWR.dwr"
SESSION_ID = "B46A8740BB941667AB32B719F1B7115A19"
THUMB_URL = "http://static.spore.com/static/thumb/{}/{}/{}/{}.png"

# Search functions accepted by the Sporepedia API.
FUNCTIONS = ("CREATURE", "UFO", "TRIBE_CREATURE", "CIV_CREATURE", "SPACE_CREATURE")


def search(text, function, count=20):
    data = (
        "callCount=1\n"
        f"scriptSessionId={SESSION_ID}\n"
        "c0-scriptName=searchService\n"
        "c0-methodName=searchAssetsDWR\n"
        "c0-id=0\n"
        "c0-adv=1\n"
        f"c0-searchText={text}\n"
        f"c0-maxResults={count}\n"
        "c0-filter=MOST_POPULAR\n"
        "c0-fields=[name]\n"
        f"c0-functions=[{function}]\n"
        "c0-purposes=[]\n"
        "c0-modes=[]\n"
        "c0-param0=Object_Object:{"
        "adv:reference:c0-adv, "
        "searchText:reference:c0-searchText, "
        "maxResults:reference:c0-maxResults, "
        "fields:reference:c0-fields, "
        "functions:reference:c0-functions, "
        "purposes:reference:c0-purposes, "
        "modes:reference:c0-modes, "
        "filter:reference:c0-filter "
        "}\n"
        "batchId=1"
    )

    response = requests.post(SEARCH_URL, data=data, timeout=30)
    response.raise_for_status()
    return response.text


def extract_assets(text):
    """Pull (assetId, name) pairs out of the DWR payload."""
    assets = []
    seen = set()

    # s1.assetId=500262075795; ... s1.name="Creature";
    for match in re.finditer(r"assetId=(\d{6,})", text):
        asset_id = match.group(1)
        if asset_id in seen:
            continue
        seen.add(asset_id)

        # The name usually appears after the assetId on the same statement block.
        window = text[match.start():match.start() + 1200]
        name_match = re.search(r'\.name="([^"]*)"', window)
        name = name_match.group(1) if name_match else ""

        assets.append((asset_id, name))

    return assets


def thumb_url(asset_id):
    return THUMB_URL.format(asset_id[0:3], asset_id[3:6], asset_id[6:9], asset_id)


def download(asset_id, out_path):
    response = requests.get(thumb_url(asset_id), timeout=30)
    if response.status_code != 200:
        return 0

    content = response.content
    if content[:8] != b"\x89PNG\r\n\x1a\n":
        return 0

    with open(out_path, "wb") as handle:
        handle.write(content)

    return len(content)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--count", type=int, default=2, help="how many files to download")
    parser.add_argument("--text", default="creature", help="search text")
    parser.add_argument("--function", default="CREATURE", choices=FUNCTIONS)
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    out_dir = args.out or os.path.join(os.path.dirname(__file__), "..", "refs", "test-creations")
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)

    text = search(args.text, args.function, 20)
    assets = extract_assets(text)
    print(f"Found {len(assets)} asset(s) for '{args.text}' / {args.function}")

    if not assets:
        print("The Sporepedia returned no results.")
        return 1

    saved = 0
    for asset_id, name in assets:
        if saved >= args.count:
            break

        path = os.path.join(out_dir, f"{args.function.lower()}_{asset_id}.png")
        size = download(asset_id, path)
        if size:
            print(f"  saved {os.path.basename(path)} ({size} bytes) - {name or 'unnamed'}")
            saved += 1
        else:
            print(f"  could not download asset {asset_id}")

    print(f"\nSaved {saved} creation(s) to {out_dir}")
    return 0 if saved else 1


if __name__ == "__main__":
    sys.exit(main())
