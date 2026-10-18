#include "payload.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

using Json = nlohmann::json;

WorkSpec ParsePayload(const std::string& payload) {
    Json json = Json::parse(payload, nullptr, /*allow_exceptions=*/false);
    if (json.is_discarded()) throw std::invalid_argument("payload is not valid JSON");
    if (!json.is_object()) throw std::invalid_argument("payload is valid JSON but not an object");

    WorkSpec spec;
    if (json.contains("sleep_ms")) {
        const Json& sleep = json["sleep_ms"];
        // is_number_integer rejects 1.5 and "ten"; the range check keeps get<int> exact.
        if (!sleep.is_number_integer() || sleep.get<long long>() < 0 ||
            sleep.get<long long>() > kMaxSleepMs) {
            throw std::invalid_argument("'sleep_ms' must be an integer from 0 to " +
                                        std::to_string(kMaxSleepMs));
        }
        spec.sleep_ms = sleep.get<int>();
    }
    if (json.contains("fail")) {
        if (!json["fail"].is_boolean()) throw std::invalid_argument("'fail' must be a boolean");
        spec.fail = json["fail"].get<bool>();
    }
    return spec;
}
