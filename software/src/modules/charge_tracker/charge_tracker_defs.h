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

// Dynamic costs are stored in a separate sidecar file per charge-record file.
// This sentinel marks charges for which no dynamic-price calculation exists,
// for example old records that were written before this feature was available.
#define CHARGE_DYNAMIC_COST_UNAVAILABLE UINT32_MAX

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
// For each charge this information is stored in a file named "charge-record-%lu-cost.bin"
// in the same folder as the charge record.
struct [[gnu::packed]] ChargeDynamicCost {
    uint32_t cost_cent = CHARGE_DYNAMIC_COST_UNAVAILABLE;
};

static_assert(sizeof(ChargeDynamicCost) == 4, "Unexpected size of ChargeDynamicCost");

struct display_name_entry {
    uint32_t length;
    uint32_t name[DISPLAY_NAME_LENGTH / sizeof(uint32_t)];
};

size_t get_display_name(uint8_t user_id, char *ret_buf, display_name_entry *display_name_cache, Language language);
String chargeRecordFilename(uint32_t i);
String chargeDynamicCostFilename(uint32_t i);
