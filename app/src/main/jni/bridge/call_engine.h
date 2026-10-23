// bridge/call_engine.h — invoke Java methods, create instances, read/write fields.
#ifndef YAMGG_BRIDGE_CALL_ENGINE_H
#define YAMGG_BRIDGE_CALL_ENGINE_H

#include <string>
#include <vector>

#include <json.hpp>

namespace yamgg {
namespace bridge {

class CallEngine {
public:
    static CallEngine& instance();

    // Call an instance method. `thisHandle` is a previously-returned
    // handle string. `args` is a JSON array of arguments; the JS side
    // coerces each element to the overload's expected type.
    bool call_instance(const std::string& className,
                       const std::string& methodName,
                       const std::vector<std::string>& overloadTypes,
                       const std::string& thisHandle,
                       const nlohmann::json& args,
                       nlohmann::json& result,
                       std::string& error,
                       int timeout_ms = 5000);

    // Call a static method.
    bool call_static(const std::string& className,
                     const std::string& methodName,
                     const std::vector<std::string>& overloadTypes,
                     const nlohmann::json& args,
                     nlohmann::json& result,
                     std::string& error,
                     int timeout_ms = 5000);

    // Create an instance via the constructor whose overload matches
    // `ctorTypes`. Returns the new handle in `handle`; `className` is
    // echoed back so callers can remember the type.
    bool create_instance(const std::string& className,
                         const std::vector<std::string>& ctorTypes,
                         const nlohmann::json& args,
                         std::string& handle,
                         std::string& error,
                         int timeout_ms = 5000);

    // Read a field. If `thisHandle` is empty, reads a static field.
    bool read_field(const std::string& className,
                    const std::string& fieldName,
                    const std::string& thisHandle,
                    nlohmann::json& value,
                    std::string& error,
                    int timeout_ms = 3000);

    // Write a field. If `thisHandle` is empty, writes a static field.
    bool write_field(const std::string& className,
                     const std::string& fieldName,
                     const std::string& thisHandle,
                     const nlohmann::json& value,
                     std::string& error,
                     int timeout_ms = 3000);

private:
    CallEngine() = default;
};

} // namespace bridge
} // namespace yamgg

#endif // YAMGG_BRIDGE_CALL_ENGINE_H
