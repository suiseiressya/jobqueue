#include "job_id.h"
#include <stdexcept>

/**
Convert string to JobId constructor. Throw exception if invalid string.
*/
JobId::JobId(const std::string& v) {
    if (uuid_parse(v.c_str(), value_) != 0) throw std::invalid_argument("invalid job id: " + v);
}

/**
Convert raw uuid to JobId constructor.
*/
JobId::JobId(const uuid_t& uuid) {
    uuid_copy(value_, uuid);
}

/**
Generate a new uuid and wrap inside a JobId.
@return New JobId
*/
JobId JobId::Generate() {
    uuid_t uuid;
    uuid_generate(uuid);
    return JobId(uuid);
}

