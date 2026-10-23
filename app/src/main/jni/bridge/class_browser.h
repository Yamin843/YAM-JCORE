// bridge/class_browser.h — class enumeration and introspection.
#ifndef YAMGG_BRIDGE_CLASS_BROWSER_H
#define YAMGG_BRIDGE_CLASS_BROWSER_H

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace yamgg {
namespace bridge {

struct MethodSignature {
    std::string              name;
    std::string              returnType;
    std::vector<std::string> argumentTypes;
    std::string              type;         // "instance" | "static" | "constructor"
};

struct FieldSignature {
    std::string name;
    std::string type;
};

struct ConstructorSignature {
    std::vector<std::string> argumentTypes;
};

struct ClassInspection {
    std::string                       className;
    std::vector<MethodSignature>      methods;
    std::vector<FieldSignature>       fields;
    std::vector<ConstructorSignature> constructors;
};

class ClassBrowser {
public:
    static ClassBrowser& instance();

    // List classes on the target, optionally filtered by substring.
    // Appends to `out`. Returns false and fills `error` on failure.
    bool list_classes(const std::string& filter,
                      std::vector<std::string>& out,
                      std::string& error,
                      int timeout_ms = 5000);

    // Inspect a class once. Subsequent calls with the same name return
    // the cached result unless `force_refresh` is true.
    bool inspect_class(const std::string& className,
                       ClassInspection& out,
                       std::string& error,
                       bool force_refresh = false,
                       int timeout_ms = 5000);

    // Drop a specific cache entry or the whole cache.
    void invalidate(const std::string& className);
    void invalidate_all();

private:
    ClassBrowser() = default;

    std::mutex                                       mu_;
    std::unordered_map<std::string, ClassInspection> cache_;
};

} // namespace bridge
} // namespace yamgg

#endif // YAMGG_BRIDGE_CLASS_BROWSER_H
