/*
 * BlePassiveScanner
 *
 * Listens for nearby BLE advertising packets and prints each one to Serial.
 * Unlike BleActiveScanner, this sketch NEVER transmits a SCAN_REQ back to
 * the advertiser, so no extra radio traffic is generated and the sketch is
 * completely silent on-air.
 *
 * Useful for:
 * - Confirming that nearby advertisers are visible from this board.
 * - Observing raw PDU types (ADV_IND, ADV_NONCONN_IND, ADV_SCAN_IND, etc.).
 * - Checking RSSI levels and advertiser addresses.
 *
 * Tip: increase kScanSpinPerChannel if packets are missed at the cost of
 * higher average current during scanning.
 */

#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

// Passive scanner example.
//
// Unlike BleActiveScanner, this sketch never asks for a scan response. It only
// reports the advertising packets that were received on-air.

static BleRadio g_ble;
static bool g_bleReady = false;
static uint32_t g_seenPackets = 0;
static uint32_t g_missWindows = 0;
static uint32_t g_lastStatusMs = 0;

// Listen budget per advertising channel for the raw scan loop.
// kTxPowerDbm: radio transmit power in dBm. Range is typically -40 to +8 dBm.
//   Lower values save power; higher values increase range.
//   For a passive scanner the radio only receives, but the HAL still programs
//   TXPOWER during initialisation.
// kScanSpinPerChannel: how many microseconds to spin-wait on each of the three
//   primary advertising channels (37, 38, 39) before giving up and returning
//   a "miss". Larger values catch slower advertising intervals but keep the CPU
//   busier. 2 000 000 us = 2 s per channel.
static constexpr int8_t kTxPowerDbm = -8;
static constexpr uint32_t kScanSpinPerChannel = 2000000UL;

static const char* pduTypeName(uint8_t type) {
  switch (type & 0x0FU) {
    case 0x00:
      return "ADV_IND";
    case 0x01:
      return "ADV_DIRECT";
    case 0x02:
      return "ADV_NONCONN";
    case 0x03:
      return "SCAN_REQ";
    case 0x04:
      return "SCAN_RSP";
    case 0x05:
      return "CONNECT_IND";
    case 0x06:
      return "ADV_SCAN";
    default:
      return "OTHER";
  }
}

static void printAddress(const uint8_t* addr) {
  if (addr == nullptr) {
    return;
  }

  // BLE addresses are typically shown MSB first.
  for (int i = 5; i >= 0; --i) {
    if (addr[i] < 16U) {
      Serial.print('0');
    }
    Serial.print(addr[i], HEX);
    if (i > 0) {
      Serial.print(':');
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(350);  // Allow USB CDC to enumerate before printing.

  Serial.print("\r\nBlePassiveScanner start\r\n");
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);  // LED is active-low; true = off.

  // begin() starts the HFXO clock, programs the radio, and configures BLE
  // timing. It must succeed before any scan or advertise call is valid.
  const bool ok = g_ble.begin(kTxPowerDbm);
  g_bleReady = ok;
  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (!ok) {
    Serial.print("Hint: enable Tools -> BLE Support = Enabled\r\n");
  }
}

void loop() {
  if (!g_bleReady) {
    const uint32_t now = millis();
    if ((now - g_lastStatusMs) >= 2000UL) {
      g_lastStatusMs = now;
      Serial.print("BLE init not ready; scanner idle\r\n");
    }
    delay(50);
    return;
  }

  BleScanPacket pkt{};
  // scanCycle() listens passively on channels 37, 38, and 39 in sequence.
  // It returns true if at least one advertising packet was received. Because
  // this is a passive scan, no SCAN_REQ is ever transmitted.
  const bool got = g_ble.scanCycle(&pkt, kScanSpinPerChannel);

  if (!got) {
    ++g_missWindows;
    const uint32_t now = millis();
    if ((now - g_lastStatusMs) >= 1000UL) {
      g_lastStatusMs = now;
      Serial.print("scanning... hits=");
      Serial.print(g_seenPackets);
      Serial.print(" misses=");
      Serial.print(g_missWindows);
      Serial.print(" spin=");
      Serial.print(kScanSpinPerChannel);
      Serial.print("\r\n");
    }
    // Keep scanning actively; WFI can stall indefinitely if no wake IRQ is armed.
    delay(1);
    return;
  }

  ++g_seenPackets;
  Gpio::write(kPinUserLed, (g_seenPackets & 0x1U) == 0U);  // Toggle LED on each packet.

  // Extract fields from the raw BLE PDU header byte:
  // Bits [3:0] = PDU type (ADV_IND, ADV_DIRECT, ADV_NONCONN, etc.)
  // Bit  [6]   = TxAdd: 1 = advertiser address is random, 0 = public
  const uint8_t pduType = pkt.pduHeader & 0x0FU;
  const bool txRandom = ((pkt.pduHeader >> 6U) & 0x1U) != 0U;

  Serial.print("#");
  Serial.print(g_seenPackets);
  Serial.print(" ch=");
  Serial.print(static_cast<uint8_t>(pkt.channel));
  Serial.print(" rssi=");
  Serial.print(pkt.rssiDbm);
  Serial.print(" type=");
  Serial.print(pduTypeName(pduType));
  Serial.print(" txAddr=");
  Serial.print(txRandom ? "random" : "public");
  Serial.print(" len=");
  Serial.print(pkt.length);

  if (pkt.length >= 6U && pkt.payload != nullptr) {
    Serial.print(" advA=");
    printAddress(pkt.payload);
  }

  Serial.print("\r\n");
}
