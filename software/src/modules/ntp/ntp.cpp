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
#include "ntp.h"

#include <time.h>
#include <esp_netif.h>
#include <esp_sntp.h>

#include "event_log_prefix.h"
#include "generated/module_dependencies.h"
#include "timezone_translation.h"

#include "gcc_warnings.h"

static void set_sntp_server_slot(uint8_t slot, const String &server)
{
    // The ESP SNTP implementation stores raw pointers and does not duplicate
    // the passed strings. `ntp_server1` / `ntp_server2` are long-lived members,
    // so using their c_str() pointers here is safe across the SNTP runtime as
    // long as callers pass references to those persistent member strings.
    //
    // Passing nullptr explicitly clears a previously configured slot. This is
    // important when switching between DHCP/manual modes at runtime so stale
    // server entries from the prior mode are not kept unintentionally.
    esp_sntp_setservername(slot, server.isEmpty() ? nullptr : server.c_str());
}

static void ntp_sync_cb(struct timeval *tv)
{
#if MODULE_RTC_AVAILABLE()
    rtc.push_system_time(*tv, Rtc::Quality::High);
#else
    settimeofday(tv, NULL);
#endif

    ntp.time_synced_NTPThread();
}

void NTP::pre_setup()
{
    config = ConfigRoot{Config::Object({
        {"enable", Config::Bool(true)},
        {"use_dhcp", Config::Bool(true)},
        {"timezone", Config::Str("Europe/Berlin", 0, 32)}, // Longest is America/Argentina/ComodRivadavia = 32 chars
        {"server", Config::Str("time.cloudflare.com", 0, 64)}, // We've applied for a vendor zone @ pool.ntp.org, however this seems to take quite a while. Use Cloudflare's public anycast servers for now.
        {"server2", Config::Str("time.google.com", 0, 64)}, // Google's public anycast servers as backup.
    }), [this](Config &conf, ConfigSource source) -> String {
        if (lookup_timezone(conf.get("timezone")->asEphemeralCStr()) == nullptr)
            return "Can't update config: Failed to look up timezone.";

        if (source != ConfigSource::File) {
            task_scheduler.scheduleOnce([this]() {
                this->apply_config();
            });
        }

        return "";
    }};

    state = Config::Object({
        {"synced", Config::Bool(false)},
        {"time", Config::Uint32(0)} // unix timestamp in minutes
    });
}

void NTP::setup()
{
    initialized = true;

    api.restorePersistentConfig("ntp/config", &config);

    // Enable network stack before setting any SNTP options.
    // It should be safe to set SNTP options without the network stack
    // running, but it needs to be running to send any SNTP queries anyway.
    esp_netif_init();

    apply_config();
}

void NTP::register_events()
{
#if MODULE_NETWORK_AVAILABLE()
    network.on_network_connected([this](const Config *connected) {
        if (connected->asBool() && config.get("enable")->asBool() && !esp_sntp_enabled()) {
            esp_sntp_init();
        }

        return EventResult::OK;
    });
#endif
}

void NTP::apply_config()
{
    const char *tz_name = config.get("timezone")->asUnsafeCStr();
    const char *tz_string = lookup_timezone(tz_name);

    if (tz_string == nullptr) {
        logger.printfln("Failed to look up timezone information for %s. Will not set timezone", tz_name);
        return;
    }

    setenv("TZ", tz_string, 1);
    tzset();
    logger.printfln("Set timezone to %s", tz_name);

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    if (!config.get("enable")->asBool()) {
        set_synced(false);
        sync_expires_at = 0_us;
        return;
    }

    // Keep local copies of unsafe ConfStrings because the SNTP lib doesn't create its own copies and holds references to whatever is passed to it.
    ntp_server1 = config.get("server" )->asString();
    ntp_server2 = config.get("server2")->asString();

    const bool set_servers_from_dhcp = config.get("use_dhcp")->asBool();

    // Historical context for removed legacy callback override:
    //
    // Earlier firmware versions implemented a global
    // `extern "C" sntp_sync_time(struct timeval*)` symbol and routed it into
    // module logic. That path worked as a compatibility override across older
    // lwIP/ESP-IDF combinations where callback registration behavior differed
    // or was not yet consistently used.
    //
    // We now use the explicit SNTP callback registration API
    // (`esp_sntp_set_time_sync_notification_cb`) as the single integration
    // path. This keeps the callback flow deterministic and easier to reason
    // about: SNTP updates time -> registered callback runs -> RTC/state update.
    //
    // Runtime behavior is equivalent for module logic (`ntp_sync_cb` still
    // performs RTC push + `time_synced_NTPThread()`), but we avoid maintaining
    // two overlapping callback hooks with potentially different call paths.
    esp_sntp_set_time_sync_notification_cb(ntp_sync_cb);

    // Getting SNTP servers from DHCP should be enabled before setting up Ethernet or WiFi.
    esp_sntp_servermode_dhcp(set_servers_from_dhcp);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    const String &manual_server_primary = ntp_server1.isEmpty() ? ntp_server2 : ntp_server1;
    const String &manual_server_secondary = ntp_server2.isEmpty() ? ntp_server1 : ntp_server2;

    if (set_servers_from_dhcp) {
        // DHCP mode: keep slot 0 reserved for server addresses offered via
        // DHCP and make sure stale manual values from previous configs are
        // removed first.
        set_sntp_server_slot(0, String{});
        set_sntp_server_slot(1, manual_server_primary);
        set_sntp_server_slot(2, manual_server_secondary);
    } else {
        // Manual mode: use slots 0/1 for configured servers and clear slot 2 so
        // no stale fallback from a prior DHCP-enabled config survives.
        set_sntp_server_slot(0, manual_server_primary);
        set_sntp_server_slot(1, manual_server_secondary);
        set_sntp_server_slot(2, String{});
    }

    if (manual_server_primary.isEmpty()) {
        logger.printfln("No manual NTP servers configured; relying on DHCP-provided servers only.");
    }

#if MODULE_NETWORK_AVAILABLE()
    // During initial boot, the network is not yet connected.
    // register_events() will start SNTP when the network first connects.
    // For runtime reconfiguration, start SNTP immediately if the network is already up.
    if (network.is_connected()) {
        esp_sntp_init();
    }
#else
    esp_sntp_init();
#endif
}

void NTP::set_synced(bool synced)
{
    this->state.get("synced")->updateBool(synced);
}

void NTP::set_api_time(struct timeval time) {
    state.get("time")->updateUint(static_cast<uint32_t>(time.tv_sec / 60)); // This will overflow in 10135 CE, which seems safe enough.
}

void NTP::register_urls()
{
    api.addPersistentConfig("ntp/config", &config);
    api.addState("ntp/state", &state);
}

void NTP::time_synced_NTPThread() {
    micros_t now = now_us();

    if (sync_expires_at == 0_us) {
        uint32_t now_u32 = now.to<millis_t>().as<uint32_t>();

        task_scheduler.scheduleOnce([this, now_u32]() {
            this->set_synced(true);

            uint32_t secs = now_u32 / 1000;
            uint32_t ms   = now_u32 % 1000;
            // Don't log in TCP/IP task: Deadlocks the event lock
            logger.printfln("NTP synchronized at %lu,%03lu", secs, ms);
        });

        task_scheduler.scheduleUncancelable([this]() {
            if (deadline_elapsed(this->sync_expires_at)) {
                this->set_synced(false);
            }
        }, 1_h, 1_h);
    } else {
        task_scheduler.scheduleOnce([this]() {
            this->set_synced(true);
        });
    }

    sync_expires_at = now + 25_h;
}
