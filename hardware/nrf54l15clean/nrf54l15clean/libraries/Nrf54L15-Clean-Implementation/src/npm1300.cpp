/*
 * nPM1300 PMIC driver — TWIM23 via TwoWire.
 *
 * Uses TWIM23 (free serial fabric instance) on P1.17 (SCL) / P1.18 (SDA).
 * Wire.begin()/end() between each transaction for zero residual current.
 */
#include "npm1300.h"
#include <Arduino.h>
#include <Wire.h>

#if defined(PIN_PMIC_SDA) && defined(PIN_PMIC_SCL)
#define NPM1300_HAS_BOARD_PMIC 1
#else
#define NPM1300_HAS_BOARD_PMIC 0
#endif

namespace {

#if NPM1300_HAS_BOARD_PMIC

// TWIM23 as NRF_TWIM_Type pointer (defined in NCS, cast to address)
#define NRF_TWIM23    ((NRF_TWIM_Type *)0x500ED000UL)

static TwoWire g_pmicWire(NRF_TWIM23, PIN_PMIC_SDA, PIN_PMIC_SCL);
static bool g_busStarted = false;

bool pmic_bus_begin() {
    if (g_busStarted) return true;
    g_pmicWire.begin();
    g_pmicWire.setClock(100000);
    g_busStarted = true;
    return true;
}

void pmic_bus_end() {
    if (g_busStarted) {
        g_pmicWire.end();
        g_busStarted = false;
    }
    // These are dedicated PMIC pins. Restore their reset-state input-buffer
    // disconnect after each transaction so measurements do not add GPIO bias
    // current while the bus is idle.
    NRF_P1->DIRCLR = (1UL << 17U) | (1UL << 18U);
    NRF_P1->PIN_CNF[17U] = GPIO_PIN_CNF_INPUT_Disconnect;
    NRF_P1->PIN_CNF[18U] = GPIO_PIN_CNF_INPUT_Disconnect;
}

bool pmic_read_burst(uint8_t base, uint8_t offset, uint8_t* data, size_t len) {
    if (!pmic_bus_begin()) return false;
    bool ok = true;
    g_pmicWire.beginTransmission(NPM1300_ADDR);
    g_pmicWire.write(base);
    g_pmicWire.write(offset);
    if (g_pmicWire.endTransmission(false) != 0) { ok = false; goto exit; }
    g_pmicWire.requestFrom(NPM1300_ADDR, (int)len);
    for (size_t i = 0; i < len; i++) {
        if (g_pmicWire.available()) data[i] = g_pmicWire.read();
        else { ok = false; break; }
    }
exit:
    pmic_bus_end();
    return ok;
}

bool pmic_write_burst(uint8_t base, uint8_t offset, const uint8_t* data, size_t len) {
    if (!pmic_bus_begin()) return false;
    g_pmicWire.beginTransmission(NPM1300_ADDR);
    g_pmicWire.write(base);
    g_pmicWire.write(offset);
    for (size_t i = 0; i < len; i++) g_pmicWire.write(data[i]);
    bool ok = (g_pmicWire.endTransmission() == 0);
    pmic_bus_end();
    return ok;
}

#endif  // NPM1300_HAS_BOARD_PMIC

// ... rest of the file remains the same ...

// ─── Register offsets ──────────────────────────────────────
constexpr uint8_t kMainOffsetVersion = 0x26U;
constexpr uint8_t kVbusOffsetIlimUpdate = 0x00U;
constexpr uint8_t kVbusOffsetIlim0 = 0x01U;
constexpr uint8_t kVbusOffsetIlimStartup = 0x02U;
constexpr uint8_t kVbusOffsetStatus = 0x07U;
constexpr uint8_t kChargerOffsetErrClr = 0x00U;
constexpr uint8_t kChargerOffsetEnSet = 0x04U;
constexpr uint8_t kChargerOffsetEnClr = 0x05U;
constexpr uint8_t kChargerOffsetDisableSet = 0x06U;
constexpr uint8_t kChargerOffsetDisableClr = 0x07U;
constexpr uint8_t kChargerOffsetISet = 0x08U;
constexpr uint8_t kChargerOffsetDischargeMsb = 0x0AU;
constexpr uint8_t kChargerOffsetDischargeLsb = 0x0BU;
constexpr uint8_t kChargerOffsetVTerm = 0x0CU;
constexpr uint8_t kChargerOffsetVTermReduced = 0x0DU;
constexpr uint8_t kChargerOffsetStatus = 0x34U;
constexpr uint8_t kChargerOffsetError = 0x36U;
constexpr uint8_t kAdcOffsetTaskVbat = 0x00U;
constexpr uint8_t kAdcOffsetTaskNtc = 0x01U;
constexpr uint8_t kAdcOffsetTaskTemp = 0x02U;
constexpr uint8_t kAdcOffsetTaskVsys = 0x03U;
constexpr uint8_t kAdcOffsetTaskVbus = 0x07U;
constexpr uint8_t kAdcOffsetConfig = 0x09U;
constexpr uint8_t kAdcOffsetTaskAuto = 0x0CU;
constexpr uint8_t kAdcOffsetResults = 0x10U;
constexpr uint8_t kAdcOffsetIbatEnable = 0x24U;
constexpr uint8_t kAdcLsbVbatShift = 0U;
constexpr uint8_t kAdcLsbNtcShift = 2U;
constexpr uint8_t kAdcLsbDieShift = 4U;
constexpr uint8_t kAdcLsbVsysShift = 6U;
constexpr uint8_t kAdcLsbIbatShift = 4U;
constexpr uint8_t kAdcLsbVbusShift = 6U;
constexpr uint32_t kAdcCacheWindowMs = 500U;
constexpr uint32_t kAdcConversionTimeUs = 250U;
constexpr uint8_t kLdSwOffsetEnSet = 0x00U;
constexpr uint8_t kLdSwOffsetEnClr = 0x01U;
constexpr uint8_t kLdSwOffsetStatus = 0x04U;
constexpr uint8_t kLdSwOffsetGpiSel = 0x05U;
constexpr uint8_t kLdSwOffsetConfig = 0x07U;
constexpr uint8_t kLdSwOffsetLdoSel = 0x08U;
constexpr uint8_t kLdSwOffsetVoutSel = 0x0CU;
constexpr uint8_t kLdSw1OnMask = 0x03U;
constexpr uint8_t kLdSw2OnMask = 0x0CU;
constexpr uint8_t kBuckOffsetEnSet = 0x00U;
constexpr uint8_t kBuckOffsetEnClr = 0x01U;
constexpr uint8_t kBuckOffsetPwmSet = 0x04U;
constexpr uint8_t kBuckOffsetPwmClr = 0x05U;
constexpr uint8_t kBuckOffsetCtrl0  = 0x15U;
constexpr uint8_t kBuckOffsetVoutNorm = 0x08U;
constexpr uint8_t kBuckOffsetSwCtrl = 0x0FU;
constexpr uint8_t kBuckOffsetStatus = 0x34U;
constexpr uint8_t kBuck1OnMask = 0x04U;
constexpr uint8_t kBuck2OnMask = 0x40U;
constexpr uint8_t kGpioCount = 5U;
constexpr uint8_t kGpioOffsetMode = 0x00U;
constexpr uint8_t kGpioOffsetStatus = 0x1EU;
constexpr uint8_t kLedCount = 3U;
constexpr uint8_t kLedModeHost = 2U;
constexpr uint8_t kLedOffsetMode = 0x00U;
constexpr uint8_t kLedOffsetSet = 0x03U;
constexpr uint8_t kLedOffsetClr = 0x04U;
constexpr uint8_t kTimerOffsetLoad = 0x03U;
constexpr uint8_t kTimerOffsetTarget = 0x08U;
constexpr uint8_t kShipOffsetShip = 0x02U;
constexpr uint8_t kShipOffsetHibernate = 0x00U;
constexpr uint8_t kAdcMaxBatch = 6U;
constexpr uint16_t kRailVoltageMinMv = 1000U;
constexpr uint16_t kRailVoltageMaxMv = 3300U;
constexpr uint16_t kRailVoltageStepMv = 100U;

static constexpr uint8_t kChargerStatusChargingMask = 0x1CU;
static constexpr uint8_t kChargerEnableChargingMask = 0x01U;
static constexpr uint8_t kIbatStatusModeShift = 2U;
static constexpr uint8_t kIbatStatusModeMask = 0x03U;
static constexpr uint8_t kIbatStatusInvalidMask = 0x10U;
static constexpr uint8_t kIbatModeDischarge = 0x01U;
static constexpr uint8_t kIbatModeCharge = 0x03U;
static constexpr uint16_t kChargeCurrentMinMa = 32U;
static constexpr uint16_t kChargeCurrentMaxMa = 800U;
static constexpr uint16_t kChargeCurrentIndexMin = 16U;
static constexpr uint16_t kChargeCurrentIndexMax = 400U;
static constexpr uint16_t kVbusInputLimitMinMa = 100U;
static constexpr uint16_t kVbusInputLimitMaxMa = 1500U;
static constexpr uint16_t kVbusInputLimitStepMa = 100U;
static constexpr uint16_t kVbusChargeHeadroomMa = 100U;
static constexpr uint16_t kDischargeLimitLowMa = 200U;
static constexpr uint16_t kDischargeLimitHighMa = 1000U;
static constexpr uint16_t kDischargeLimitLowCode = 84U;
static constexpr uint16_t kDischargeLimitHighCode = 415U;
static constexpr int32_t kIbatFullScaleChargeMul = 125;
static constexpr int32_t kIbatFullScaleChargeDiv = 100;
static constexpr int32_t kIbatFullScaleDischargeMul = 112;
static constexpr int32_t kIbatFullScaleDischargeDiv = 100;
static constexpr int32_t kDieTempOffsetMilliC = 394670;
static constexpr int32_t kDieTempFactorMul = 3963000;
static constexpr int32_t kDieTempFactorDiv = 5000;

struct AdcResults {
    uint8_t ibatStat, msbVbat, msbNtc, msbDie, msbVsys,
            lsbA, ibatPre, ibatCnt, msbIbat, msbVbus, lsbB;
};

static bool g_probeValid = false;
static bool g_present = false;
static bool g_adcValid = false;
static uint32_t g_adcCachedMs = 0;
static AdcResults g_adcCache = {};
static uint8_t g_chargerStatus = 0;
static uint8_t g_chargerError = 0;
static uint8_t g_vbusStatus = 0;
static int32_t g_chargeCurrentUa = 32000;
static int32_t g_dischargeLimitUa = 1000000;
static int32_t g_vbusInputLimitUa = 100000;

static inline uint16_t adc10(uint8_t msb, uint8_t lsb, uint8_t shift) {
    return (uint16_t)(((uint16_t)msb << 2U) | ((lsb >> shift) & 0x03U));
}

// ADC full-scale voltages per nPM1300 datasheet (10-bit, 0-1023):
//   VBAT: VFSVBAT = 5.0 V
//   VSYS: VFSVSYS = 6.375 V
//   VBUS: VFSVBUS = 7.5 V
static int32_t adc_to_mv(uint16_t code, int32_t vfs_mv) {
    return ((int32_t)code * vfs_mv) / 1023;
}

static int32_t rounded_div_i64(int64_t num, int64_t den) {
    if (den <= 0) return 0;
    if (num >= 0) {
        return (int32_t)((num + (den / 2)) / den);
    }
    return (int32_t)((num - (den / 2)) / den);
}

static bool read_charger_enabled(bool* enabled) {
    if (enabled == nullptr) return false;
    uint8_t value = 0;
    if (!npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetEnSet, &value)) {
        return false;
    }
    *enabled = (value & kChargerEnableChargingMask) != 0U;
    return true;
}

