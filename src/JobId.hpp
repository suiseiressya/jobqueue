#pragma once
#include <uuid/uuid.h>

#include <functional>
#include <string>
#include <string_view>

class JobId {
public:
    uuid_t value;

    JobId() {}
    explicit JobId(const std::string&);
    explicit JobId(const uuid_t&);
    static JobId generate();

    std::string to_string() const {
        char str[37];
        uuid_unparse_lower(value, str);
        return std::string(str);
    }

    bool operator==(const JobId& other) const {
        return uuid_compare(value, other.value) == 0;
    }
};

template <>
struct std::hash<JobId> {
    size_t operator()(const JobId& id) const {
        return std::hash<std::string_view>{}(
            std::string_view(reinterpret_cast<const char*>(id.value), sizeof(uuid_t)));
    }
};
