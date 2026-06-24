#include "JobId.hpp"
#include <stdexcept>

JobId::JobId(const std::string& v) {
    if (uuid_parse(v.c_str(), value) != 0) throw std::invalid_argument("invalid job id: " + v);
}

JobId::JobId(const uuid_t& uuid) {
    uuid_copy(value, uuid);
}

JobId JobId::generate() {
    uuid_t uuid;
    uuid_generate(uuid);
    return JobId(uuid);
}