static bool set_charger_enabled_task(bool enabled) {
    return npm1300_write_reg(NPM1300_BASE_CHARGER,
                             enabled ? kChargerOffsetEnSet : kChargerOffsetEnClr,
                             kChargerEnableChargingMask);
}

static bool begin_charger_config(bool* wasEnabled) {
    bool enabled = false;
    if (!read_charger_enabled(&enabled)) {
        enabled = false;
    }
    if (wasEnabled != nullptr) {
        *wasEnabled = enabled;
    }
    if (enabled && !set_charger_enabled_task(false)) {
        return false;
    }
    return true;
}

static bool end_charger_config(bool wasEnabled, bool ok) {
    if (wasEnabled) {
        ok = set_charger_enabled_task(true) && ok;
    }
    g_adcValid = false;
    return ok;
}

static uint16_t clamp_charge_current_ma(uint16_t ma) {
    if (ma < kChargeCurrentMinMa) return kChargeCurrentMinMa;
    if (ma > kChargeCurrentMaxMa) return kChargeCurrentMaxMa;
    return ma;
}

static uint16_t charge_current_index_to_ma(uint16_t idx) {
    if (idx < kChargeCurrentIndexMin) idx = kChargeCurrentIndexMin;
    if (idx > kChargeCurrentIndexMax) idx = kChargeCurrentIndexMax;
    return (uint16_t)(kChargeCurrentMinMa + ((idx - kChargeCurrentIndexMin) * 2U));
}

