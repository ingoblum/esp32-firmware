/* esp32-firmware
 * Copyright (C) 2025 Frederic Henrichs <frederic@tinkerforge.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#pragma once
#include <cstdint>
#include <Arduino.h>

// Needed for DISPLAY_NAME_LENGTH definition
#include "../users/users.h"

#define USER_FILTER_ALL_USERS -2
#define USER_FILTER_DELETED_USERS -1

// Saved charges are saved in this folder, where each record (struct Charge) is saved in its
// own file "charge-record-%lu.bin"
#define CHARGE_RECORD_FOLDER "/charge-records"

// Dynamic history samples are recorded at most every five minutes. Shorter
// final intervals are still persisted at charge end so the graph covers the
// whole charge.
#define CHARGE_DYNAMIC_HISTORY_INTERVAL_MS (5UL * 60UL * 1000UL)

namespace charge_tracker_defs
{
    using cent=int32_t;
    using millicent=int32_t; // Der Preis in Millicent. Damit ist der maximale Preis etwa 43000€
    inline constexpr millicent PRICE_UNAVAILABLE=INT32_MAX; // Define this for an unset value;
}

// Keep the NFC tag storage local to the supplementary record format. NFC tag
// IDs are currently formatted as ten hex bytes with separators ("AA:..."),
// i.e. 29 visible characters plus the terminating zero.
#define CHARGE_SUPPLEMENTARY_TAG_ID_STRING_LENGTH 29
#define CHARGE_SUPPLEMENTARY_TAG_ID_BUFFER_LENGTH (CHARGE_SUPPLEMENTARY_TAG_ID_STRING_LENGTH + 1)

#define CHARGE_SUPPLEMENTARY_RECORD_LEGACY_SIZE sizeof(uint32_t)

struct [[gnu::packed]] ChargeStart {
    uint32_t timestamp_minutes = 0;
    float meter_start = 0.0f;
    uint8_t user_id = 0;
};

static_assert(sizeof(ChargeStart) == 9, "Unexpected size of ChargeStart");

struct [[gnu::packed]] ChargeEnd {
    uint32_t charge_duration : 24;
    float meter_end = 0.0f;
};

static_assert(sizeof(ChargeEnd) == 7, "Unexpected size of ChargeEnd");

struct [[gnu::packed]] Charge {
    ChargeStart cs;
    ChargeEnd ce;
};

// Keep this separate from Charge. The binary charge log format is intentionally
// left at 16 bytes per charge so existing logs stay readable and all repair and
// rotation logic can continue to operate on the original record size.
//
// For each charge record file this information is stored in a supplementary
// record file named "charge-record-%lu-supplementary.bin" in the same folder.
//
// The first local extension stored only the 4-byte cost_cent field. This format
// adds the NFC tag ID used to start the charge, but deliberately keeps the file
// headerless: all entries in one file have the same fixed size and setupRecords
// upgrades old 4-byte entries at boot. Do not append fields without adding a
// size-based migration; otherwise record_index based random access will silently
// point at the wrong bytes for older files.
struct [[gnu::packed]] ChargeSupplementaryRecord {
    charge_tracker_defs::cent cost = charge_tracker_defs::PRICE_UNAVAILABLE;
    uint8_t tag_type = 0;
    char tag_id[CHARGE_SUPPLEMENTARY_TAG_ID_BUFFER_LENGTH] = {};
};

static_assert(sizeof(ChargeSupplementaryRecord) == 35, "Unexpected size of ChargeSupplementaryRecord");

// One sample describes the interval ending at offset_minutes after charge
// start. power_w is the average charged power over that interval. price is
// stored in the same unit as DayAheadPrices: ct/1000 per kWh.
struct [[gnu::packed]] ChargeDynamicHistorySample {
    uint16_t offset_minutes = 0;
    uint16_t power_w = 0;
    charge_tracker_defs::millicent price_ct_per_kwh_milli = charge_tracker_defs::PRICE_UNAVAILABLE;
};

static_assert(sizeof(ChargeDynamicHistorySample) == 8, "Unexpected size of ChargeDynamicHistorySample");

struct display_name_entry {
    uint32_t length;
    uint32_t name[DISPLAY_NAME_LENGTH / sizeof(uint32_t)];
};

size_t get_display_name(uint8_t user_id, char *ret_buf, display_name_entry *display_name_cache, Language language);
String chargeRecordFilename(uint32_t i);
String chargeSupplementaryRecordFilename(uint32_t i);
String chargeDynamicHistoryFilename(uint32_t file_index, uint32_t record_index);
