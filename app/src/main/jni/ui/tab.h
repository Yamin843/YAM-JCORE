// ui/tab.h — abstract Tab interface and TabRegistry.
//
// MainWindow owns a vector of tabs registered at runtime. Reply 6
// provides the concrete tab classes and a register_all_default_tabs()
// function; MainWindow calls it once before its first draw.
//
// A tab must be thread-affine to the UI thread: every method is called
// by the ImGui render loop with the renderer's mutex held. Callbacks
// coming from the JS thread must marshal to the UI thread before
// touching tab state — use yamgg::marshal_to_ui().
#ifndef YAMGG_UI_TAB_H
#define YAMGG_UI_TAB_H

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace yamgg {

class Tab {
public:
    virtual ~Tab() = default;

    // Short label shown in the sidebar (2-4 characters).
    virtual const char* name() const = 0;

    // Optional single-character icon glyph. Empty string means none.
    virtual const char* icon() const { return ""; }

    // Draw the tab content into the currently active ImGui region.
    // Called once per frame while the tab is the active one. When the
    // tab is not active, this is not called. Every tab is expected to
    // be reasonably fast (< 1 ms) or to use virtualization for large
    // lists (ImGuiListClipper).
    virtual void draw() = 0;

    // Called when the tab is transitioned to from another tab.
    virtual void on_activated() {}

    // Called when the tab is transitioned away from.
    virtual void on_deactivated() {}

    // Called when a JS message arrives while this tab is active.
    // Default is no-op. Subclasses that need to react to hook events,
    // heap match events, etc., override this. Called from the JS
    // thread, so subclasses must marshal internally to the UI thread
    // unless the callback only writes to thread-safe state.
    virtual void on_event(const std::string& type, const std::string& json) {
        (void)type; (void)json;
    }
};

class TabRegistry {
public:
    static TabRegistry& instance();

    // Register a tab. Ownership is transferred. Tabs are kept in
    // registration order; the first registered tab becomes the initial
    // active tab.
    void register_tab(std::shared_ptr<Tab> tab);

    // Number of tabs.
    size_t count() const;

    // Access the tab at index. Returns nullptr if out of range.
    Tab* get(size_t index) const;

    // Snapshot the current list. Safe to hold while drawing, but
    // do not store the returned vector across frames — re-allocate
    // on the next registration.
    std::vector<Tab*> snapshot() const;

    // Clears all tabs. Used by shutdown.
    void clear();

private:
    TabRegistry() = default;

    mutable std::mutex                mu_;
    std::vector<std::shared_ptr<Tab>> tabs_;
};

} // namespace yamgg

#endif // YAMGG_UI_TAB_H
