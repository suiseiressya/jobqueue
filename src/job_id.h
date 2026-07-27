#pragma once
#include <uuid/uuid.h>

#include <functional>
#include <string>
#include <string_view>

/**
    JobId class.
    Wrapper around uuid/uuid.h library
*/
class JobId {
public:
    uuid_t value_;

    JobId() {}
    explicit JobId(const std::string&);
    explicit JobId(const uuid_t&);
    static JobId Generate();

    std::string ToString() const {
        char str[37];
        uuid_unparse_lower(value_, str);
        return std::string(str);
    }

    /// operator== for hashtable-based data structures
    bool operator==(const JobId& other) const {
        return uuid_compare(value_, other.value_) == 0;
    }
};

/// Hash functions for hashtable-based data structures
template <>
struct std::hash<JobId> {
    size_t operator()(const JobId& id) const {
        // reinterpret_cast into bytes (const char*) instead of ToString to speed up
        return std::hash<std::string_view>{}(
            std::string_view(reinterpret_cast<const char*>(id.value_), sizeof(uuid_t)));
    }
};
