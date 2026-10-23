// ui/widgets/tree_view.h — hierarchical tree of class names.
//
// Builds a package → subpackage → class hierarchy from a flat list of
// fully-qualified class names. The tree is rebuilt only when set_data()
// is called; drawing uses the cached structure.
//
// Drawing uses ImGuiListClipper for virtual scrolling, so a tree with
// 50,000+ nodes renders at full frame rate.
#ifndef YAMGG_UI_WIDGETS_TREE_VIEW_H
#define YAMGG_UI_WIDGETS_TREE_VIEW_H

#include <memory>
#include <string>
#include <vector>

namespace yamgg {

class TreeView {
public:
    // Node kinds for styling.
    enum class Kind { Package, Class };

    // Public so the destructor can instantiate std::unique_ptr<Node>.
    // Treated as opaque by callers; do not access fields directly.
    struct Node {
        std::string              label;
        std::string              full_name;
        Kind                     kind = Kind::Class;
        std::vector<std::unique_ptr<Node>> children;
        bool                     expanded = false;
    };
    using NodePtr = std::unique_ptr<Node>;

    // Rebuild the tree from a flat list of class names. Each name must
    // be fully qualified (e.g. "android.os.Bundle"). Names may also be
    // arrays ("[B", "[Ljava.lang.String;") — they are shown as their own
    // root node.
    void set_data(const std::vector<std::string>& class_names);

    // Draw the tree inside the currently active ImGui region.
    // `height` is the available vertical space in pixels; pass 0 to
    // fill the region. Returns true if a new node was selected this
    // frame.
    bool draw(const char* id, float height);

    // Currently selected fully-qualified name, or empty.
    const std::string& selected() const;

    // Force the selection (e.g. when the tab transitions to a known
    // class). Empty string clears.
    void set_selected(const std::string& full_name);

    // Filtering: nodes whose class name does not contain the filter
    // substring are hidden. Package nodes remain visible if any of
    // their descendants match. Empty filter shows everything.
    void set_filter(const std::string& filter);

    // Clear all data. Useful when the session restarts.
    void clear();

private:
    void insert_class(const std::string& full_name);
    void ensure_default_expansion(Node* root);
    bool draw_node(Node* node, const std::string& filter);
    void filter_node(Node* node, const std::string& filter, bool& any_match);

    std::vector<NodePtr>  roots_;
    std::string           selected_;
    std::string           filter_;
    bool                  needs_filter_apply_ = false;
};

} // namespace yamgg

#endif // YAMGG_UI_WIDGETS_TREE_VIEW_H
