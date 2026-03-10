---
status: awaiting_human_verify
trigger: "Investigate why the nRF54L15 clean-core RTT/AUXDATA channel-sounding path yields all-zero local RTT raw bytes on the initiator and non-HCI-looking peer raw bytes, despite the custom phase/CSTONES path working on two attached XIAO nRF54L15 boards."
created: 2026-03-09T21:14:35+00:00
updated: 2026-03-09T21:22:29+00:00
---

## Current Focus

hypothesis: the practical clean-core fix is to keep phase/CSTONES as the supported path and treat RTT/AUXDATA as unsupported unless a controller-backed or fully reverse-engineered decode becomes available.
test: compile and flash both channel sounding examples with RTT disabled by default, then inspect live serial output for continued phase distances and explicit RTT absence.
expecting: the initiator will still produce valid phase-based sweeps while logging `rtt_m=na` and `rtt_channels=0` rather than bogus RTT values.
next_action: wait for user confirmation that the new phase-only behavior matches the intended scope boundary in their workflow.

## Symptoms

expected: local RTT capture on initiator report RX should yield nonzero/raw-decodable AUXDATA and a stable RTT-derived distance term; peer RTT bytes encoded into report should decode consistently.
actual: phase path works, valid channels are high, but initiator local RTT raw bytes are always 0000000000000000 and peer raw bytes vary (examples: 4A313900612A8131, 78312900C8255D2A) and do not match the guessed public HCI mode-1 layout. RTT distance is usually absent or bogus hundreds of meters.
errors: no compile/runtime crash in the last known good build; issue is incorrect/undocumented hardware behavior or incorrect capture sequencing/decoding.
reproduction: compile and flash BleChannelSoundingInitiator to /dev/ttyACM0 and BleChannelSoundingReflector to /dev/ttyACM1, then inspect serial logs from initiator for lraw/praw and rtt_m/rtt_channels.
started: phase implementation already worked earlier in this session. RTT/AUXDATA support was added afterward and has never produced a stable usable RTT result.

## Eliminated

## Evidence

- timestamp: 2026-03-09T21:18:59+00:00
  checked: ble_channel_sounding.cpp RTT path
  found: receiveFrame() enables both AUXDATA channels in acquisition mode, captureAuxDataRtt() heuristically picks one of the two buffers, and parseRttRaw() assumes a guessed 6-byte HCI-like layout from rawBytes.
  implication: local/peer RTT decoding depends on undocumented channel ownership and payload format rather than a validated public interface.

- timestamp: 2026-03-09T21:18:59+00:00
  checked: ble_channel_sounding.h and ble_channel_sounding.cpp sample buffers
  found: BleCsRttSample stores at most 8 raw bytes and encodeRttExtra() serializes at most 8 raw bytes, while AUXDATADMA transfers are word-count based.
  implication: the implementation cannot preserve any RTT format longer than 8 bytes and will silently truncate richer step data.

- timestamp: 2026-03-09T21:18:59+00:00
  checked: Zephyr LE CS HCI type definitions
  found: public LE CS step data is 6 bytes for mode-1 AA-only RTT and 14 bytes for mode-1 sounding-sequence RTT (`bt_hci_le_cs_step_data_mode_1` vs `bt_hci_le_cs_step_data_mode_1_ss_rtt`).
  implication: the current 8-byte raw buffer is incompatible with sounding-sequence RTT and only barely fits AA-only RTT plus alignment slack.

- timestamp: 2026-03-09T21:18:59+00:00
  checked: Nordic SoftDevice Controller channel sounding documentation and sample config
  found: controller docs mark RTT with AA-only as supported, RTT with sounding sequence as not supported, and the official ranging initiator sample sets `rtt_type = BT_CONN_LE_CS_RTT_TYPE_AA_ONLY`.
  implication: a practical public implementation boundary is AA-only RTT via controller-managed CS results, not arbitrary sounding-sequence RTT via bare RADIO registers.

- timestamp: 2026-03-09T21:18:59+00:00
  checked: nRF54L15 datasheet/register descriptions and generated headers
  found: public docs expose `AUXDATA.CNF[n]`, `AUXDATADMA[n]`, and `RTT.CONFIG`, but do not document AUXDATA payload layout or the semantic meaning of the two AUXDATA channels; generated headers label the blocks as unspecified.
  implication: there is not enough public hardware documentation to decode bare AUXDATA RTT results reliably without proprietary controller knowledge or empirical reverse engineering.

## Resolution

root_cause: The clean-core RTT implementation relied on undocumented bare RADIO AUXDATA semantics, enabled both AUXDATA DMA channels without role-specific meaning, truncated raw samples to 8 bytes, and assumed a controller/HCI-style RTT layout that public nRF54L15 RADIO documentation does not define. Nordic's own public CS support documents only guarantee AA-only RTT through controller-managed CS results, not sounding-sequence RTT via raw RADIO AUXDATA.
fix: Disable RTT by default for the clean-core BLE channel sounding path and make initiator logging treat RTT as unavailable unless validated data is actually present.
verification: Both examples compiled with `arduino-cli` for `nrf54l15clean:nrf54l15clean:xiao_nrf54l15`, were uploaded to `/dev/ttyACM0` and `/dev/ttyACM1`, and live initiator logs showed phase-only operation (`dist_m=0.0425 ... phase_m=0.0425 rtt_m=na rtt_channels=0`) while the reflector continued replying (`replies=1641`).
files_changed:
  - /home/lolren/Desktop/Nrf54L15/repo/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/ble_channel_sounding.h
  - /home/lolren/Desktop/Nrf54L15/repo/hardware/nrf54l15clean/nrf54l15clean/examples/BLE/BleChannelSoundingInitiator/BleChannelSoundingInitiator.ino
  - /home/lolren/Desktop/Nrf54L15/repo/hardware/nrf54l15clean/nrf54l15clean/examples/BLE/BleChannelSoundingReflector/BleChannelSoundingReflector.ino