static bool read_charge_current_limit_ua(int32_t* outUa) {
    if (outUa == nullptr) return false;
    uint8_t raw[2] = {0, 0};
    if (!npm1300_read_burst(NPM1300_BASE_CHARGER, kChargerOffsetISet, raw, sizeof(raw))) {
        return false;
    }
    const uint16_t idx = (uint16_t)((uint16_t)raw[0] * 2U + (raw[1] & 0x01U));
    *outUa = (int32_t)charge_current_index_to_ma(idx) * 1000;
    return true;
}

static uint16_t discharge_limit_code_from_ma(uint16_t ma) {
    return (ma <= kDischargeLimitLowMa) ? kDischargeLimitLowCode : kDischargeLimitHighCode;
}

static int32_t discharge_limit_code_to_ua(uint16_t code) {
    const uint16_t midpoint = (uint16_t)((kDischargeLimitLowCode + kDischargeLimitHighCode) / 2U);
    return (code <= midpoint) ? ((int32_t)kDischargeLimitLowMa * 1000)
                              : ((int32_t)kDischargeLimitHighMa * 1000);
}

static bool read_discharge_limit_ua(int32_t* outUa) {
    if (outUa == nullptr) return false;
    uint8_t raw[2] = {0, 0};
    if (!npm1300_read_burst(NPM1300_BASE_CHARGER, kChargerOffsetDischargeMsb, raw, sizeof(raw))) {
        return false;
    }
    const uint16_t code = (uint16_t)((uint16_t)raw[0] * 2U + (raw[1] & 0x01U));
    *outUa = discharge_limit_code_to_ua(code);
    return true;
}

