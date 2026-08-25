/*
  Persist.h — EEPROM adapter for Policy.h slot choice.

  Torn writes leave the backup intact. Unprogrammed 0xFF is first-boot, not
  FAULT. Magic-present + both CRCs bad → EEPROM_CORRUPT.
  REASONING.md §6, §4 F6

  License: MIT (see root LICENSE)
*/

#pragma once

#include <Arduino.h>
#include <EEPROM.h>

#include "Config.h"
#include "Policy.h"
#include "Types.h"

extern PersistentState g_state;

static_assert(sizeof(EventLogEntry) == EVENT_LOG_SLOT_BYTES,
              "event log slot size");

inline void persist_eeprom_read(uint16_t addr, uint8_t *dst, uint16_t n) {
  for (uint16_t i = 0; i < n; i++) {
    dst[i] = EEPROM.read((int)(addr + i));
  }
}

inline void persist_eeprom_write(uint16_t addr, const uint8_t *src,
                                 uint16_t n) {
  for (uint16_t i = 0; i < n; i++) {
    uint8_t v = src[i];
    if (EEPROM.read((int)(addr + i)) != v) {
      EEPROM.write((int)(addr + i), v);
    }
  }
}

enum PersistLoadResult : uint8_t {
  PERSIST_OK = 0,
  PERSIST_FIRST_BOOT = 1,
  PERSIST_CORRUPT = 2
};

inline PersistLoadResult persist_load() {
  PersistentState primary;
  PersistentState backup;
  persist_eeprom_read(PERSIST_PRIMARY_ADDR, reinterpret_cast<uint8_t *>(&primary),
                      sizeof(primary));
  persist_eeprom_read(PERSIST_BACKUP_ADDR, reinterpret_cast<uint8_t *>(&backup),
                      sizeof(backup));

  switch (persist_pick(&primary, &backup)) {
  case PERSIST_SLOT_PRIMARY:
    g_state = primary;
    return PERSIST_OK;
  case PERSIST_SLOT_BACKUP:
    g_state = backup;
    persist_eeprom_write(PERSIST_PRIMARY_ADDR,
                         reinterpret_cast<const uint8_t *>(&g_state),
                         sizeof(g_state));
    return PERSIST_OK;
  case PERSIST_SLOT_BLANK:
    persist_init_defaults(&g_state);
    return PERSIST_FIRST_BOOT;
  default:
    persist_init_defaults(&g_state);
    g_state.state = (uint8_t)SystemState::FAULT;
    g_state.fault_reason = (uint8_t)FaultReason::EEPROM_CORRUPT;
    persist_set_crc(&g_state);
    return PERSIST_CORRUPT;
  }
}

inline void persist_save() {
  persist_set_crc(&g_state);
  persist_eeprom_write(PERSIST_BACKUP_ADDR,
                       reinterpret_cast<const uint8_t *>(&g_state),
                       sizeof(g_state));
  persist_eeprom_write(PERSIST_PRIMARY_ADDR,
                       reinterpret_cast<const uint8_t *>(&g_state),
                       sizeof(g_state));
}

inline void persist_log(EventAction action, FaultReason reason,
                        uint16_t moisture, uint16_t vbat, uint32_t timestamp) {
  EventLogEntry e;
  for (uint8_t i = 0; i < sizeof(e.extra); i++) {
    e.extra[i] = 0;
  }
  e.timestamp = timestamp;
  e.action = (uint8_t)action;
  e.reason = (uint8_t)reason;
  e.moisture = moisture;
  e.vbat_mv = vbat;
  uint8_t slot = (uint8_t)(g_state.log_head % EVENT_LOG_SLOTS);
  uint16_t addr =
      (uint16_t)(EVENT_LOG_ADDR + (uint16_t)slot * EVENT_LOG_SLOT_BYTES);
  persist_eeprom_write(addr, reinterpret_cast<const uint8_t *>(&e), sizeof(e));
  g_state.log_head = persist_next_log_head(g_state.log_head);
}
