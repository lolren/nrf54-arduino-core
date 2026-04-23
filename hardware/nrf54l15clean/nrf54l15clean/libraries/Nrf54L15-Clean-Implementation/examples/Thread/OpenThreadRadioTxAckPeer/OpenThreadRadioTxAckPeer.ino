#include <Arduino.h>

#include "nrf54l15_hal.h"

using xiao_nrf54l15::ZigbeeFrame;
using xiao_nrf54l15::ZigbeeMacCommandView;
using xiao_nrf54l15::ZigbeeRadio;

extern "C" __attribute__((used)) volatile uint32_t g_ot_txack_peer_results[8] = {0};

namespace {

constexpr uint8_t kChannel = 15U;
constexpr uint16_t kExpectedSourceShort = 0x1111U;
constexpr uint16_t kExpectedDestinationShort = 0x3333U;
constexpr uint8_t kDataRequestCommandId = 0x04U;

ZigbeeRadio gRadio;
uint32_t gCallbackSetCount = 0U;
uint32_t gCallbackClearCount = 0U;
bool gSawExpectedRequest = false;
uint16_t gLastSourceShort = 0U;
uint16_t gLastDestinationShort = 0U;
uint8_t gLastSequence = 0U;

bool pendingCallback(const uint8_t* psdu, uint8_t length, void*) {
  ZigbeeMacCommandView view = {};
  const bool match = ZigbeeRadio::parseMacCommandFrameShort(psdu, length, &view) &&
                     view.valid && view.commandId == kDataRequestCommandId &&
                     view.sourceShort == kExpectedSourceShort;
  if (match) {
    ++gCallbackSetCount;
    return true;
  }
  ++gCallbackClearCount;
  return false;
}

void updateResults() {
  g_ot_txack_peer_results[0] = gSawExpectedRequest ? 1U : 0U;
  g_ot_txack_peer_results[1] = gLastSourceShort;
  g_ot_txack_peer_results[2] = gLastDestinationShort;
  g_ot_txack_peer_results[3] = gLastSequence;
  g_ot_txack_peer_results[4] = gCallbackSetCount;
  g_ot_txack_peer_results[5] = gCallbackClearCount;
  g_ot_txack_peer_results[6] = 0U;
  g_ot_txack_peer_results[7] = 0U;
}

}  // namespace

void setup() {
  gRadio.begin(kChannel, 0);
  gRadio.setMacDataRequestPendingCallback(pendingCallback);
  updateResults();
}

void loop() {
  ZigbeeFrame frame = {};
  if (gRadio.receive(&frame, 1000000U, 900000UL)) {
    ZigbeeMacCommandView view = {};
    if (ZigbeeRadio::parseMacCommandFrameShort(frame.psdu, frame.length, &view) &&
        view.valid && view.commandId == kDataRequestCommandId) {
      gLastSourceShort = view.sourceShort;
      gLastDestinationShort = view.destinationShort;
      gLastSequence = view.sequence;
      if (view.sourceShort == kExpectedSourceShort &&
          view.destinationShort == kExpectedDestinationShort) {
        gSawExpectedRequest = true;
      }
    }
  }

  updateResults();
}
