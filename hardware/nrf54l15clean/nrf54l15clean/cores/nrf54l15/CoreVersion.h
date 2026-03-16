/*
 * Public core-version macros for sketches.
 *
 * The numeric and string values come from CoreVersionGenerated.h, which the
 * build system regenerates from platform.txt before each compile. That keeps
 * the sketch-visible version in sync with the board package version without
 * requiring manual header edits on each release.
 */

#ifndef NRF54L15_CLEAN_CORE_VERSION_H
#define NRF54L15_CLEAN_CORE_VERSION_H

#ifndef ARDUINO_NRF54L15_CLEAN_VERSION_STRING
#error "CoreVersionGenerated.h was not force-included by the build system"
#endif

#define NRF54L15_CLEAN_CORE_VERSION_MAJOR ARDUINO_NRF54L15_CLEAN_VERSION_MAJOR
#define NRF54L15_CLEAN_CORE_VERSION_MINOR ARDUINO_NRF54L15_CLEAN_VERSION_MINOR
#define NRF54L15_CLEAN_CORE_VERSION_PATCH ARDUINO_NRF54L15_CLEAN_VERSION_PATCH
#define NRF54L15_CLEAN_CORE_VERSION ARDUINO_NRF54L15_CLEAN_VERSION
#define NRF54L15_CLEAN_CORE_VERSION_STRING ARDUINO_NRF54L15_CLEAN_VERSION_STRING

#ifdef __cplusplus
namespace arduino {
namespace nrf54l15clean {
inline constexpr int kCoreVersionMajor = ARDUINO_NRF54L15_CLEAN_VERSION_MAJOR;
inline constexpr int kCoreVersionMinor = ARDUINO_NRF54L15_CLEAN_VERSION_MINOR;
inline constexpr int kCoreVersionPatch = ARDUINO_NRF54L15_CLEAN_VERSION_PATCH;
inline constexpr unsigned long kCoreVersion = ARDUINO_NRF54L15_CLEAN_VERSION;
inline constexpr const char kCoreVersionString[] = ARDUINO_NRF54L15_CLEAN_VERSION_STRING;
}  // namespace nrf54l15clean
}  // namespace arduino
#endif

#endif  // NRF54L15_CLEAN_CORE_VERSION_H
