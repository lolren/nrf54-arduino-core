## UF2 Tooling

This directory vendors the upstream Microsoft UF2 conversion utility and keeps
our board-specific integration in a small local wrapper.

Files:

- `uf2conv.py`: upstream converter from https://github.com/microsoft/uf2
- `uf2families.json`: upstream family table used by `uf2conv.py`
- `LICENSE.txt`: upstream MIT license text
- `uf2_emit.py`: local wrapper used by `platform.txt`

Why the wrapper exists:

- future boards can override `build.uf2_family` and `build.uf2_base_address`
  in `boards.txt` without patching `uf2conv.py`
- the `platform.txt` recipe stays stable even if the exact UF2 input/output
  policy changes later

Current board properties consumed by `uf2_emit.py`:

- `build.uf2_family`
- `build.uf2_base_address`
