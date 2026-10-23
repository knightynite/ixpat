#pragma once
#include <nlohmann/json.hpp>

#include "core/actions.h"
#include "core/events.h"

// JSON (de)serializers that reproduce the layout Swift's synthesized Codable writes in the macOS app:
//  - enums with associated values are single-key objects: {"launchApp":{"path":"..."}}, {"none":{}}
//  - nil optionals are omitted, present ones are plain keys
//  - UUIDs are uppercase strings
// They live in namespace ixpat so nlohmann finds them by ADL.
namespace ixpat {

void to_json(nlohmann::json& j, const HapticEcho& e);
void from_json(const nlohmann::json& j, HapticEcho& e);
void to_json(nlohmann::json& j, const HapticRule& r);
void from_json(const nlohmann::json& j, HapticRule& r);
void to_json(nlohmann::json& j, const ButtonAction& a);
void from_json(const nlohmann::json& j, ButtonAction& a);
void to_json(nlohmann::json& j, const RingItem& r);
void from_json(const nlohmann::json& j, RingItem& r);
void to_json(nlohmann::json& j, const AppProfile& p);
void from_json(const nlohmann::json& j, AppProfile& p);

}  // namespace ixpat
