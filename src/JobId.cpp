#include "JobId.hpp"
#include <stdexcept>

/**
Convert string to JobId constructor. Throw exception if invalid string.
*/
JobId::JobId(const std::string& v) {
    if (uuid_parse(v.c_str(), value) != 0) throw std::invalid_argument("invalid job id: " + v);
}

/**
Convert raw uuid to JobId constructor.
*/
JobId::JobId(const uuid_t& uuid) {
    uuid_copy(value, uuid);
}

/**
Generate a new uuid and wrap inside a JobId. 
@return New JobId
*/
JobId JobId::generate() {
    uuid_t uuid;
    uuid_generate(uuid);
    return JobId(uuid);
}

