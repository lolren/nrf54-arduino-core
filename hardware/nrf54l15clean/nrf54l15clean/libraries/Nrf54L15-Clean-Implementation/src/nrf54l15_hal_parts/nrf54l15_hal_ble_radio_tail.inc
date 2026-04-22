bool BleRadio::configureBlePhy(uint8_t phy) {
  if (radio_ == nullptr) {
    return false;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitDisabled(120000UL);
  radio_->TASKS_SOFTRESET = RADIO_TASKS_SOFTRESET_TASKS_SOFTRESET_Trigger;

  const uint8_t selectedPhy = selectPreferredBlePhy(phy);
  const bool coded = (selectedPhy == kBlePhyCoded);
  const bool phy2M = (selectedPhy == kBlePhy2M);
  if (selectedPhy == kBlePhyNone) {
    return false;
  }

  uint32_t mode = RADIO_MODE_MODE_Ble_1Mbit;
  if (coded) {
    mode = RADIO_MODE_MODE_Ble_LR125Kbit;
  } else if (phy2M) {
    mode = RADIO_MODE_MODE_Ble_2Mbit;
  }
  radio_->MODE =
      ((mode << RADIO_MODE_MODE_Pos) & RADIO_MODE_MODE_Msk);
  radio_->TIMING = ((RADIO_TIMING_RU_Fast << RADIO_TIMING_RU_Pos) &
                    RADIO_TIMING_RU_Msk);

  uint32_t pcnf0 = 0;
  pcnf0 |= (8UL << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk;
  pcnf0 |= (1UL << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S1LEN_Pos) & RADIO_PCNF0_S1LEN_Msk;
  pcnf0 |= (RADIO_PCNF0_S1INCL_Automatic << RADIO_PCNF0_S1INCL_Pos) &
           RADIO_PCNF0_S1INCL_Msk;
  pcnf0 |= (((coded ? RADIO_PCNF0_PLEN_LongRange
                    : (phy2M ? RADIO_PCNF0_PLEN_16bit
                             : RADIO_PCNF0_PLEN_8bit)))
            << RADIO_PCNF0_PLEN_Pos) &
           RADIO_PCNF0_PLEN_Msk;
  pcnf0 |= (RADIO_PCNF0_CRCINC_Exclude << RADIO_PCNF0_CRCINC_Pos) &
           RADIO_PCNF0_CRCINC_Msk;
  if (coded) {
    pcnf0 |= (2UL << RADIO_PCNF0_CILEN_Pos) & RADIO_PCNF0_CILEN_Msk;
    pcnf0 |= (3UL << RADIO_PCNF0_TERMLEN_Pos) & RADIO_PCNF0_TERMLEN_Msk;
  }
  radio_->PCNF0 = pcnf0;

  uint32_t pcnf1 = 0;
  pcnf1 |= (255UL << RADIO_PCNF1_MAXLEN_Pos) & RADIO_PCNF1_MAXLEN_Msk;
  pcnf1 |= (0UL << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk;
  pcnf1 |= (3UL << RADIO_PCNF1_BALEN_Pos) & RADIO_PCNF1_BALEN_Msk;
  pcnf1 |= (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) &
           RADIO_PCNF1_ENDIAN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos) &
           RADIO_PCNF1_WHITEEN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEOFFSET_Include << RADIO_PCNF1_WHITEOFFSET_Pos) &
           RADIO_PCNF1_WHITEOFFSET_Msk;
  radio_->PCNF1 = pcnf1;

  radio_->BASE0 = bleAccessAddressBase(kBleAccessAddress);
  radio_->PREFIX0 = (radio_->PREFIX0 & ~RADIO_PREFIX0_AP0_Msk) |
                    ((bleAccessAddressPrefix(kBleAccessAddress)
                      << RADIO_PREFIX0_AP0_Pos) &
                     RADIO_PREFIX0_AP0_Msk);
  radio_->TXADDRESS =
      (0UL << RADIO_TXADDRESS_TXADDRESS_Pos) & RADIO_TXADDRESS_TXADDRESS_Msk;
  radio_->RXADDRESSES =
      (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos) &
      RADIO_RXADDRESSES_ADDR0_Msk;

  uint32_t crccnf = 0;
  crccnf |= (RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos) &
            RADIO_CRCCNF_LEN_Msk;
  crccnf |= (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos) &
            RADIO_CRCCNF_SKIPADDR_Msk;
  radio_->CRCCNF = crccnf;
  radio_->CRCPOLY = (kBleCrcPolynomial & RADIO_CRCPOLY_CRCPOLY_Msk);
  radio_->CRCINIT = (kBleAdvertisingCrcInit & RADIO_CRCINIT_CRCINIT_Msk);

  radio_->TIFS = 150U;
  radio_->DATAWHITE = bleDataWhiteValue(37U);
  clearRadioCoreEvents(radio_);
  return true;
}

bool BleRadio::configureBle1M() {
  connected_ = false;
  stopBackgroundAdvertising();
  stopBackgroundConnectionService();
  return configureBlePhy(kBlePhy1M);
}

bool BleRadio::waitDisabled(uint32_t spinLimit) {
  if (radio_ == nullptr) {
    return false;
  }

  // Check before clearing: when PHYEND→DISABLE→RXEN hardware shorts are active
  // (connectable ADV_IND path), DISABLE fires and EVENTS_DISABLED is set while the
  // radio immediately transitions to RXEN.  The old pattern cleared the event first,
  // then polled — creating a race where the already-set event was erased and the loop
  // spun for the full spinLimit (~30 ms at 128 MHz), then force-disabled the radio that
  // was already in RX waiting for SCAN_REQ.  Sony Xperia 10 III sends SCAN_REQ at
  // exactly T_IFS = 150 µs; missing the window left the device invisible in scanners.
  if (radio_->EVENTS_DISABLED != 0U) {
    radio_->EVENTS_DISABLED = 0U;
    return true;
  }

  // Event not yet fired — flush any residual zero-value and poll.
  radio_->EVENTS_DISABLED = 0U;
  while (spinLimit-- > 0U) {
    if (radio_->EVENTS_DISABLED != 0U) {
      radio_->EVENTS_DISABLED = 0U;
      return true;
    }
    const uint32_t state =
        (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if (state == RADIO_STATE_STATE_Disabled) {
      return true;
    }
  }
  return false;
}

bool BleRadio::waitForEnd(uint32_t spinLimit) {
  if (radio_ == nullptr) {
    return false;
  }

  while (spinLimit-- > 0U) {
    if (radio_->EVENTS_PHYEND != 0U || radio_->EVENTS_END != 0U) {
      return true;
    }
  }
  return false;
}

bool BleRadio::setAdvertisingChannel(BleAdvertisingChannel channel) {
  if (radio_ == nullptr) {
    return false;
  }

  const uint8_t freq = bleChannelToFrequency(channel);
  const uint8_t channelIndex = bleChannelToIndex(channel);

  radio_->FREQUENCY =
      ((static_cast<uint32_t>(freq) << RADIO_FREQUENCY_FREQUENCY_Pos) &
       RADIO_FREQUENCY_FREQUENCY_Msk) |
      (0UL << RADIO_FREQUENCY_MAP_Pos);

  radio_->DATAWHITE = bleDataWhiteValue(channelIndex);
  return true;
}

uint32_t BleRadio::txPowerRegFromDbm(int8_t dbm) {
  if (dbm >= 8) {
    return RADIO_TXPOWER_TXPOWER_Pos8dBm;
  }
  if (dbm >= 7) {
    return RADIO_TXPOWER_TXPOWER_Pos7dBm;
  }
  if (dbm >= 6) {
    return RADIO_TXPOWER_TXPOWER_Pos6dBm;
  }
  if (dbm >= 5) {
    return RADIO_TXPOWER_TXPOWER_Pos5dBm;
  }
  if (dbm >= 4) {
    return RADIO_TXPOWER_TXPOWER_Pos4dBm;
  }
  if (dbm >= 3) {
    return RADIO_TXPOWER_TXPOWER_Pos3dBm;
  }
  if (dbm >= 2) {
    return RADIO_TXPOWER_TXPOWER_Pos2dBm;
  }
  if (dbm >= 1) {
    return RADIO_TXPOWER_TXPOWER_Pos1dBm;
  }
  if (dbm >= 0) {
    return RADIO_TXPOWER_TXPOWER_0dBm;
  }
  if (dbm >= -1) {
    return RADIO_TXPOWER_TXPOWER_Neg1dBm;
  }
  if (dbm >= -2) {
    return RADIO_TXPOWER_TXPOWER_Neg2dBm;
  }
  if (dbm >= -3) {
    return RADIO_TXPOWER_TXPOWER_Neg3dBm;
  }
  if (dbm >= -4) {
    return RADIO_TXPOWER_TXPOWER_Neg4dBm;
  }
  if (dbm >= -5) {
    return RADIO_TXPOWER_TXPOWER_Neg5dBm;
  }
  if (dbm >= -6) {
    return RADIO_TXPOWER_TXPOWER_Neg6dBm;
  }
  if (dbm >= -7) {
    return RADIO_TXPOWER_TXPOWER_Neg7dBm;
  }
  if (dbm >= -8) {
    return RADIO_TXPOWER_TXPOWER_Neg8dBm;
  }
  if (dbm >= -9) {
    return RADIO_TXPOWER_TXPOWER_Neg9dBm;
  }
  if (dbm >= -10) {
    return RADIO_TXPOWER_TXPOWER_Neg10dBm;
  }
  if (dbm >= -12) {
    return RADIO_TXPOWER_TXPOWER_Neg12dBm;
  }
  if (dbm >= -14) {
    return RADIO_TXPOWER_TXPOWER_Neg14dBm;
  }
  if (dbm >= -16) {
    return RADIO_TXPOWER_TXPOWER_Neg16dBm;
  }
  if (dbm >= -18) {
    return RADIO_TXPOWER_TXPOWER_Neg18dBm;
  }
  if (dbm >= -20) {
    return RADIO_TXPOWER_TXPOWER_Neg20dBm;
  }
  if (dbm >= -28) {
    return RADIO_TXPOWER_TXPOWER_Neg28dBm;
  }
  if (dbm >= -40) {
    return RADIO_TXPOWER_TXPOWER_Neg40dBm;
  }
  return RADIO_TXPOWER_TXPOWER_Neg46dBm;
}

}  // namespace xiao_nrf54l15