static int32_t ibat_to_ma(uint16_t code, uint8_t stat) {
    if ((stat & kIbatStatusInvalidMask) != 0U) {
        return -1;
    }

    const uint8_t mode = (stat >> kIbatStatusModeShift) & kIbatStatusModeMask;
    int32_t fullScaleUa = 0;
    if (mode == kIbatModeDischarge) {
        int32_t limitUa = g_dischargeLimitUa;
        if (read_discharge_limit_ua(&limitUa)) {
            g_dischargeLimitUa = limitUa;
        }
        fullScaleUa = (limitUa * kIbatFullScaleDischargeMul) /
                      kIbatFullScaleDischargeDiv;
    } else if (mode == kIbatModeCharge) {
        int32_t limitUa = g_chargeCurrentUa;
        if (read_charge_current_limit_ua(&limitUa)) {
            g_chargeCurrentUa = limitUa;
        }
        fullScaleUa = (limitUa * kIbatFullScaleChargeMul) /
                      kIbatFullScaleChargeDiv;
    } else {
        return 0;
    }

    return rounded_div_i64((int64_t)code * (int64_t)fullScaleUa, 1023LL * 1000LL);
}

static uint8_t clamp_channel(uint8_t ch) { return (ch > 1U) ? 0U : ch; }

static uint16_t charger_current_index(uint16_t ma) {
    ma = clamp_charge_current_ma(ma);
    uint16_t idx = (uint16_t)(kChargeCurrentIndexMin + ((ma - kChargeCurrentMinMa) / 2U));
    if (idx > kChargeCurrentIndexMax) idx = kChargeCurrentIndexMax;
    return idx;
}

static uint8_t vbus_input_limit_code_from_ma(uint16_t ma) {
    if (ma <= kVbusInputLimitMinMa) {
        return 1U;
    }
    if (ma >= kVbusInputLimitMaxMa) {
        return 15U;
    }

    return static_cast<uint8_t>((ma + (kVbusInputLimitStepMa - 1U)) /
                                kVbusInputLimitStepMa);
}

static uint16_t vbus_input_limit_code_to_ma(uint8_t code) {
    code &= 0x0FU;
    if (code == 0U) {
        return 500U;
    }
    if (code > 15U) {
        return kVbusInputLimitMaxMa;
    }
    return static_cast<uint16_t>(code * kVbusInputLimitStepMa);
}

static uint16_t recommended_vbus_limit_for_charge_ma(uint16_t chargeMa) {
    const uint32_t requestedMa = clamp_charge_current_ma(chargeMa);
    uint32_t limitMa = requestedMa + (requestedMa / 4U) + kVbusChargeHeadroomMa;
    if (limitMa < 200U) {
        limitMa = 200U;
    }
    if (limitMa > kVbusInputLimitMaxMa) {
        limitMa = kVbusInputLimitMaxMa;
    }
    return static_cast<uint16_t>(limitMa);
}

static uint8_t charger_vterm_index(uint16_t mv) {
    if (mv <= 3500U) return 0U;
    if (mv <= 3650U) return (uint8_t)((mv - 3500U + 25U) / 50U);
    if (mv < 4000U) return (mv < 3825U) ? 3U : 4U;
    if (mv >= 4450U) return 13U;
    return (uint8_t)(4U + ((mv - 4000U + 25U) / 50U));
}

static bool voltage_to_code(uint16_t mv, uint8_t* code) {
    if (code == nullptr || mv < kRailVoltageMinMv || mv > kRailVoltageMaxMv ||
        ((mv - kRailVoltageMinMv) % kRailVoltageStepMv) != 0U) {
        return false;
    }
    *code = static_cast<uint8_t>((mv - kRailVoltageMinMv) /
                                 kRailVoltageStepMv);
    return true;
}

static bool ensure_present() {
    uint8_t revision = 0;
    if (!g_probeValid || !g_present) {
        g_present = npm1300_read_reg(NPM1300_BASE_MAIN, kMainOffsetVersion, &revision);
        g_probeValid = true;
    }
    return g_present;
}

static void ensure_adc_active() {
    if (ensure_present()) {
        npm1300_write_reg(NPM1300_BASE_ADC, kAdcOffsetIbatEnable, 1U);
        npm1300_write_reg(NPM1300_BASE_ADC, kAdcOffsetTaskAuto, 1U);
        npm1300_write_reg(NPM1300_BASE_ADC, kAdcOffsetConfig, 0U);
    }
}

