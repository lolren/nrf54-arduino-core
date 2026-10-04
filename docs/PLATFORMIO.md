# Using this core with PlatformIO

In addition to the Arduino IDE, this core can be used as a PlatformIO
platform.

## Basic usage

```ini
[env:xiao_nrf54l15]
platform = https://github.com/lolren/nrf54-arduino-core.git
board = xiao_nrf54l15
framework = arduino
```

Supported `board` values right now: `xiao_nrf54l15`, `xiao_nrf54lm20b`
(Seeed XIAO nRF54LM20A).

## Pinning to a specific version

Without a ref, PlatformIO tracks whatever's currently on `main` -- useful
while developing, but it means the exact source you're building against
can change under you with every new commit upstream.

To pin to an exact, unchanging point in history, add `#<commit-sha>` to
the URL:

```ini
platform = https://github.com/lolren/nrf54-arduino-core.git#<commit-sha>
```

This works today, for any commit, regardless of release process -- it's
plain git/PlatformIO behavior, not something specific to how this repo
tags releases.

Pinning to a clean tagged release (`#v1.0.21` rather than a raw commit
SHA) is also possible with the exact same syntax, but depends on whether
a given tag includes the PlatformIO files (`platform.json`, `boards/`,
`builder/`) -- not every tag necessarily will. Check this repo's releases
to see whether tag-based pinning is set up as of whichever version you're
reading this.

Worth knowing regardless: this is standard PlatformIO behavior for any
git-hosted platform, not specific to this one -- easy to miss if you're
used to registry-published platforms like `espressif32`, which pin via
`platform = espressif32@6.5.0` instead. Those two syntaxes aren't
interchangeable: this platform isn't published to PlatformIO's registry,
so the `@version` form doesn't apply here -- use `#<git-ref>` instead.

## Uploading

Upload goes through [pyOCD](https://pyocd.io/), targeting the board's
onboard CMSIS-DAP debug interface. Make sure pyOCD is installed and on
your `PATH` (`pip install pyocd`), then:

```
pio run -t upload
```

## Known limitations

- Only the XIAO nRF54L15 and XIAO nRF54LM20A have board manifests so far
  -- no DK-specific variant, no HOLYIOT boards, etc.
- Upload doesn't pin to a specific debug probe. Fine with one board
  connected; may behave unpredictably with multiple debug probes attached
  at once.
- The nRF54LM20-DK carries the nRF54LM20B SoC, not the LM20A -- pyOCD's
  `nrf54lm20a` target will correctly refuse to program it
  ("This doesn't look like an nRF54LM20A device!"). This isn't a bug;
  it's pyOCD correctly detecting a chip mismatch. Use actual XIAO
  nRF54LM20A hardware to test that board.