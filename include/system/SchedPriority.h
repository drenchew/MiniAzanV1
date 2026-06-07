#pragma once

#include <stdint.h>

/** AUTOSAR-like priority bands — lower number = higher priority. */
namespace SchedPriority {

constexpr uint8_t P0_Emergency = 0;  /**< STOP_AZAN fast lane */
constexpr uint8_t P1_RealTime = 1;   /**< Audio play/stop */
constexpr uint8_t P2_SystemCore = 2; /**< RTC, prayer */
constexpr uint8_t P3_UserAction = 3;  /**< UI settings, navigation data */
constexpr uint8_t P4_HeavyIo = 4;   /**< SD scan, delete, BT transfer */

}  // namespace SchedPriority
