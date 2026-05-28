/* esp32-firmware
 * Copyright (C) 2020-2021 Erik Fleckstein <erik@tinkerforge.com>
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
#include "users.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <LittleFS.h>

#include "event_log_prefix.h"
#include "generated/module_dependencies.h"
#include "tools.h"
#include "digest_auth.h"

#define USERNAME_FILE "/users/all_usernames"

// We have to do access the evse/evse_v2 configs manually
// because a lot of the code runs in setup(), i.e. before APIs
// are registered.
void set_data_storage(uint8_t *buf)
{
    evse_common.set_data_storage(DATA_STORE_PAGE_CHARGE_TRACKER, buf);
}

void get_data_storage(uint8_t *buf)
{
    evse_common.get_data_storage(DATA_STORE_PAGE_CHARGE_TRACKER, buf);
}

void zero_user_slot_info()
{
    uint8_t buf[63] = {0};
    set_data_storage(buf);
}

uint8_t get_charger_state()
{
    return evse_common.get_state().get("charger_state")->asUint();
}

Config *get_user_slot()
{
    return (Config *)evse_common.get_slots().get(CHARGING_SLOT_USER);
}

float get_energy()
{
    float energy = NAN;
    evse_common.get_charger_meter_energy(&energy);
    return energy;
}

#define USER_SLOT_INFO_VERSION 1
struct UserSlotInfo {
    uint16_t checksum;
    uint8_t version;
    uint8_t user_id;
    uint32_t evse_uptime_on_start;
    uint32_t timestamp_minutes;
    float meter_start;
};

uint16_t calc_checksum(const UserSlotInfo &info)
{
    uint32_t float_buf = 0;
    memcpy(&float_buf, &info.meter_start, sizeof(float_buf));

    uint32_t checksum = info.checksum
                      + ((((uint32_t)info.version) << 8) | info.user_id)
                      + (info.evse_uptime_on_start >> 16)
                      // TODO: Change low-word masks from 0xFF to 0xFFFF to include all 16 low bits
                      // in the checksum sum. The current 8-bit mask weakens corruption detection.
                      // unfortunately this change will alter the checksum for already written entries,
                      // which requires either a new version for the UserSlotInfo or some other means
                      // of doing an upgrade without invalidating old data.
                      + (info.evse_uptime_on_start & 0xFF)
                      + (info.timestamp_minutes >> 16)
                      + (info.timestamp_minutes & 0xFF)
                      + (float_buf >> 16)
                      + (float_buf & 0xFF);

    uint32_t carry = checksum >> 16;
    checksum = (checksum & 0xFFFF) + carry;
    checksum = ~checksum;
    return checksum;
}

void write_user_slot_info(uint8_t user_id, uint32_t evse_uptime, uint32_t timestamp_minutes, float meter_start)
{
    UserSlotInfo info;
    info.checksum = 0;
    info.version = USER_SLOT_INFO_VERSION;
    info.user_id = user_id;
    info.evse_uptime_on_start = evse_uptime;
    info.timestamp_minutes = timestamp_minutes;
    info.meter_start = meter_start;

    info.checksum = calc_checksum(info);

    uint8_t buf[63] = {0};
    memcpy(buf, &info, sizeof(info));
    set_data_storage(buf);
}

bool read_user_slot_info(UserSlotInfo *result)
{
    uint8_t buf[63] = {0};
    get_data_storage(buf);

    // zero_user_slot_info() explicitly clears the EVSE storage page to all zeros.
    // This is the normal "no persisted charge session" state after boot/stop.
    if (std::all_of(buf, buf + sizeof(buf), [](uint8_t byte) { return byte == 0; })) {
        // An empty slot means there is no user/session metadata to restore,
        // not that the stored data is corrupted.
        logger.printfln("User slot info empty (all zeros). No active user slot data to restore.");
        return false;
    }

    memcpy(result, buf, sizeof(UserSlotInfo));
    uint16_t calc = calc_checksum(*result);
    if (calc != 0) {
        logger.printfln("Checksum mismatch! Calculated sum error: 0x%04X. Stored data: CS=0x%04X, V=%u, UID=%u, Uptime=%lu, TS=%lu, Meter=%f",
            static_cast<unsigned int>(calc),
            static_cast<unsigned int>(result->checksum),
            static_cast<unsigned int>(result->version),
            static_cast<unsigned int>(result->user_id),
            result->evse_uptime_on_start,
            result->timestamp_minutes,
            result->meter_start);
        return false;
    }

    if (result->version != USER_SLOT_INFO_VERSION)
        logger.printfln("Version mismatch!");

    return result->version == USER_SLOT_INFO_VERSION;
}

void Users::pre_setup()
{
    config_users_prototype = Config::Object({
        {"id", Config::Uint8(0)},
        {"roles", Config::Uint32(0)},
        {"current", Config::Uint16(32000)},
        {"display_name", Config::Str("", 0, USERNAME_LENGTH)},
        {"username", Config::Str("", 0, USERNAME_LENGTH)},
        {"digest_hash", Config::Str("", 0, 32)},
    });

    config = Config::Object({
        {"users", Config::Array(
            {
                Config::Object({
                    {"id", Config::Uint8(0)},
                    {"roles", Config::Uint32(0xFFFFFFFF)},
                    {"current", Config::Uint16(32000)},
                    {"display_name", Config::Str("Anonymous", 0, USERNAME_LENGTH)},
                    {"username", Config::Str("anonymous", 0, USERNAME_LENGTH)},
                    {"digest_hash", Config::Str("", 0, 32)}
                })
            },
            &config_users_prototype,
            1, MAX_ACTIVE_USERS,
            Config::type_id<Config::ConfObject>()
        )},
        {"next_user_id", Config::Uint8(0)},
        {"http_auth_enabled", Config::Bool(false)}
    });

    add = ConfigRoot{Config::Object({
        {"id", Config::Uint8(0)},
        {"roles", Config::Uint32(0)},
        {"current", Config::Uint(32000, 0, 32000)},
        {"display_name", Config::Str("", 0, USERNAME_LENGTH)},
        {"username", Config::Str("", 0, USERNAME_LENGTH)},
        {"digest_hash", Config::Str("", 0, 32)},
    }), [this](Config &add, ConfigSource source) -> String {
        if (config.get("next_user_id")->asUint() == 0)
            return "Can't add user. All user IDs in use.";

        if (add.get("id")->asUint() != config.get("next_user_id")->asUint())
            return "Can't add user. Wrong next user ID";

        if (config.get("users")->count() == MAX_ACTIVE_USERS)
            return "Can't add user. Already have the maximum number of active users.";

        for (size_t i = 0; i < config.get("users")->count(); ++i)
            if (config.get("users")->get(i)->get("username")->asString() == add.get("username")->asString())
                return "Can't add user. A user with this username already exists.";

        {
            char username[33] = {0};
            File f = LittleFS.open(USERNAME_FILE, "r");
            for (size_t i = 0; i < f.size(); i += USERNAME_ENTRY_LENGTH) {
                f.seek(i);
                f.read((uint8_t *) username, USERNAME_LENGTH);
                if (add.get("username")->asString() == username)
                    return "Can't add user. A user with this username already has tracked charges.";
            }
        }

        return "";
    }};
    add.set_permit_null_updates(false);

    modify = ConfigRoot{Config::Object({
        {"id", Config::Uint(256, 0, 256)}, // 256 is used as marker value that the ID was not written
        {"roles", Config::Uint32(0)},
        {"current", Config::Uint(32000 + 1, 0, 32000 + 1)}, // 32000 + 1 is also a marker value
        {"display_name", Config::Str("___MARKER___VALUE___", 0, USERNAME_LENGTH)},
        {"username", Config::Str("___MARKER___VALUE___", 0, USERNAME_LENGTH)},
        {"digest_hash", Config::Str("___MARKER___VALUE___", 0, 32)},
    }), [this](Config &modify, ConfigSource source) -> String {
        auto id = modify.get("id")->asUint();
        auto roles = modify.get("roles")->asUint();
        auto current = modify.get("current")->asUint();
        const String &display_name = modify.get("display_name")->asString();
        const String &username = modify.get("username")->asString();
        const String &digest_hash = modify.get("digest_hash")->asString();

        bool id_passed = id < 256 ;
        bool roles_passed = roles != 0;
        bool current_passed = current < 32000 + 1;
        bool display_name_passed = display_name != "___MARKER___VALUE___";
        bool username_passed = username != "___MARKER___VALUE___";
        bool digest_hash_passed = digest_hash != "___MARKER___VALUE___";

        if (!id_passed)
            return "Can't modify user. User ID is null or missing.";

        // Only allow modification of display name for anonymous.

        if (id == 0) {
            if (username_passed)
                return "Can't modify anonymous user. Username needs to be null or missing.";
            if (digest_hash_passed)
                return "Can't modify anonymous user. Digest_hash needs to be null or missing.";
            if (roles_passed)
                return "Can't modify anonymous user. Roles need to be null or missing.";
            if (current_passed)
                return "Can't modify anonymous user. Current need to be null or missing.";
        }

        Config *user = nullptr;
        for (size_t i = 0; i < config.get("users")->count(); ++i) {
            if (config.get("users")->get(i)->get("id")->asUint() == id) {
                user = (Config *)config.get("users")->get(i);
                break;
            }
        }

        if (user == nullptr) {
            return "Can't modify user. User with this ID not found.";
        }

        if (username_passed
            && !digest_hash_passed
            && user->get("username")->asString() != username
            && !user->get("digest_hash")->asString().isEmpty()) {
            return "Changing the username without updating the digest hash is not allowed!";
        }

        for (size_t i = 0; i < config.get("users")->count(); ++i) {
            if (config.get("users")->get(i)->get("id")->asUint() == id)
                continue;

            if (config.get("users")->get(i)->get("username")->asString() == username) {
                return "Can't modify user. Another user with the same username already exists.";
            }
        }

        {
            char other_name[33] = {0};
            File f = LittleFS.open(USERNAME_FILE, "r");
            for(size_t i = 0; i < f.size(); i += USERNAME_ENTRY_LENGTH) {
                if ((i / USERNAME_ENTRY_LENGTH) == id)
                    continue;

                f.seek(i);
                f.read((uint8_t *) other_name, USERNAME_LENGTH);
                if (username == other_name)
                    return "Can't modify user. A user with this username already has tracked charges.";
            }
        }

        if (!roles_passed)
            modify.get("roles")->updateUint(user->get("roles")->asUint());
        if (!current_passed)
            modify.get("current")->updateUint(user->get("current")->asUint());
        if (!display_name_passed)
            modify.get("display_name")->updateString(user->get("display_name")->asString());
        if (!username_passed)
            modify.get("username")->updateString(user->get("username")->asString());
        if (!digest_hash_passed)
            modify.get("digest_hash")->updateString(user->get("digest_hash")->asString());

        return "";
    }};

    remove = ConfigRoot{Config::Object({
        {"id", Config::Uint8(0)}
    }), [this](Config &remove, ConfigSource source) -> String {
        if (remove.get("id")->asUint() == 0)
            return "The anonymous user can't be removed.";

        for (size_t i = 0; i < config.get("users")->count(); ++i) {
            if (config.get("users")->get(i)->get("id")->asUint() == remove.get("id")->asUint()) {
                return "";
            }
        }

        return "Can't remove user. User with this ID not found.";
    }};

    http_auth_update = ConfigRoot{Config::Object({
        {"enabled", Config::Bool(false)}
    }), [this](Config &update, ConfigSource source) -> String {
        if (!update.get("enabled")->asBool())
            return "";

        for (size_t i = 0; i < config.get("users")->count(); ++i) {
            if (!config.get("users")->get(i)->get("digest_hash")->asString().isEmpty())
                return "";
        }

        return "Can't enable HTTP authentication if not at least one user with a password is configured!";
    }};

    start_charging_cmd = ConfigRoot{Config::Object({
        // 256 is an out-of-range marker that lets us detect "missing id"
        // when callers choose username-only commands.
        {"id", Config::Uint(256, 0, 256)},
        {"username", Config::Str("", 0, USERNAME_LENGTH)}
    })};

    stop_charging_cmd = ConfigRoot{Config::Object({
        {"id", Config::Uint(256, 0, 256)},
        {"username", Config::Str("", 0, USERNAME_LENGTH)}
    })};

    // By default, the config system (via force_same_keys = true) requires all keys 
    // of an object to be present in the JSON payload. For charging commands via MQTT/API, 
    // we want to allow partial updates (e.g. only id or only username, or even empty {}).
    start_charging_cmd.set_force_same_keys(false);
    stop_charging_cmd.set_force_same_keys(false);
}

void create_username_file()
{
    logger.printfln("Recreating users file");
    File f = LittleFS.open(USERNAME_FILE, "w", true);
    const uint8_t buf[512] = {};

    for (int i = 0; i < MAX_PASSIVE_USERS * USERNAME_ENTRY_LENGTH; i += sizeof(buf))
        f.write(buf, sizeof(buf));
}

void Users::setup()
{
    api.restorePersistentConfig("users/config", &config);

    if (!LittleFS.exists(USERNAME_FILE)) {
        logger.printfln("Username list for tracked charges does not exist! Recreating now.");
        create_username_file();
        for (size_t i = 0; i < config.get("users")->count(); ++i) {
            Config *user = (Config *)config.get("users")->get(i);
            this->rename_user(user->get("id")->asUint(), user->get("username")->asString(), user->get("display_name")->asString());
        }
    }

    // Next user id is 0 if there is no free user left.
    // After a reboot maybe tracked charges were removed.
    if (config.get("next_user_id")->asUint() == 0)
        search_next_free_user();

    Config *user_slot = get_user_slot();
    bool charge_start_tracked = charge_tracker.currentlyCharging();
    const uint8_t charger_state = get_charger_state();
    // Inclusion of READY_TO_CHARGE ensures that an authorized session that hasn't 
    // technically started "Charging" yet is not prematurely aborted during a reboot.
    const bool charging = charger_state == CHARGER_STATE_READY_TO_CHARGE || charger_state == CHARGER_STATE_CHARGING;
    // Legacy behavior also treated "active user slot with max_current == 32000"
    // as an implicit "charging" signal during setup. This could revive stale
    // charge tracker state after reboot even when no real charging session was
    // running. Keep this condition only for diagnostics.
    const bool legacy_slot_implies_charging = user_slot->get("active")->asBool() && user_slot->get("max_current")->asUint() == 32000;
    if (!charging && legacy_slot_implies_charging) {
        logger.printfln("Setup: legacy charging heuristic matched (active user slot + 32000 mA). Old firmware would continue an existing charge record.");
    }

    if (charge_start_tracked && !charging) {
        float override_value = get_energy();
        // The energy value can be NaN if the meter is not readable yet.
        // This will be repaired when starting the next charge.
        this->stop_charging(0, true, override_value);
    }

    if (charging) {
        // If the EVSE is already charging, read back the user slot info, in case the ESP just power cycled.
        UserSlotInfo info;
        bool success = read_user_slot_info(&info);
        if (success) {
            if (!charge_start_tracked) {
                charge_tracker.startCharge(info.timestamp_minutes, info.meter_start, info.user_id, info.evse_uptime_on_start, USERS_AUTH_TYPE_LOST, Config::ConfVariant{});
            } else {
                // Don't track a start, but restore the current_charge API anyway.
                charge_tracker.current_charge.get("user_id")->updateInt(info.user_id);
                charge_tracker.current_charge.get("meter_start")->updateFloat(info.meter_start);
                charge_tracker.current_charge.get("evse_uptime_start")->updateUint(info.evse_uptime_on_start);
                charge_tracker.current_charge.get("timestamp_minutes")->updateUint(info.timestamp_minutes);
                charge_tracker.current_charge.get("authorization_type")->updateUint(USERS_AUTH_TYPE_LOST);
            }
        } else if (!charge_start_tracked)
            this->start_charging(0, 32000, USERS_AUTH_TYPE_NONE, Config::ConfVariant{});
    } else {
        UserSlotInfo info;
        if (read_user_slot_info(&info)) {
            logger.printfln("Setup: stale user slot info found without active charging state. Clearing persisted user slot info.");
            zero_user_slot_info();
        }
    }

    auto outer_charger_state = get_charger_state();
    task_scheduler.scheduleUncancelable([this, outer_charger_state](){
        static uint8_t last_charger_state = outer_charger_state;

        uint8_t current_charger_state = get_charger_state();

        // If an authorization is pending and the charger is now ready to charge or charging,
        // finally start the charge tracker.
        if (pending_auth.active && (current_charger_state == CHARGER_STATE_READY_TO_CHARGE || current_charger_state == CHARGER_STATE_CHARGING)) {
            if (!charge_tracker.currentlyCharging()) {
                uint32_t evse_uptime = evse_common.get_low_level_state().get("uptime")->asUint();
                float meter_start = get_energy();
                uint32_t timestamp = rtc.timestamp_minutes();

                if (charge_tracker.startCharge(timestamp, meter_start, pending_auth.user_id, evse_uptime, pending_auth.auth_type, pending_auth.auth_info)) {
                    write_user_slot_info(pending_auth.user_id, evse_uptime, timestamp, meter_start);
                    pending_auth.active = false;
                }
            } else {
                // Already charging (e.g. through other means or previously started), clear pending.
                pending_auth.active = false;
            }
        }

        if (current_charger_state == last_charger_state)
            return;

        logger.printfln("Charger state changed from %u to %u", last_charger_state, current_charger_state);
        last_charger_state = current_charger_state;

        // stop_charging and start_charging will check
        // if a start/stop was already tracked, so it is safe
        // to call those methods more often than needed.
        switch(current_charger_state) {
            case CHARGER_STATE_NOT_PLUGGED_IN: // 0
                this->stop_charging(0, true);
                break;
            case CHARGER_STATE_WAITING_FOR_RELEASE: // 1
                // When stopping with evse/stop_charging, we did encounter a state transition from 3 -> 1.
                // If so, we stop the charge here.
                if (last_charger_state == CHARGER_STATE_CHARGING)
                    this->stop_charging(0, true);

                break; // Do not fall through here. The charge should stop and not start right away after.
            case CHARGER_STATE_READY_TO_CHARGE: // 2
                // Automatic end when changing from 3 -> 2. When the tracker is not ende here it will
                // fill with 0W-values indefinitely.
                if (last_charger_state == CHARGER_STATE_CHARGING)
                    this->stop_charging(0, true);

                // TODO: Revise the logic here. It might be the case, that the car only pauses the
                // charge. If so, the history gets fragmented. In order to avoid this scenario, one could
                // 1. implement a timout, e.g. 5 minutes before the tracker is stopped after the car ended or paused the charge.
                // 2. stop the tracker based on a power threshold for a certain time.

                // If the changes from 1 -> 2, we should start the charging here, and so we fall through here.
                [[fallthrough]];
            case CHARGER_STATE_CHARGING: // 3
                if (!get_user_slot()->get("active")->asBool())
                    this->start_charging(0, 32000, USERS_AUTH_TYPE_NONE, Config::ConfVariant{});
                break;
            case CHARGER_STATE_ERROR: // 4
                break;
        }
    }, 1_s, 1_s);

    initialized = true;

    if (config.get("http_auth_enabled")->asBool()) {
        bool user_with_password_found = false;
        for (size_t i = 0; i < config.get("users")->count(); ++i) {
            if (!config.get("users")->get(i)->get("digest_hash")->asString().isEmpty()) {
                user_with_password_found = true;
                break;
            }
        }

        if (!user_with_password_found) {
            logger.printfln("Web interface authentication can not be enabled: No user with set password found.");
            return;
        }

        server.onAuthenticate_HTTPThread([this](WebServerRequest req) -> bool {
            String auth = req.header("Authorization");
            if (auth.isEmpty()) {
                return false;
            }

            if (!auth.startsWith("Digest ")) {
                return false;
            }

            auth = auth.substring(7);
            AuthFields fields = parseDigestAuth(auth.c_str());

            bool result = false;

            // If this times out, result stays false.
            task_scheduler.await([this, &req, &fields, &result]() {
                for (size_t i = 0; i < config.get("users")->count(); ++i) {
                    if (config.get("users")->get(i)->get("username")->asString().equals(fields.username)) {
                        result = checkDigestAuthentication(fields, req.methodString(), fields.username.c_str(), config.get("users")->get(i)->get("digest_hash")->asEphemeralCStr(), nullptr, true, nullptr, nullptr, nullptr); // use of emphemeral C string ok
                        break;
                    }
                }
            });

            return result;
        });

        logger.printfln("Web interface authentication enabled.");
    }
}

void Users::search_next_free_user()
{
    uint8_t user_id = config.get("next_user_id")->asUint();
    uint8_t start_uid = user_id;
    user_id++;
    {
        File f = LittleFS.open(USERNAME_FILE, "r+");
        while (start_uid != user_id) {
            if (user_id == 0)
                user_id++;
            f.seek(user_id * USERNAME_ENTRY_LENGTH, SeekMode::SeekSet);
            char user_name_byte = 0;
            f.readBytes(&user_name_byte, 1);
            if (user_name_byte == '\0')
                break;
            user_id++;
        };
    }
    if (user_id == start_uid)
        user_id = 0;

    config.get("next_user_id")->updateUint(user_id);
}

size_t Users::get_display_name(uint8_t user_id, char *ret_buf, Language language)
{
    size_t length = 0;

    for (const auto &cfg : config.get("users")) {
        if (cfg.get("id")->asUint() == user_id) {
            const String &s = cfg.get("display_name")->asString();
            strncpy(ret_buf, s.c_str(), 32);
            length = min(s.length(), 32u);
            break;
        }
    }

    if (length == 0) {
        File f = LittleFS.open(USERNAME_FILE, "r");
        f.seek(user_id * USERNAME_ENTRY_LENGTH + USERNAME_LENGTH, SeekMode::SeekSet);
        f.read((uint8_t *)ret_buf, DISPLAY_NAME_LENGTH);
        length = strnlen(ret_buf, 32);
    }

    // length should never be 0 except if we manually upload test data to the charger.
    if (length == 0) {
        const char *buf = CSVTranslations::getDeletedUser(language);
        length = strlen(buf);
        strncpy(ret_buf, buf, DISPLAY_NAME_LENGTH);
    } else if (user_id == 0 && strcmp(ret_buf, "Anonymous") == 0) {
        const char *buf = CSVTranslations::getUnknownUser(language);
        length = strlen(buf);
        strncpy(ret_buf, buf, DISPLAY_NAME_LENGTH);
    }

    return length;
}

bool Users::is_user_configured(uint8_t user_id)
{
    for (const auto &cfg : config.get("users"))
        if (cfg.get("id")->asUint() == user_id)
            return true;

    return false;
}

bool Users::resolve_charge_action_user(ConfigRoot &command, uint8_t *resolved_user_id, String &errmsg)
{
    const uint32_t user_id_cfg = command.get("id")->asUint();
    const String &username = command.get("username")->asString();
    const bool have_user_id = user_id_cfg < 256;
    const bool have_username = !username.isEmpty();

    if (!have_user_id && !have_username) {
        *resolved_user_id = 0;
        return true;
    }

    if (have_user_id && have_username) {
        for (size_t i = 0; i < config.get("users")->count(); ++i) {
            Config *user_cfg = (Config *)config.get("users")->get(i);
            if (user_cfg->get("id")->asUint() == user_id_cfg) {
                if (user_cfg->get("username")->asString() == username) {
                    *resolved_user_id = static_cast<uint8_t>(user_id_cfg);
                    return true;
                }

                errmsg = "Provided id and username do not belong to the same user.";
                return false;
            }
        }

        errmsg = "User with id " + String(static_cast<unsigned long>(user_id_cfg)) + " not found.";
        return false;
    }

    if (have_user_id) {
        *resolved_user_id = static_cast<uint8_t>(user_id_cfg);
        return true;
    }

    if (have_username) {
        for (size_t i = 0; i < config.get("users")->count(); ++i) {
            if (config.get("users")->get(i)->get("username")->asString() == username) {
                *resolved_user_id = config.get("users")->get(i)->get("id")->asUint();
                return true;
            }
        }

        errmsg = "User with username '" + username + "' not found.";
        return false;
    }

    return false;
}

#if MODULE_EVSE_LED_AVAILABLE()
static void check_waiting_for_start()
{
    const Config *user_slot = (const Config *)api.getState("evse/slots", false)->get(CHARGING_SLOT_USER);

    if (!user_slot->get("active")->asBool())
        return;


    bool waiting_for_start = (api.getState("evse/state", false)->get("iec61851_state")->asUint() == 1)
                          && (user_slot->get("max_current")->asUint() == 0);

    if (waiting_for_start)
        evse_led.set_module(EvseLed::Blink::Nag, 2000);
}
#endif

void Users::register_urls()
{
    // No users (except anonymous) configured: Make sure the EVSE's user slot is disabled.
    bool user_slot = false;

    if (api.hasFeature("evse"))
        user_slot = evse_common.get_slots().get(CHARGING_SLOT_USER)->get("active")->asBool();

    if (config.get("users")->count() <= 1 && user_slot) {
        logger.printfln("User slot enabled, but no users configured. Disabling user slot.");
        api.callCommand("evse/user_enabled_update", Config::ConfUpdateObject{{
            {"enabled", false}
        }});
    }

    api.addCommand("users/modify", &modify, {"digest_hash", "display_name", "username"}, [this](Language /*language*/, String &errmsg) {
        auto id = modify.get("id")->asUint();

        Config *user = nullptr;
        for (size_t i = 0; i < config.get("users")->count(); ++i) {
            if (config.get("users")->get(i)->get("id")->asUint() == id) {
                user = (Config *)config.get("users")->get(i);
                break;
            }
        }

        // Validity was already checked, but we have to search the user config anyway.
        if (user == nullptr) {
            errmsg = "Can't modify user. User with this ID not found.";
            return;
        }

        user->get("roles")->updateUint(modify.get("roles")->asUint());
        user->get("current")->updateUint(modify.get("current")->asUint());
        bool display_name_changed = user->get("display_name")->updateString(modify.get("display_name")->asString());
        bool username_changed = user->get("username")->updateString(modify.get("username")->asString());
        user->get("digest_hash")->updateString(modify.get("digest_hash")->asString());

        String err = this->config.validate(ConfigSource::API);
        if (!err.isEmpty()) {
            errmsg = err;
            return;
        }

        API::writeConfig("users/config", &config);

        if (display_name_changed || username_changed)
            this->rename_user(user->get("id")->asUint(), user->get("username")->asString(), user->get("display_name")->asString());

        modify.get("id")->updateUint(256);
        modify.get("roles")->updateUint(0);
        modify.get("current")->updateUint(32000 + 1);
        modify.get("display_name")->updateString("___MARKER___VALUE___");
        modify.get("username")->updateString("___MARKER___VALUE___");
        modify.get("digest_hash")->updateString("___MARKER___VALUE___");
    }, true);

    api.addState("users/config", &config, {"digest_hash"}, {"display_name", "username"});
    api.addCommand("users/add", &add, {"digest_hash", "display_name", "username"}, [this](Language /*language*/, String &/*errmsg*/) {
        auto user = config.get("users")->add();

        user->get("id")->updateUint(add.get("id")->asUint());
        user->get("roles")->updateUint(add.get("roles")->asUint());
        user->get("current")->updateUint(add.get("current")->asUint());
        user->get("display_name")->updateString(add.get("display_name")->asString());
        user->get("username")->updateString(add.get("username")->asString());
        user->get("digest_hash")->updateString(add.get("digest_hash")->asString());

        search_next_free_user();

        API::writeConfig("users/config", &config);
        this->rename_user(user->get("id")->asUint(), user->get("username")->asString(), user->get("display_name")->asString());
    }, true);

    api.addCommand("users/remove", &remove, {}, [this](Language /*language*/, String &/*errmsg*/) {
        size_t idx = std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < config.get("users")->count(); ++i) {
            if (config.get("users")->get(i)->get("id")->asUint() == remove.get("id")->asUint()) {
                idx = i;
                break;
            }
        }

        if (idx == std::numeric_limits<size_t>::max()) {
            // Defense in depth: the validator has already checked this
            // condition. This if should never be true
            logger.printfln("Can't remove user. User with this ID not found.");
            return;
        }

        config.get("users")->remove(idx);
        API::writeConfig("users/config", &config);

#if MODULE_NFC_AVAILABLE()
        nfc.remove_user(remove.get("id")->asUint());
#endif

        if (!charge_tracker.is_user_tracked(remove.get("id")->asUint()))
        {
            this->rename_user(remove.get("id")->asUint(), "", "");
            // If this user still has tracked charges, we can't recycle their ID, so it is correct
            // to check this here (and not one level up).
            if (config.get("next_user_id")->asUint() == 0)
            {
                config.get("next_user_id")->updateUint(remove.get("id")->asUint());
                API::writeConfig("users/config", &config);
            }
        }
    }, true);

    api.addCommand("users/http_auth_update", &http_auth_update, {}, [this](Language /*language*/, String &/*errmsg*/) {
        bool enable = http_auth_update.get("enabled")->asBool();
        if (!enable) {
            server.runInHTTPThread([](void *arg) {
                server.onAuthenticate_HTTPThread([](WebServerRequest req){return true;});
            }, nullptr);
        }

        config.get("http_auth_enabled")->updateBool(enable);
        API::writeConfig("users/config", &config);
    }, false);

    api.addCommand("users/start_charging", &start_charging_cmd, {}, [this](Language /*language*/, String &errmsg) {
        uint8_t user_id = 0;
        if (!this->resolve_charge_action_user(start_charging_cmd, &user_id, errmsg)) {
            start_charging_cmd.get("id")->updateUint(256);
            start_charging_cmd.get("username")->updateString("");
            return;
        }

        if (!this->trigger_charge_action(user_id, USERS_AUTH_TYPE_NONE, Config::ConfVariant{}, TRIGGER_CHARGE_START, 0_s, 0_s)) {
            errmsg = "Failed to start charge for user. Please check EVSE state.";
        }

        start_charging_cmd.get("id")->updateUint(256);
        start_charging_cmd.get("username")->updateString("");
    }, true);

    api.addCommand("users/stop_charging", &stop_charging_cmd, {}, [this](Language /*language*/, String &errmsg) {
        uint8_t user_id = 0;
        if (!this->resolve_charge_action_user(stop_charging_cmd, &user_id, errmsg)) {
            stop_charging_cmd.get("id")->updateUint(256);
            stop_charging_cmd.get("username")->updateString("");
            return;
        }

        this->trigger_charge_action(user_id, USERS_AUTH_TYPE_NONE, Config::ConfVariant{}, TRIGGER_CHARGE_STOP, 0_s, 0_s);

        stop_charging_cmd.get("id")->updateUint(256);
        stop_charging_cmd.get("username")->updateString("");
    }, true);

    server.on_HTTPThread("/users/all_usernames", HTTP_GET, [this](WebServerRequest request) {
        //std::lock_guard<std::mutex> lock{records_mutex};
        size_t len = MAX_PASSIVE_USERS * USERNAME_ENTRY_LENGTH;
        auto buf = heap_alloc_array<char>(len);
        if (buf == nullptr) {
            return request.send_plain(507);
        }

        size_t read = 0;

        {
            File f = LittleFS.open(USERNAME_FILE, "r");
            read = f.read((uint8_t *)buf.get(), len);
        }

        return request.send_bytes(200, buf.get(), read);
    });

