#!/usr/bin/env python3
"""Build Arduino Board Manager release artifacts for Nrf54L15-Clean-Implementation."""

from __future__ import annotations

import argparse
import hashlib
import json
import tarfile
from pathlib import Path


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def build_archive(platform_dir: Path, archive_path: Path, archive_root: str) -> None:
    if archive_path.exists():
        archive_path.unlink()

    def normalize_tarinfo(ti: tarfile.TarInfo) -> tarfile.TarInfo:
        # Reproducible archive metadata across machines/runs.
        ti.uid = 0
        ti.gid = 0
        ti.uname = ""
        ti.gname = ""
        ti.mtime = 0
        if ti.isdir():
            ti.mode = 0o755
        elif ti.isfile():
            ti.mode = 0o755 if (ti.mode & 0o111) else 0o644
        return ti

    def add_path(tar: tarfile.TarFile, fs_path: Path, arcname: str) -> None:
        tarinfo = tar.gettarinfo(str(fs_path), arcname=arcname)
        tarinfo = normalize_tarinfo(tarinfo)
        if fs_path.is_file():
            with fs_path.open("rb") as f:
                tar.addfile(tarinfo, f)
            return
        tar.addfile(tarinfo)

    with tarfile.open(archive_path, mode="w:bz2") as tar:
        add_path(tar, platform_dir, archive_root)
        for child in sorted(platform_dir.rglob("*")):
            rel = child.relative_to(platform_dir).as_posix()
            add_path(tar, child, f"{archive_root}/{rel}")


def make_platform_entry(
    platform_name: str,
    archive_name: str,
    archive_url: str,
    archive_sha256: str,
    archive_size: int,
    version: str,
    repo_url: str,
    architecture: str,
) -> dict:
    return {
        "name": platform_name,
        "architecture": architecture,
        "version": version,
        "category": "Arduino",
        "url": archive_url,
        "archiveFileName": archive_name,
        "checksum": f"SHA-256:{archive_sha256}",
        "size": str(archive_size),
        "boards": [
            {
                "name": "XIAO nRF54L15 (Nrf54L15-Clean-Implementation)",
            }
        ],
        "help": {
            "online": repo_url,
        },
        "toolsDependencies": [
            {
                "packager": "arduino",
                "name": "arm-none-eabi-gcc",
                "version": "7-2017q4",
            },
            {
                "packager": "arduino",
                "name": "openocd",
                "version": "0.11.0-arduino2",
            },
        ],
        "discoveryDependencies": [],
        "monitorDependencies": [],
    }


def make_index(
    packager: str,
    platform_name: str,
    repo_url: str,
    platforms: list[dict] | None = None,
) -> dict:
    return {
        "packages": [
            {
                "name": packager,
                "maintainer": "Nrf54L15-Clean-Implementation",
                "websiteURL": repo_url,
                "email": "",
                "help": {
                    "online": repo_url,
                },
                "platforms": platforms if platforms is not None else [],
                "tools": [],
            }
        ]
    }


def version_sort_key(version: str) -> tuple:
    parts = []
    for token in version.split("."):
        if token.isdigit():
            parts.append((0, int(token)))
        else:
            parts.append((1, token))
    return tuple(parts)