static bool read_adc_results(AdcResults* out) {
    if (!out) return false;
    const uint32_t now = millis();
    if (g_adcValid && (now - g_adcCachedMs) <= kAdcCacheWindowMs) {
        *out = g_adcCache; return true;
    }
    g_adcValid = false;
    if (!ensure_present()) return false;

    uint8_t previousIbatEnable = 0;
    uint8_t previousConfig = 0;
    if (!npm1300_read_reg(NPM1300_BASE_ADC, kAdcOffsetIbatEnable, &previousIbatEnable) ||
        !npm1300_read_reg(NPM1300_BASE_ADC, kAdcOffsetConfig, &previousConfig)) {
        return false;
    }

    bool ok = npm1300_write_reg(NPM1300_BASE_ADC, kAdcOffsetIbatEnable, 1U) &&
              npm1300_write_reg(NPM1300_BASE_ADC, kAdcOffsetTaskAuto, 1U) &&
              npm1300_write_reg(NPM1300_BASE_ADC, kAdcOffsetConfig, 0U);
    const uint8_t tasks[] = {1U, 1U, 1U, 1U};
    uint8_t chargerStatus = 0;
    uint8_t chargerError = 0;
    uint8_t vbusStatus = 0;
    uint8_t buf[11] = {};
    if (ok) {
        ok = npm1300_write_burst(NPM1300_BASE_ADC, kAdcOffsetTaskVbat, tasks, sizeof(tasks)) &&
             npm1300_write_reg(NPM1300_BASE_ADC, kAdcOffsetTaskVbus, 1U) &&
             npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetStatus, &chargerStatus) &&
             npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetError, &chargerError) &&
             npm1300_read_reg(NPM1300_BASE_VBUS, kVbusOffsetStatus, &vbusStatus);
        // A failed task transfer may still have started some conversions.
        delayMicroseconds(kAdcConversionTimeUs * 6U);
        if (ok) {
            ok = npm1300_read_burst(NPM1300_BASE_ADC, kAdcOffsetResults, buf, sizeof(buf));
        }
    }

    // Even a failed write may have reached the PMIC. Restore both settings
    // independently, including when setup, acquisition or the first restore fails.
    const bool ibatRestored = npm1300_write_reg(NPM1300_BASE_ADC,
                                               kAdcOffsetIbatEnable, previousIbatEnable);
    const bool configRestored = npm1300_write_reg(NPM1300_BASE_ADC,
                                                 kAdcOffsetConfig, previousConfig);
    if (!ok || !ibatRestored || !configRestored) return false;

    g_chargerStatus = chargerStatus;
    g_chargerError = chargerError;
    g_vbusStatus = vbusStatus;
    g_adcCache.ibatStat = buf[0];
    g_adcCache.msbVbat = buf[1];
    g_adcCache.msbNtc = buf[2];
    g_adcCache.msbDie = buf[3];
    g_adcCache.msbVsys = buf[4];
    g_adcCache.lsbA = buf[5];
    g_adcCache.msbIbat = buf[8];
    g_adcCache.msbVbus = buf[9];
    g_adcCache.lsbB = buf[10];
    g_adcValid = true; g_adcCachedMs = now;
    *out = g_adcCache;
    return true;
}

static bool buck_set_mode(uint8_t channel, uint8_t mode) {
    channel = clamp_channel(channel);
    const uint8_t pfmMask = static_cast<uint8_t>(1U << channel);
    if (mode == NPM1300_BUCK_MODE_FORCE_PWM) {
        return npm1300_update_reg(NPM1300_BASE_BUCK, kBuckOffsetCtrl0, pfmMask, 0U) &&
               npm1300_write_reg(NPM1300_BASE_BUCK, kBuckOffsetPwmSet + (channel * 2U), 1U);
    }
    if (mode == NPM1300_BUCK_MODE_FORCE_HYST) {
        return npm1300_update_reg(NPM1300_BASE_BUCK, kBuckOffsetCtrl0, pfmMask, pfmMask) &&
               npm1300_write_reg(NPM1300_BASE_BUCK, kBuckOffsetPwmClr + (channel * 2U), 1U);
    }
    return npm1300_update_reg(NPM1300_BASE_BUCK, kBuckOffsetCtrl0, pfmMask, 0U) &&
           npm1300_write_reg(NPM1300_BASE_BUCK, kBuckOffsetPwmClr + (channel * 2U), 1U);
}

static bool buck_enable(uint8_t channel, bool enable) {
    channel = clamp_channel(channel);
    return npm1300_write_reg(NPM1300_BASE_BUCK,
        static_cast<uint8_t>((enable ? kBuckOffsetEnSet : kBuckOffsetEnClr) + (channel * 2U)), 1U);
}

static bool buck_set_voltage(uint8_t channel, uint16_t mv) {
    uint8_t code = 0;
    if (!voltage_to_code(mv, &code)) return false;
    channel = clamp_channel(channel);
    return npm1300_write_reg(NPM1300_BASE_BUCK, kBuckOffsetVoutNorm + (channel * 2U), code) &&
           npm1300_write_reg(NPM1300_BASE_BUCK, kBuckOffsetSwCtrl, 1U << channel);
}

static bool buck_is_enabled(uint8_t channel) {
    uint8_t status = 0;
    if (!npm1300_read_reg(NPM1300_BASE_BUCK, kBuckOffsetStatus, &status)) return false;
    return (status & (channel == 0U ? kBuck1OnMask : kBuck2OnMask)) != 0U;
}

}  // namespace

// ─── Public API ────────────────────────────────────────────

bool npm1300_read_reg(uint8_t base, uint8_t offset, uint8_t* value) {
#if NPM1300_HAS_BOARD_PMIC
    return pmic_read_burst(base, offset, value, 1U);
#else
    (void)base; (void)offset; (void)value; return false;
#endif
}

bool npm1300_write_reg(uint8_t base, uint8_t offset, uint8_t value) {
#if NPM1300_HAS_BOARD_PMIC
    return pmic_write_burst(base, offset, &value, 1U);
#else
    (void)base; (void)offset; (void)value; return false;
#endif
}