#if MODULE_EVSE_LED_AVAILABLE()
    task_scheduler.scheduleUncancelable([](){check_waiting_for_start();}, 1_s, 1_s);
#endif
}

uint8_t Users::next_user_id()
{
    return this->config.get("next_user_id")->asUint();
}

void Users::rename_user(uint8_t user_id, const String &username, const String &display_name)
{
    char buf[USERNAME_ENTRY_LENGTH] = {0};
    username.toCharArray(buf, USERNAME_LENGTH);
    display_name.toCharArray(buf + USERNAME_LENGTH, DISPLAY_NAME_LENGTH);

    File f = LittleFS.open(USERNAME_FILE, "r+");
    f.seek(user_id * USERNAME_ENTRY_LENGTH, SeekMode::SeekSet);
    f.write((const uint8_t *)buf, USERNAME_ENTRY_LENGTH);
}

void Users::remove_from_username_file(uint8_t user_id)
{
    Config *users = (Config *)config.get("users");
    for (size_t i = 0; i < users->count(); ++i) {
        if (users->get(i)->get("id")->asUint() == user_id) {
            return;
        }
    }

    this->rename_user(user_id, "", "");
    if (config.get("next_user_id")->asUint() == 0) {
        config.get("next_user_id")->updateUint(user_id);
        API::writeConfig("users/config", &config);
    }
}