def load_existing_index(index_path: Path, packager: str, platform_name: str, repo_url: str) -> dict:
    if not index_path.is_file():
        return make_index(
            packager=packager,
            platform_name=platform_name,
            repo_url=repo_url,
        )

    data = json.loads(index_path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise SystemExit(f"Unexpected package index structure in {index_path}")

    packages = data.get("packages")
    if not isinstance(packages, list) or not packages:
        return make_index(
            packager=packager,
            platform_name=platform_name,
            repo_url=repo_url,
        )

    package = packages[0]
    if not isinstance(package, dict):
        raise SystemExit(f"Unexpected package entry in {index_path}")

    package["name"] = packager
    package["maintainer"] = "Nrf54L15-Clean-Implementation"
    package["websiteURL"] = repo_url
    package["email"] = ""
    package["help"] = {"online": repo_url}
    if not isinstance(package.get("platforms"), list):
        package["platforms"] = []
    if not isinstance(package.get("tools"), list):
        package["tools"] = []

    return data


def merge_platform(index: dict, platform_entry: dict) -> dict:
    packages = index.setdefault("packages", [])
    if not packages:
        raise SystemExit("Package index missing packages[]")

    package = packages[0]
    platforms = [
        p for p in package.get("platforms", [])
        if isinstance(p, dict) and p.get("version") != platform_entry["version"]
    ]
    platforms.append(platform_entry)
    platforms.sort(key=lambda p: version_sort_key(str(p.get("version", ""))), reverse=True)
    package["platforms"] = platforms
    return index


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        default=Path(__file__).resolve().parents[1],
        type=Path,
        help="Board package root directory (default: script parent)",
    )
    parser.add_argument(
        "--version",
        default=None,
        help="Platform version (default: read version from selected platform.txt)",
    )
    parser.add_argument(
        "--source-version",
        default=None,
        help="Source platform directory version (default: --version)",
    )
    parser.add_argument(
        "--packager",
        default="nrf54l15clean",
        help="Packager name used in package index and FQBN",
    )
    parser.add_argument(
        "--platform-name",
        default="Nrf54L15-Clean-Implementation",
        help="Display name in Boards Manager",
    )
    parser.add_argument(
        "--release-base-url",
        default="https://github.com/REPLACE_ME/REPLACE_ME/releases/download/v{version}",
        help="Release base URL. Supports {version} placeholder.",
    )
    parser.add_argument(
        "--repo-url",
        default="https://github.com/REPLACE_ME/REPLACE_ME",
        help="Repository URL used in package metadata.",
    )
    parser.add_argument(
        "--dist-dir",
        default=None,
        type=Path,
        help="Output directory (default: <root>/dist)",
    )
    parser.add_argument(
        "--existing-index",
        default=None,
        type=Path,
        help="Existing package index to merge into (default: <root>/package_<packager>_index.json)",
    )
    return parser.parse_args()


def detect_source_version(root: Path, packager: str) -> str:
    """Resolve source platform directory name under hardware/<packager>/."""
    hardware_root = root / "hardware" / packager
    if not hardware_root.is_dir():
        raise SystemExit(f"Hardware root not found: {hardware_root}")

    dirs = sorted(p.name for p in hardware_root.iterdir() if p.is_dir())
    if not dirs:
        raise SystemExit(f"No platform source directories found under: {hardware_root}")

    # Prefer a stable architecture-named source directory if present.
    if packager in dirs:
        return packager

    # Fallback to previous behavior for version-named directories.
    return dirs[-1]


def read_platform_version(platform_dir: Path) -> str:
    platform_txt = platform_dir / "platform.txt"
    if not platform_txt.is_file():
        raise SystemExit(f"platform.txt not found: {platform_txt}")

    for line in platform_txt.read_text(encoding="utf-8").splitlines():
        if line.startswith("version="):
            value = line.split("=", 1)[1].strip()
            if value:
                return value
            break
    raise SystemExit(f"version=... not found in {platform_txt}")


def main() -> int:
    args = parse_args()

    root = args.root.resolve()
    dist_dir = args.dist_dir.resolve() if args.dist_dir else (root / "dist")
    dist_dir.mkdir(parents=True, exist_ok=True)

    source_version = (
        args.source_version
        if args.source_version
        else detect_source_version(root=root, packager=args.packager)
    )
    platform_dir = root / "hardware" / args.packager / source_version
    if not platform_dir.is_dir():
        raise SystemExit(f"Platform directory not found: {platform_dir}")

    version = args.version if args.version else read_platform_version(platform_dir)

    archive_name = f"{args.packager}-{version}.tar.bz2"
    archive_path = dist_dir / archive_name
    archive_root = f"{args.packager}-{version}"

    build_archive(platform_dir, archive_path, archive_root)

    archive_sha256 = sha256_file(archive_path)
    archive_size = archive_path.stat().st_size

    release_base_url = args.release_base_url.format(version=version)
    archive_url = f"{release_base_url}/{archive_name}"

    existing_index_path = (
        args.existing_index.resolve()
        if args.existing_index
        else (root / f"package_{args.packager}_index.json")
    )
    existing_index = load_existing_index(
        index_path=existing_index_path,
        packager=args.packager,
        platform_name=args.platform_name,
        repo_url=args.repo_url,
    )
    platform_entry = make_platform_entry(
        platform_name=args.platform_name,
        archive_name=archive_name,
        archive_url=archive_url,
        archive_sha256=archive_sha256,
        archive_size=archive_size,
        version=version,
        repo_url=args.repo_url,
        architecture=args.packager,
    )
    index = merge_platform(existing_index, platform_entry)

    index_path = dist_dir / f"package_{args.packager}_index.json"
    index_path.write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")

    print(f"archive: {archive_path}")
    print(f"sha256:  {archive_sha256}")
    print(f"size:    {archive_size}")
    print(f"index:   {index_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