bool npm1300_read_burst(uint8_t base, uint8_t offset, uint8_t* data, size_t len) {
#if NPM1300_HAS_BOARD_PMIC
    if (!data || !len || len > 32) return false;
    return pmic_read_burst(base, offset, data, len);
#else
    (void)base; (void)offset; (void)data; (void)len; return false;
#endif
}

bool npm1300_write_burst(uint8_t base, uint8_t offset, const uint8_t* data, size_t len) {
#if NPM1300_HAS_BOARD_PMIC
    if (!data || !len || len > 32) return false;
    return pmic_write_burst(base, offset, data, len);
#else
    (void)base; (void)offset; (void)data; (void)len; return false;
#endif
}

bool npm1300_update_reg(uint8_t base, uint8_t offset, uint8_t mask, uint8_t value) {
    uint8_t current = 0;
    if (!npm1300_read_reg(base, offset, &current)) return false;
    return npm1300_write_reg(base, offset, (current & ~mask) | (value & mask));
}

void npm1300_begin(void) { ensure_adc_active(); }
void npm1300_init(void) { npm1300_begin(); }
bool npm1300_is_present(void) { return ensure_present(); }

bool npm1300_ldo1_enable(bool enable) {
    return npm1300_write_reg(NPM1300_BASE_LDSW, enable ? kLdSwOffsetEnSet : kLdSwOffsetEnClr, 1U);
}
bool npm1300_ldo1_set_voltage(uint16_t mv) {
    uint8_t code; if (!voltage_to_code(mv, &code)) return false;
    return npm1300_write_reg(NPM1300_BASE_LDSW, kLdSwOffsetVoutSel, code);
}
bool npm1300_ldo1_is_enabled(void) {
    uint8_t s; return npm1300_read_reg(NPM1300_BASE_LDSW, kLdSwOffsetStatus, &s) &&
                      ((s & kLdSw1OnMask) != 0U);
}
bool npm1300_ldo2_enable(bool enable) {
    return npm1300_write_reg(NPM1300_BASE_LDSW,
                             static_cast<uint8_t>((enable ? kLdSwOffsetEnSet : kLdSwOffsetEnClr) + 2U),
                             1U);
}
bool npm1300_ldo2_set_voltage(uint16_t mv) {
    uint8_t code; if (!voltage_to_code(mv, &code)) return false;
    return npm1300_write_reg(NPM1300_BASE_LDSW, kLdSwOffsetVoutSel + 1U, code);
}
bool npm1300_ldo2_is_enabled(void) {
    uint8_t s; return npm1300_read_reg(NPM1300_BASE_LDSW, kLdSwOffsetStatus, &s) &&
                      ((s & kLdSw2OnMask) != 0U);
}
bool npm1300_ldo1_set_mode(uint8_t mode) {
    if (mode > NPM1300_LDSW_MODE_LDO) return false;
    return npm1300_write_reg(NPM1300_BASE_LDSW, kLdSwOffsetLdoSel,
                             mode == NPM1300_LDSW_MODE_LDO ? 1U : 0U);
}
bool npm1300_ldo2_set_mode(uint8_t mode) {
    if (mode > NPM1300_LDSW_MODE_LDO) return false;
    return npm1300_write_reg(NPM1300_BASE_LDSW, kLdSwOffsetLdoSel + 1U,
                             mode == NPM1300_LDSW_MODE_LDO ? 1U : 0U);
}
bool npm1300_imu_mic_power_enable(bool enable) {
    if (!enable) return npm1300_ldo1_enable(false);
    return npm1300_ldo1_set_mode(NPM1300_LDSW_MODE_LDO) &&
           npm1300_ldo1_set_voltage(NPM1300_LDO_VOLTAGE_3V3) &&
           npm1300_ldo1_enable(true);
}
bool npm1300_sensor_power_enable(bool enable) {
    return npm1300_imu_mic_power_enable(enable);
}
bool npm1300_prepare_for_sleep(void) {
    g_adcValid = false;
    return npm1300_write_reg(NPM1300_BASE_ADC, kAdcOffsetIbatEnable, 0U);
}

bool npm1300_buck1_enable(bool e) { return buck_enable(0, e); }
bool npm1300_buck1_set_voltage(uint16_t mv) { return buck_set_voltage(0, mv); }
bool npm1300_buck1_is_enabled(void) { return buck_is_enabled(0); }
bool npm1300_buck2_enable(bool e) {
#if defined(NPM1300_ENABLE_UNSAFE_SYSTEM_RAIL_CONTROL)
    return buck_enable(1, e);
#else
    // BUCK2 is VSYS_3V3. Disabling it also removes the SAMD11 upload path.
    if (!e) return false;
    return buck_enable(1, true);
#endif
}
bool npm1300_buck2_set_voltage(uint16_t mv) {
#if defined(NPM1300_ENABLE_UNSAFE_SYSTEM_RAIL_CONTROL)
    return buck_set_voltage(1, mv);
#else
    // Even a valid nPM1300 voltage is unsafe for this board's fixed rail.
    (void)mv;
    return false;
#endif
}
bool npm1300_system_buck_is_enabled(void) { return buck_is_enabled(1); }
bool npm1300_system_buck_set_mode(uint8_t m) {
    if (m > NPM1300_BUCK_MODE_FORCE_HYST) return false;
    return buck_set_mode(1, m);
}
bool npm1300_buck2_is_enabled(void) { return npm1300_system_buck_is_enabled(); }
bool npm1300_buck1_set_mode(uint8_t m) { return buck_set_mode(0, m); }
bool npm1300_buck2_set_mode(uint8_t m) { return npm1300_system_buck_set_mode(m); }