// Only returns true if the triggered action was a charge start.
bool Users::trigger_charge_action(uint8_t user_id, uint8_t auth_type, Config::ConfVariant auth_info, int action, micros_t deadtime_post_stop, micros_t deadtime_post_start)
{
    bool user_enabled = get_user_slot()->get("active")->asBool();
    if (!user_enabled)
        return false;
    // This is called whenever a user wants to trigger a charge action.
    // I.e. when holding an NFC tag at the box or when calling the start_charging API

    uint16_t current_limit = 0;
    Config *users = (Config *)config.get("users");
    for (size_t i = 0; i < users->count(); ++i) {
        if (users->get(i)->get("id")->asUint() != user_id)
            continue;

        current_limit = users->get(i)->get("current")->asUint();
    }

    if (current_limit == 0) {
        logger.printfln("Unknown user with ID %u.", user_id);
        return false;
    }

    uint8_t iec_state = evse_common.get_state().get("iec61851_state")->asUint();
    uint32_t tscs = evse_common.get_low_level_state().get("time_since_state_change")->asUint();

    switch (iec_state) {
        case IEC_STATE_B: // State B: The user wants to start charging. If we already have a tracked charge, stop charging to allow switching to another user.
            if (charge_tracker.currentlyCharging()) {
                if ((action == TRIGGER_CHARGE_ANY || action == TRIGGER_CHARGE_STOP) && (auth_type == USERS_AUTH_TYPE_NFC_INJECTION || deadline_elapsed(last_charge_action_triggered + deadtime_post_start)))
                    this->stop_charging(user_id, false);
                return false;
            }
            if ((action == TRIGGER_CHARGE_ANY || action == TRIGGER_CHARGE_START) && (auth_type == USERS_AUTH_TYPE_NFC_INJECTION || deadline_elapsed(last_charge_action_triggered + deadtime_post_stop))) {
                // If a user (via NFC or API) wants to start charging, we also automatically release
                // the manual charging slot (Concept 2). This prevents the case where the user is
                // authorized but the charge is blocked by a previous manual stop.
                api.callCommand("evse/start_charging");
                return this->start_charging(user_id, current_limit, auth_type, auth_info);
            }
            return false;
        case IEC_STATE_C: // State C: The user wants to stop charging.
            // Debounce here a bit, an impatient user can otherwise accidentially trigger a stop if a start_charging takes too long.
            if (tscs > 3000 && (action == TRIGGER_CHARGE_ANY || action == TRIGGER_CHARGE_STOP) && (auth_type == USERS_AUTH_TYPE_NFC_INJECTION || deadline_elapsed(last_charge_action_triggered + deadtime_post_start)))
                this->stop_charging(user_id, false);
            return false;
        default: //Don't do anything in state A, D, and E/F
            break;
    }
    return false;
}

