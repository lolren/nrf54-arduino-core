#!/usr/bin/env python3
"""Compile advertised features and board examples from an exact release archive."""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path


FEATURE_BUILDS = (
    (
        "xiao_nrf54l15",
        "libraries/Nrf54L15-Clean-Implementation/examples/Peripherals/SaadcOversamplingProbe",
    ),
    (
        "xiao_nrf54lm20b",
        "libraries/Nrf54L15-Clean-Implementation/examples/Peripherals/SaadcOversamplingProbe",
    ),
    (
        "xiao_nrf54l15",
        "examples/Wire/WireScanner",
    ),
    (
        "xiao_nrf54lm20b",
        "examples/Wire/WireScanner",
    ),
    (
        "xiao_nrf54l15",
        "libraries/Bluefruit52Lib/examples/HID/blehid_mouse",
    ),
    (
        "xiao_nrf54l15",
        "libraries/Bluefruit52Lib/examples/Services/ancs",
    ),
    (
        "xiao_nrf54l15",
        "libraries/Bluefruit52Lib/examples/Security/pairing_numeric_comparison",
    ),
    (
        "xiao_nrf54l15",
        "libraries/Nrf54L15-Clean-Implementation/examples/BLE/Security/BleOobPairPeripheral",
    ),
    (
        "xiao_nrf54lm20b",
        "libraries/Nrf54L15-Clean-Implementation/examples/BLE/Security/BleOobPairCentral",
    ),
    (
        "xiao_nrf54l15",
        "libraries/Nrf54L15-Clean-Implementation/examples/BLE/Privacy/BleResolvablePrivateAddress",
    ),
    (
        "xiao_nrf54lm20b",
        "libraries/Nrf54L15-Clean-Implementation/examples/BLE/Scanning/BleActiveScanner",
    ),
    (
        "xiao_nrf54l15:clean_ble=on,cpu_freq=128m",
        "libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingInitiator",
    ),
    (
        "xiao_nrf54lm20b:clean_ble=on,cpu_freq=128m",
        "libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingReflector",
    ),
    (
        "xiao_nrf54l15:clean_thread=stage",
        "libraries/Nrf54L15-Clean-Implementation/examples/Thread/OpenThreadCoreStageProbe",
    ),
    (
        "xiao_nrf54lm20b:clean_thread=stage",
        "libraries/Nrf54L15-Clean-Implementation/examples/Thread/OpenThreadCoreStageProbe",
    ),
    (
        "xiao_nrf54l15:clean_thread=stage,clean_matter=stage",
        "libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnOffLightFoundationCompileTarget",
    ),
    (
        "xiao_nrf54lm20b:clean_thread=stage,clean_matter=stage",
        "libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnOffLightFoundationCompileTarget",
    ),
    (
        "xiao_nrf54l15",
        "libraries/nRF54-Board-Examples/examples/XIAO-nRF54L15/XiaoBoardControlPins",
    ),
    (
        "xiao_nrf54l15",
        "libraries/nRF54-Board-Examples/examples/XIAO-nRF54L15-Sense/XiaoSenseMicLevel",
    ),
    (
        "xiao_nrf54lm20b",
        "libraries/nRF54-Board-Examples/examples/XIAO-nRF54LM20A/FlashInfo",
    ),
    (
        "xiao_nrf54lm20b",
        "libraries/nRF54-Board-Examples/examples/XIAO-nRF54LM20A-Sense/XiaoLM20A_MicLevel",
    ),
    (
        "holyiot_25008_nrf54l15",
        "libraries/nRF54-Board-Examples/examples/HOLYIOT-25008/Holyiot25008RgbButton",
    ),
    (
        "nrf54l15dk_pca10156",
        "libraries/nRF54-Board-Examples/examples/Nordic-nRF54L15-DK/Nrf54L15DkLinearGpioMap",
    ),
)
LOCAL_PACKAGER = "localnrf54"
DECLARED_COMPILER_VERSION = "7-2017q4"
REQUIRED_ARCHIVE_SHA256 = {
    "tools/nrf_ocd": "b05cc011852292f71119eab836bde413dec9dbf1e70428dce1020037f2d547a0",
    "tools/nrf_ocd.exe": "1f66274c97d6617729debb2585260e90fc4e0e0dd4e35e51e1cca582bb9f1434",
    "tools/nrf_ocd-compliance/open-nrf-ocd-v0.3.8-source.tar.gz":
        "962c788d860b968d27917d3cdc35c7b4df28cf4bdae6000fef853c7b9743c27c",
    "tools/nrf_ocd-compliance/libusb-1.0.27.tar.bz2":
        "ffaa41d741a8a3bee244ac8e54a72ea05bf2879663c098c82fc5757853441575",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--compiler-path", type=Path)
    parser.add_argument("--timeout", type=int, default=900)
    return parser.parse_args()


def find_compiler_path() -> Path | None:
    roots = (
        Path.home() / ".arduino15" / "packages" / "arduino" / "tools" / "arm-none-eabi-gcc",
        Path.home() / "Library" / "Arduino15" / "packages" / "arduino" / "tools" / "arm-none-eabi-gcc",
        Path(os.environ.get("LOCALAPPDATA", "")) / "Arduino15" / "packages" / "arduino" / "tools" / "arm-none-eabi-gcc",
    )
    for root in roots:
        declared = root / DECLARED_COMPILER_VERSION / "bin"
        if declared.is_dir():
            return declared
    return None


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_text_markers(path: Path, markers: tuple[str, ...]) -> None:
    text = path.read_text(encoding="utf-8")
    missing = [marker for marker in markers if marker not in text]
    if missing:
        raise SystemExit(
            f"release archive notice check failed for {path}: missing {missing}"
        )


def verify_nrf_ocd_source_notices(source_archive: Path) -> None:
    expected = {
        "src/flash_algo_nrf54l.c": (
            "Copyright (c) 2010-2023 Nordic Semiconductor ASA",
            "Copyright (c) 2025 StarSphere",
            "SPDX-License-Identifier: Apache-2.0",
            "Adapted for nRF OCD in 2026",
        ),
        "src/target_nrf54l.c": (
            "Copyright (c) 2006-2013 Arm Limited",
            "Copyright (c) 2019 Monadnock Systems Ltd.",
            "SPDX-License-Identifier: Apache-2.0",
            "Adapted for nRF OCD in 2026",
        ),
        "src/target_nrf54lm20a.c": (
            "Copyright (c) 2025 StarSphere",
            "SPDX-License-Identifier: Apache-2.0",
            "Adapted for nRF OCD in 2026",
        ),
        "src/hid_libusb.c": (
            "Copyright (c) 2021-2023 Chris Reed",
            "Copyright (c) 2025 Lars Häring",
            "SPDX-License-Identifier: Apache-2.0",
            "Adapted for nRF OCD in 2026",
        ),
    }
    with tarfile.open(source_archive, "r:gz") as archive:
        files = {member.name: member for member in archive.getmembers() if member.isfile()}
        for suffix, markers in expected.items():
            matches = [member for name, member in files.items() if name.endswith(suffix)]
            if len(matches) != 1:
                raise SystemExit(
                    f"nRF OCD source must contain one {suffix}, found {len(matches)}"
                )
            handle = archive.extractfile(matches[0])
            if handle is None:
                raise SystemExit(f"could not read nRF OCD source member {suffix}")
            text = handle.read().decode("utf-8")
            missing = [marker for marker in markers if marker not in text]
            if missing:
                raise SystemExit(
                    f"nRF OCD source notice check failed for {suffix}: missing {missing}"
                )


def main() -> int:
    args = parse_args()
    archive = args.archive.resolve()
    if not archive.is_file():
        raise SystemExit(f"archive not found: {archive}")
    if shutil.which("arduino-cli") is None:
        raise SystemExit("arduino-cli is required")
    compiler = args.compiler_path.resolve() if args.compiler_path else find_compiler_path()
    if compiler is None or not compiler.is_dir():
        raise SystemExit(
            f"ARM compiler {DECLARED_COMPILER_VERSION} bin directory not found; "
            "pass --compiler-path"
        )

    with tempfile.TemporaryDirectory(prefix="nrf54-release-verify-") as td:
        temp = Path(td)
        extracted = temp / "extracted"
        extracted.mkdir()
        with tarfile.open(archive, "r:*") as tar:
            tar.extractall(extracted)
        roots = [path for path in extracted.iterdir() if path.is_dir()]
        if len(roots) != 1:
            raise SystemExit(f"archive must contain one root directory, found {len(roots)}")
        platform = roots[0]

        required = (
            platform / "LICENSE",
            platform / "THIRD_PARTY_NOTICES.md",
            platform / "LICENSES/Apache-2.0.txt",
            platform / "LICENSES/LGPL-2.1-or-later.txt",
            platform / "LICENSES/TinyUSB-MIT.txt",
            platform / "LICENSES/Unlicense.txt",
            platform / "libraries/Bluefruit52Lib/LICENSE",
            platform / "libraries/Bluefruit52Lib/examples/Services/custom_htm/LICENSE",
            platform / "libraries/Adafruit_SPIFlash/LICENSE",
            platform / "libraries/nRF54-Board-Examples/library.properties",
            platform / "libraries/nRF54-Board-Examples/examples/XIAO-nRF54L15-Sense/XiaoSenseMicLevel/XiaoSenseMicLevel.ino",
            platform / "libraries/nRF54-Board-Examples/examples/XIAO-nRF54LM20A/FlashInfo/FlashInfo.ino",
            platform / "libraries/nRF54-Board-Examples/examples/XIAO-nRF54LM20A-Sense/XiaoLM20A_MicLevel/XiaoLM20A_MicLevel.ino",
            platform / "libraries/nRF54-Board-Examples/examples/HOLYIOT-25008/Holyiot25008RgbButton/Holyiot25008RgbButton.ino",
            platform / "libraries/nRF54-Board-Examples/examples/Nordic-nRF54L15-DK/Nrf54L15DkLinearGpioMap/Nrf54L15DkLinearGpioMap.ino",
            platform / "libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/LICENSE",
            platform / "libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/LICENSE-ATTRIBUTION.txt",
            platform / "libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/VERSION",
            platform / "libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/openthread-LICENSE.txt",
            platform / "libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/openthread-NOTICE.txt",
            platform / "libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/third_party/mbedtls/repo/LICENSE",
            platform / "libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/connectedhomeip-LICENSE.txt",
            platform / "libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/connectedhomeip-NOTICE.txt",
            platform / "libraries/Nrf54L15-Clean-Implementation/src/openthread-LICENSE.txt",
            platform / "libraries/Nrf54L15-Clean-Implementation/src/openthread-NOTICE.txt",
            platform / "libraries/Nrf54L15-Clean-Implementation/src/openthread_core_stage",
            platform / "libraries/Nrf54L15-Clean-Implementation/src/openthread_core_stage_bridge.cpp",
            platform / "cores/nrf54l15/BuildTargetGuard.h",
            platform / "cores/nrf54lm20b/BuildTargetGuard.h",
            platform / "tools/build_cache_guard.py",
            platform / "tools/build_cache_guard.ps1",
            platform / "tools/nrf_ocd-compliance/nrf_ocd-MIT.txt",
            platform / "tools/nrf_ocd-compliance/nrf_ocd-THIRD_PARTY_NOTICES.md",
            platform / "tools/nrf_ocd-compliance/README.md",
            platform / "tools/nrf_ocd-compliance/open-nrf-ocd-v0.3.8-source.tar.gz",
            platform / "tools/nrf_ocd-compliance/libusb-1.0.27.tar.bz2",
            platform / "tools/nrf_ocd-compliance/rebuild-linux-x86_64.sh",
            platform / "tools/nrf54l15hosttools/1.1.3/LICENSE",
            platform / "tools/nrf54l15hosttools/1.1.3/THIRD_PARTY_NOTICES.md",
            platform / "tools/uf2/LICENSE.txt",
        )
        missing = [str(path.relative_to(platform)) for path in required if not path.exists()]
        if missing:
            raise SystemExit(f"release archive omits advertised feature sources: {missing}")

        stale_board_examples = (
            "libraries/Adafruit_SPIFlash/examples/FlashInfo/FlashInfo.ino",
            "libraries/Adafruit_SPIFlash/examples/FlashReadWrite/FlashReadWrite.ino",
            "libraries/HOLYIOT-25008-Examples/library.properties",
            "examples/HolyIoT/Holyiot25008RgbButton/Holyiot25008RgbButton.ino",
            "examples/XiaoL15/XiaoSenseMicLevel/XiaoSenseMicLevel.ino",
            "examples/XiaoLM20A/XiaoLM20A_MicLevel/XiaoLM20A_MicLevel.ino",
        )
        stale = [path for path in stale_board_examples if (platform / path).exists()]
        if stale:
            raise SystemExit(f"release archive contains stale board example menus: {stale}")

        require_text_markers(
            platform / "THIRD_PARTY_NOTICES.md",
            ("Nordic SDC/MPSL", "ConnectedHomeIP scaffold", "nRF OCD native uploader"),
        )
        require_text_markers(
            platform / "LICENSES/Apache-2.0.txt",
            ("Apache License", "Version 2.0, January 2004", "TERMS AND CONDITIONS"),
        )
        require_text_markers(
            platform / "LICENSES/LGPL-2.1-or-later.txt",
            ("GNU LESSER GENERAL PUBLIC LICENSE", "Version 2.1, February 1999"),
        )
        nordic_sdc = (
            platform
            / "libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc"
        )
        require_text_markers(
            nordic_sdc / "LICENSE",
            (
                "LicenseRef-Nordic-5-Clause",
                "Nordic Semiconductor ASA",
                "4. This software, with or without modification",
                "5. Any software provided in binary form",
            ),
        )
        require_text_markers(
            nordic_sdc / "LICENSE-ATTRIBUTION.txt",
            ("BSD-3-Clause", "Copyright (c) 2013 ARM Ltd"),
        )
        require_text_markers(
            nordic_sdc / "VERSION",
            ("Nordic nrfxlib revision:", "SoftDevice Controller multirole"),
        )
        matter_notice = (
            platform
            / "libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/connectedhomeip-NOTICE.txt"
        )
        require_text_markers(
            matter_notice,
            ("The Matter SDK is an open source implementation", "This NOTICE must be included"),
        )
        for notice in (
            platform
            / "libraries/Nrf54L15-Clean-Implementation/src/openthread-NOTICE.txt",
            platform
            / "libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/openthread-NOTICE.txt",
        ):
            require_text_markers(
                notice,
                ("OpenThread is an open source implementation", "Thread 1.4.0 Final Specification"),
            )
        nrf_ocd_compliance = platform / "tools/nrf_ocd-compliance"
        require_text_markers(
            nrf_ocd_compliance / "nrf_ocd-THIRD_PARTY_NOTICES.md",
            ("pyOCD", "libusb", "1.0.27", "LGPL-2.1-or-later"),
        )
        require_text_markers(
            nrf_ocd_compliance / "README.md",
            (
                "nRF OCD Corresponding Source",
                "open-nrf-ocd-v0.3.8-source.tar.gz",
                "rebuild-linux-x86_64.sh",
            ),
        )

        pyocd_notice_markers = (
            "Copyright (c) 2006-2013 Arm Limited",
            "Copyright (c) 2010-2023 Nordic Semiconductor ASA",
            "Copyright (c) 2025 StarSphere",
            "SPDX-License-Identifier: Apache-2.0",
            "Adapted for this core in 2026",
        )
        for name in (
            "target_nRF54LM20A.py",
            "pyocd_register_lm20b.py",
            "pyocd_target_nRF54LM20A.py",
        ):
            require_text_markers(platform / "tools" / name, pyocd_notice_markers)

        library_notice_markers = {
            "Preferences/src/Preferences.h": (
                "Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD",
                "SPDX-License-Identifier: Apache-2.0",
                "Modified for the nRF54 Arduino Core in 2026",
            ),
            "Preferences/src/Preferences.cpp": (
                "Copyright 2015-2021 Espressif Systems (Shanghai) PTE LTD",
                "SPDX-License-Identifier: Apache-2.0",
                "Modified for the nRF54 Arduino Core in 2026",
            ),
            "EEPROM/src/EEPROM.h": (
                "Original copyright (c) 2006 David A. Mellis",
                "Copyright (c) 2014 Ivan Grokhotkov",
                "SPDX-License-Identifier: LGPL-2.1-or-later",
                "Modified for the nRF54 Arduino Core in 2026",
            ),
            "EEPROM/src/EEPROM.cpp": (
                "Original copyright (c) 2006 David A. Mellis",
                "Copyright (c) 2014 Ivan Grokhotkov",
                "SPDX-License-Identifier: LGPL-2.1-or-later",
                "Modified for the nRF54 Arduino Core in 2026",
            ),
        }
        for name, markers in library_notice_markers.items():
            require_text_markers(platform / "libraries" / name, markers)

        arduino_notice_markers = {
            "Stream.cpp": ("Copyright (c) 2008 David A. Mellis",),
            "Stream.h": ("Copyright (c) 2010 David A. Mellis",),
            "IPAddress.cpp": ("Copyright (c) 2011 Adrian McEwen",),
            "IPAddress.h": ("Copyright (c) 2011 Adrian McEwen",),
            "Printable.h": ("Copyright (c) 2011 Adrian McEwen",),
            "Print.cpp": ("Copyright (c) 2008 David A. Mellis", "Chuck Todd"),
            "Print.h": ("Copyright (c) 2008 David A. Mellis",),
            "WCharacter.h": ("Copyright (c) 2010 Hernando Barragan",),
            "WString.h": ("Copyright (c) 2009-2010 Hernando Barragan", "Paul Stoffregen"),
            "Client.h": ("Copyright (c) 2011 Adrian McEwen",),
            "Server.h": ("Copyright (c) 2011 Adrian McEwen",),
        }
        for core in ("nrf54l15", "nrf54lm20b"):
            core_dir = platform / "cores" / core
            require_text_markers(
                core_dir / "Arduino.h",
                (
                    "Copyright (c) 2005-2013 Arduino Team",
                    "SPDX-License-Identifier: LGPL-2.1-or-later",
                    "Modified for the nRF54 Arduino Core in 2026",
                ),
            )
            for name, copyright_markers in arduino_notice_markers.items():
                require_text_markers(
                    core_dir / name,
                    (
                        *copyright_markers,
                        "SPDX-License-Identifier: LGPL-2.1-or-later",
                        "Modified for the nRF54 Arduino Core in 2026",
                    ),
                )
            require_text_markers(
                core_dir / "Udp.h",
                (
                    "Copyright (c) 2008 Bjoern Hartmann",
                    "Permission is hereby granted, free of charge",
                    "Modified for the nRF54 Arduino Core in 2026",
                ),
            )
            require_text_markers(
                core_dir / "utility/SoftwareTimer.h",
                (
                    "Copyright (c) 2018 Adafruit Industries",
                    "Redistribution and use in source and binary forms",
                    "Modified for the nRF54 Arduino Core in 2026",
                ),
            )
            require_text_markers(
                core_dir / "SoftwareTimer.cpp",
                (
                    "Copyright (c) 2019 Ha Thach",
                    "Permission is hereby granted, free of charge",
                    "Modified for the nRF54 Arduino Core in 2026",
                ),
            )
            require_text_markers(
                core_dir / "utility/debug.h",
                (
                    "Copyright (c) 2018 Adafruit Industries",
                    "Redistribution and use in source and binary forms",
                    "Modified for the nRF54 Arduino Core in 2026",
                ),
            )
            require_text_markers(
                core_dir / "avr/pgmspace.h",
                (
                    "Copyright (c) 2015 Arduino LLC",
                    "Based on work of Paul Stoffregen",
                    "Permission is hereby granted, free of charge",
                    "Modified for the nRF54 Arduino Core in 2026",
                ),
            )
            require_text_markers(
                core_dir / "cmsis.h",
                (
                    "Copyright (c) 2009-2020 Arm Limited",
                    "SPDX-License-Identifier: Apache-2.0",
                    "Modified for the nRF54 Arduino Core in 2026",
                ),
            )
            system_name = (
                "system_nrf54l15.c" if core == "nrf54l15" else "system_nrf54lm20b.c"
            )
            require_text_markers(
                core_dir / system_name,
                (
                    "Copyright (c) 2009-2026 ARM Limited",
                    "SPDX-License-Identifier: Apache-2.0",
                    "NOTICE: The upstream system template was modified by Nordic",
                    "Copyright (c) 2019-2026 Nordic Semiconductor ASA",
                    "SPDX-License-Identifier: BSD-3-Clause",
                    "Modified again for the nRF54 Arduino Core in 2026",
                ),
            )
            for name in ("nrfx_temp.h", "nrfx_temp.cpp"):
                require_text_markers(
                    core_dir / name,
                    (
                        "Copyright (c) 2019-2026 Nordic Semiconductor ASA",
                        "2. Redistributions in binary form must reproduce",
                        "SPDX-License-Identifier: BSD-3-Clause",
                        "Adapted for the nRF54 Arduino Core in 2026",
                    ),
                )

        verify_nrf_ocd_source_notices(
            platform
            / "tools/nrf_ocd-compliance/open-nrf-ocd-v0.3.8-source.tar.gz"
        )
        redistributed_wheels = sorted(platform.rglob("*.whl"))
        if redistributed_wheels:
            raise SystemExit(
                "release archive unexpectedly redistributes Python wheels: "
                f"{[path.relative_to(platform).as_posix() for path in redistributed_wheels]}"
            )
        for relative, expected in REQUIRED_ARCHIVE_SHA256.items():
            actual = sha256_file(platform / relative)
            if actual != expected:
                raise SystemExit(
                    f"release archive has unexpected {relative} SHA-256: "
                    f"{actual} != {expected}"
                )

        user = temp / "user"
        data = Path(
            os.environ.get("ARDUINO_DATA_DIR", str(Path.home() / ".arduino15"))
        ).expanduser().resolve()
        if not data.is_dir():
            raise SystemExit(f"Arduino data directory does not exist: {data}")
        downloads = temp / "downloads"
        # Keep the extracted archive in a distinct namespace so an installed
        # Board Manager core cannot satisfy these compiles by accident.
        hardware = user / "hardware" / LOCAL_PACKAGER
        hardware.mkdir(parents=True)
        downloads.mkdir()
        (hardware / "nrf54l15clean").symlink_to(platform, target_is_directory=True)
        config = temp / "arduino-cli.yaml"
        config.write_text(
            "directories:\n"
            f"  data: {data}\n"
            f"  downloads: {downloads}\n"
            f"  user: {user}\n",
            encoding="utf-8",
        )

        for index, (board_options, sketch) in enumerate(FEATURE_BUILDS):
            fqbn = f"{LOCAL_PACKAGER}:nrf54l15clean:{board_options}"
            build_path = temp / f"build-{index}"
            cmd = [
                "arduino-cli",
                "compile",
                "--config-file",
                str(config),
                "--fqbn",
                fqbn,
                "--build-property",
                f"compiler.path={compiler}{os.sep}",
                "--build-path",
                str(build_path),
                str(platform / sketch),
            ]
            completed = subprocess.run(cmd, capture_output=True, text=True, timeout=args.timeout)
            if completed.returncode != 0:
                output = completed.stdout + completed.stderr
                raise SystemExit(f"archive feature compile failed for {fqbn}:\n{output[-12000:]}")

            elf_files = tuple(build_path.glob("*.elf"))
            if len(elf_files) != 1:
                raise SystemExit(
                    f"archive feature compile produced {len(elf_files)} ELF files for {fqbn}"
                )
            nm_name = "arm-none-eabi-nm.exe" if os.name == "nt" else "arm-none-eabi-nm"
            nm = subprocess.run(
                [str(compiler / nm_name), str(elf_files[0])],
                capture_output=True,
                text=True,
                timeout=args.timeout,
            )
            if nm.returncode != 0:
                raise SystemExit(f"could not inspect target identity for {fqbn}: {nm.stderr}")
            lm20 = board_options.split(":", 1)[0] == "xiao_nrf54lm20b"
            expected = (
                "__nrf54_core_target_nrf54lm20"
                if lm20
                else "__nrf54_core_target_nrf54l15"
            )
            forbidden = (
                "__nrf54_core_target_nrf54l15"
                if lm20
                else "__nrf54_core_target_nrf54lm20"
            )
            if expected not in nm.stdout or forbidden in nm.stdout:
                raise SystemExit(
                    f"archive feature compile has mixed or missing SoC identity for {fqbn}"
                )
            print(f"archive feature compile OK: {fqbn} {sketch}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