bool npm1300_charger_enable(bool e) {
    if (e) {
        (void)npm1300_write_reg(NPM1300_BASE_CHARGER, kChargerOffsetErrClr, 1U);
        (void)npm1300_write_reg(NPM1300_BASE_CHARGER, kChargerOffsetDisableClr, 1U);
    }
    g_adcValid = false;
    return set_charger_enabled_task(e);
}
bool npm1300_charger_set_current(uint16_t ma) {
    const uint16_t idx = charger_current_index(ma);
    const uint16_t actualMa = charge_current_index_to_ma(idx);
    const uint8_t data[] = {
        static_cast<uint8_t>(idx / 2U),
        static_cast<uint8_t>(idx & 1U)
    };

    if (!npm1300_vbus_set_input_current_limit_ma(
            recommended_vbus_limit_for_charge_ma(actualMa))) {
        return false;
    }

    bool wasEnabled = false;
    if (!begin_charger_config(&wasEnabled)) {
        return false;
    }
    const bool ok = npm1300_write_burst(NPM1300_BASE_CHARGER, kChargerOffsetISet,
                                        data, sizeof(data));
    g_chargeCurrentUa = static_cast<int32_t>(actualMa) * 1000;
    return end_charger_config(wasEnabled, ok);
}
bool npm1300_charger_set_term_voltage(uint16_t mv) {
    const uint8_t idx = charger_vterm_index(mv);
    bool wasEnabled = false;
    if (!begin_charger_config(&wasEnabled)) {
        return false;
    }
    const bool ok = npm1300_write_reg(NPM1300_BASE_CHARGER, kChargerOffsetVTerm, idx) &&
                    npm1300_write_reg(NPM1300_BASE_CHARGER, kChargerOffsetVTermReduced, idx);
    return end_charger_config(wasEnabled, ok);
}
bool npm1300_charger_is_charging(void) {
    uint8_t s; return npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetStatus, &s) &&
                      ((s & kChargerStatusChargingMask) != 0U);
}
bool npm1300_charger_status(uint8_t* s) { return npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetStatus, s); }
bool npm1300_charger_error(uint8_t* e) { return npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetError, e); }
bool npm1300_vbus_status(uint8_t* s) { return npm1300_read_reg(NPM1300_BASE_VBUS, kVbusOffsetStatus, s); }

static bool npm1300_vbus_absent_for_ship_hibernate(void) {
    uint8_t status = 0U;
    return npm1300_vbus_status(&status) &&
           ((status & NPM1300_VBUS_STATUS_PRESENT) == 0U);
}

bool npm1300_vbus_set_input_current_limit_ma(uint16_t ma) {
    const uint8_t code = vbus_input_limit_code_from_ma(ma);
    const uint16_t actualMa = vbus_input_limit_code_to_ma(code);

    const bool ok = npm1300_write_reg(NPM1300_BASE_VBUS, kVbusOffsetIlim0, code) &&
                    npm1300_write_reg(NPM1300_BASE_VBUS, kVbusOffsetIlimStartup, code) &&
                    npm1300_write_reg(NPM1300_BASE_VBUS, kVbusOffsetIlimUpdate, 1U);
    if (ok) {
        g_vbusInputLimitUa = static_cast<int32_t>(actualMa) * 1000;
    }
    return ok;
}

uint16_t npm1300_vbus_get_input_current_limit_ma(void) {
    uint8_t code = 0U;
    if (!npm1300_read_reg(NPM1300_BASE_VBUS, kVbusOffsetIlim0, &code)) {
        return static_cast<uint16_t>(g_vbusInputLimitUa / 1000);
    }
    const uint16_t actualMa = vbus_input_limit_code_to_ma(code);
    g_vbusInputLimitUa = static_cast<int32_t>(actualMa) * 1000;
    return actualMa;
}

int32_t npm1300_read_vbat_mv(void) {
    AdcResults r{}; if (!read_adc_results(&r)) return -1;
    return adc_to_mv(adc10(r.msbVbat, r.lsbA, kAdcLsbVbatShift), 5000);
}
int32_t npm1300_read_temp_mc(void) {
    AdcResults r{}; if (!read_adc_results(&r)) return -1;
    uint16_t code = adc10(r.msbDie, r.lsbA, kAdcLsbDieShift);
    return kDieTempOffsetMilliC - ((int32_t)code * kDieTempFactorMul) / kDieTempFactorDiv;
}
int32_t npm1300_read_ibat_ma(void) {
    AdcResults r{}; if (!read_adc_results(&r)) return -1;
    return ibat_to_ma(adc10(r.msbIbat, r.lsbB, kAdcLsbIbatShift), r.ibatStat);
}
int32_t npm1300_read_vsys_mv(void) {
    AdcResults r{}; if (!read_adc_results(&r)) return -1;
    return adc_to_mv(adc10(r.msbVsys, r.lsbA, kAdcLsbVsysShift), 6375);
}
int32_t npm1300_read_vbus_mv(void) {
    AdcResults r{}; if (!read_adc_results(&r)) return -1;
    return adc_to_mv(adc10(r.msbVbus, r.lsbB, kAdcLsbVbusShift), 7500);
}

