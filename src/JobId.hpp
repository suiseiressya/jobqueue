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

    /// operator== for hashtable-based data structures
    bool operator==(const JobId& other) const {
        return uuid_compare(value, other.value) == 0;
    }
};

/// Hash functions for hashtable-based data structures
template <>
struct std::hash<JobId> {
    size_t operator()(const JobId& id) const {
        // reinterpret_cast into bytes (const char*) instead of to_string to speed up
        return std::hash<std::string_view>{}(
            std::string_view(reinterpret_cast<const char*>(id.value), sizeof(uuid_t)));
    }
};