void Users::remove_username_file()
{
    if (LittleFS.exists(USERNAME_FILE))
        LittleFS.remove(USERNAME_FILE);
}

bool Users::start_charging(uint8_t user_id, uint16_t current_limit, uint8_t auth_type, Config::ConfVariant auth_info)
{
    last_charge_action_triggered = now_us();

    // If we are already charging, we don't start a new charge.
    if (charge_tracker.currentlyCharging())
        return false;

    // Delayed tracker start. We only authorize the user here
    // and set their current limit. The actual charge tracker start
    // is performed in the periodic task once the EVSE reaches a state
    // where it is actually ready to charge or charging.
    pending_auth.user_id = user_id;
    pending_auth.current_limit = current_limit;
    pending_auth.auth_type = auth_type;
    pending_auth.auth_info = auth_info;
    pending_auth.active = true;

    evse_common.set_user_current(current_limit);

    return true;
}

bool Users::stop_charging(uint8_t user_id, bool force, float meter_abs)
{
    last_charge_action_triggered = now_us();

    // If a charge was pending but hasn't started yet, we can just clear it.
    if (pending_auth.active && (force || pending_auth.user_id == user_id)) {
        pending_auth.active = false;
    }

    if (charge_tracker.currentlyCharging()) {
        UserSlotInfo info;
        bool success = read_user_slot_info(&info);
        // If reading the user slot info failed, we don't know which user started this charge anymore.
        // This should only happen if the EVSE power-cycles, however on a power-cycle any running charge
        // should be aborted. It is safe to allow tracking a charge end in this case for any authorized card,
        // as this should never happen anyway.
        // Allow forcing the endCharge tracking. This is necessary in the case that the car was disconnected.
        // The user is then authorized at the other end of the charging cable.
        if (!force && success && info.user_id != user_id)
            return false;

        uint32_t charge_duration = 0;
        if (success) {
            uint32_t now_seconds = evse_common.get_low_level_state().get("uptime")->asUint() / 1000;
            uint32_t start_seconds = info.evse_uptime_on_start / 1000;
            if (now_seconds < start_seconds) {
                now_seconds += (0xFFFFFFFF / 1000);
            }
            charge_duration = now_seconds - start_seconds;
        }

        if (meter_abs)
            charge_tracker.endCharge(charge_duration, meter_abs);
        else
            charge_tracker.endCharge(charge_duration, get_energy());
    }

    zero_user_slot_info();

    // Only clear the user current if no other authorization is pending.
    if (!pending_auth.active) {
        evse_common.set_user_current(0);
    }

    return true;
}