bool npm1300_enter_ship_mode(void) {
    if (!npm1300_vbus_absent_for_ship_hibernate()) {
        return false;
    }
    return npm1300_write_reg(NPM1300_BASE_SHIP, kShipOffsetShip, 1U);
}
bool npm1300_enter_hibernate(void) {
    if (!npm1300_vbus_absent_for_ship_hibernate()) {
        return false;
    }
    return npm1300_write_reg(NPM1300_BASE_SHIP, kShipOffsetHibernate, 1U);
}

bool npm1300_configure_hibernate_timer_ms(uint32_t delay_ms) {
    if (delay_ms == 0UL || delay_ms > NPM1300_HIBERNATE_TIMER_MAX_MS) {
        return false;
    }

    const uint32_t ticks =
        (delay_ms + (NPM1300_HIBERNATE_TIMER_PRESCALE_MS - 1UL)) /
        NPM1300_HIBERNATE_TIMER_PRESCALE_MS;
    if (ticks == 0UL || ticks > 0xFFFFFFUL) {
        return false;
    }

    const uint8_t target[] = {
        static_cast<uint8_t>((ticks >> 16U) & 0xFFU),
        static_cast<uint8_t>((ticks >> 8U) & 0xFFU),
        static_cast<uint8_t>(ticks & 0xFFU),
    };

    return npm1300_write_burst(NPM1300_BASE_TIMER, kTimerOffsetTarget,
                               target, sizeof(target)) &&
           npm1300_write_reg(NPM1300_BASE_TIMER, kTimerOffsetLoad, 1U);
}

bool npm1300_enter_timed_hibernate_ms(uint32_t delay_ms) {
    if (delay_ms == 0UL || delay_ms > NPM1300_HIBERNATE_TIMER_MAX_MS) {
        return false;
    }

    uint8_t vbusStatus = 0U;
    if (!npm1300_vbus_status(&vbusStatus)) {
        return false;
    }

    if ((vbusStatus & NPM1300_VBUS_STATUS_PRESENT) != 0U) {
        // nPM1300 Hibernate requires VBUS to be absent, and VBUS is itself a
        // PMIC wake source. Use the SoC's timed System OFF cold-reset path
        // while USB is powering the board.
        systemOffWakeReset(delay_ms);
    }

    if (!npm1300_configure_hibernate_timer_ms(delay_ms)) {
        return false;
    }

    // Nordic's nPM13xx driver waits after TIMERTARGETSTROBE so the PMIC
    // latches the wake-up timer before TASKENTERHIBERNATE is written.
    delay(1);
    return npm1300_enter_hibernate();
}

bool npm1300_enter_hibernate_after_ms(uint32_t delay_ms) {
    return npm1300_enter_timed_hibernate_ms(delay_ms);
}

bool npm1300_led_set(uint8_t led, uint8_t brightness) {
    if (led >= kLedCount) return false;
    return npm1300_write_reg(NPM1300_BASE_LED, kLedOffsetMode + led, kLedModeHost) &&
           npm1300_write_reg(NPM1300_BASE_LED,
                             static_cast<uint8_t>((brightness ? kLedOffsetSet : kLedOffsetClr) + (led * 2U)),
                             1U);
}
bool npm1300_gpio_set_mode(uint8_t pin, uint8_t mode) {
    if (pin >= kGpioCount || mode > 9) return false;
    return npm1300_write_reg(NPM1300_BASE_GPIO, kGpioOffsetMode + pin, mode);
}
bool npm1300_gpio_write(uint8_t pin, bool high) {
    return npm1300_gpio_set_mode(pin, high ? NPM1300_GPIO_GPOLOGIC1
                                           : NPM1300_GPIO_GPOLOGIC0);
}
bool npm1300_gpio_status(uint8_t* status) {
    return npm1300_read_reg(NPM1300_BASE_GPIO, kGpioOffsetStatus, status);
}

bool npm1300_charger_set_discharge_current_ma(uint16_t ma) {
    const uint16_t code = discharge_limit_code_from_ma(ma);
    const uint8_t data[] = {
        static_cast<uint8_t>(code / 2U),
        static_cast<uint8_t>(code & 1U)
    };

    bool wasEnabled = false;
    if (!begin_charger_config(&wasEnabled)) {
        return false;
    }
    const bool ok = npm1300_write_burst(NPM1300_BASE_CHARGER,
                                        kChargerOffsetDischargeMsb,
                                        data, sizeof(data));
    g_dischargeLimitUa = discharge_limit_code_to_ua(code);
    return end_charger_config(wasEnabled, ok);
}

bool npm1300_is_crc_corrupt(void) {
    return g_chargerError & 0x04U;
}
