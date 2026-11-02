#pragma once
#include "sms_mod.h"
#include <string>

namespace qapi {
// Only the original eight-byte header is assumed before checking table sizes.
inline bool header(const SmsApi* host) {
    return host && host->size >= offsetof(SmsApi, minor) + sizeof(host->minor);
}
inline std::string version(const SmsApi* host) {
    return header(host) ? std::to_string(host->major) + "." + std::to_string(host->minor) : "unavailable";
}
inline std::string missing(const SmsApi* host) {
    if (!header(host)) return "The game did not supply a complete native mod API header.";
    if (host->major != 1) return "This mod needs native mod API major version 1.";
    // Check both the outer table and each member before dereferencing them.
    // A newer SDK minor must not make optional extensions mandatory.
    std::string absent;
#define Q_REQUIRE(table, member) \
    if (!SMS_API_HAS(host, table) || !SMS_API_HAS(host->table, member)) { \
        if (!absent.empty()) absent += ", "; \
        absent += #table "." #member; \
    }
    Q_REQUIRE(core, log)
    Q_REQUIRE(core, subscribe)
    Q_REQUIRE(core, mod_dir)
    Q_REQUIRE(core, data_dir)
    Q_REQUIRE(game, state)
    Q_REQUIRE(game, find_object)
    Q_REQUIRE(game, warp)
    Q_REQUIRE(player, get)
    Q_REQUIRE(player, set_position)
    Q_REQUIRE(player, set_velocity)
    Q_REQUIRE(player, set_forward_speed)
    Q_REQUIRE(input, bind_key)
    Q_REQUIRE(config, add_option)
#undef Q_REQUIRE
    return absent.empty() ? "" : "Required native API functions are missing: " + absent +
        ". Select an official game release that provides these functions.";
}
} // namespace qapi
