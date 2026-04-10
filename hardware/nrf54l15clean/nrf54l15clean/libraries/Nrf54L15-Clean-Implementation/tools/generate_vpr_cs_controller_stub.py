#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile


def run(cmd: list[str]) -> None:
    subprocess.run(cmd, check=True)


def main() -> int:
    repo_root = pathlib.Path(__file__).resolve().parents[6]
    lib_root = pathlib.Path(__file__).resolve().parents[1]
    src_root = lib_root / "src"
    tools_root = lib_root / "tools" / "vpr"
    toolchain = "riscv64-unknown-elf-gcc"
    objcopy = "riscv64-unknown-elf-objcopy"
    firmware_c = tools_root / "vpr_cs_controller_stub.c"
    firmware_ld = tools_root / "vpr_cs_transport_stub.ld"
    out_header = src_root / "vpr_cs_controller_stub_firmware.h"

    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = pathlib.Path(tmp)
        elf = tmpdir / "vpr_cs_controller_stub.elf"
        binary = tmpdir / "vpr_cs_controller_stub.bin"

        run(
            [
                toolchain,
                "-march=rv32emc_zicsr",
                "-mabi=ilp32e",
                "-Oz",
                "-ffreestanding",
                "-ffunction-sections",
                "-fdata-sections",
                "-fno-builtin",
                "-fno-stack-protector",
                "-msmall-data-limit=0",
                "-nostdlib",
                "-Wl,--gc-sections",
                "-Wl,-T",
                str(firmware_ld),
                "-I",
                str(src_root),
                "-I",
                str(tools_root),
                "-o",
                str(elf),
                str(firmware_c),
            ]
        )
        run([objcopy, "-O", "binary", str(elf), str(binary)])
        blob = binary.read_bytes()

    lines = [
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "namespace xiao_nrf54l15 {",
        f"constexpr size_t kVprCsControllerStubFirmwareSize = {len(blob)}U;",
        "alignas(4) constexpr uint8_t kVprCsControllerStubFirmware[] = {",
    ]
    for offset in range(0, len(blob), 12):
        chunk = blob[offset : offset + 12]
        rendered = ", ".join(f"0x{byte:02X}U" for byte in chunk)
        lines.append(f"    {rendered},")
    lines.extend(["};", "", "}  // namespace xiao_nrf54l15", ""])
    out_header.write_text("\n".join(lines), encoding="ascii")
    print(f"generated {out_header.relative_to(repo_root)} ({len(blob)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
