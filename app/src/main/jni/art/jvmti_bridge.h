// art/jvmti_bridge.h — JVMTI operations surfaced as a C++ API.
//
// Not a reimplementation. Each method issues a command to the JS
// bootstrap agent which uses Java.performNow + the bridge's JVMTI
// accessors. Blocking; call from a worker thread (never the JS thread).
#ifndef YAMGG_ART_JVMTI_BRIDGE_H
#define YAMGG_ART_JVMTI_BRIDGE_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace yamgg {
namespace art {

struct LoadedClass {
    std::string name;
};

struct ClassMethod {
    std::string              name;
    std::string              returnType;
    std::vector<std::string> argumentTypes;
    std::string              type;         // "instance" | "static" | "constructor"
};

struct ClassField {
    std::string name;
    std::string type;
};

struct ClassInspection {
    std::string              className;
    std::vector<ClassMethod> methods;
    std::vector<ClassField>  fields;
};

class JvmtiBridge {
public:
    static JvmtiBridge& instance();

    // Adds can_tag_objects (the only capability the bridge requires).
    // Idempotent. Returns true if the capability was added or was
    // already present.
    bool add_capabilities(std::string& error, int timeout_ms = 3000);

    // Enumerates every loaded class name on the target.
    bool get_loaded_classes(std::vector<LoadedClass>& out,
                            std::string& error,
                            int timeout_ms = 5000);

    // Returns the declared methods and fields of a class.
    bool get_class_members(const std::string& className,
                           ClassInspection& out,
                           std::string& error,
                           int timeout_ms = 5000);

private:
    JvmtiBridge() = default;
};

} // namespace art
} // namespace yamgg

#endif // YAMGG_ART_JVMTI_BRIDGE_H
