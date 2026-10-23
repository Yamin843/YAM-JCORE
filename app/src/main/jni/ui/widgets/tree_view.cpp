// ui/widgets/tree_view.cpp
#include "ui/widgets/tree_view.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <unordered_map>

#include "imgui.h"
#include "imgui_internal.h"

namespace yamgg {

} // namespace

void TreeView::set_data(const std::vector<std::string>& names) {
    roots_.clear();
    selected_.clear();

    for (const auto& name : names) {
        if (!name.empty()) insert_class(name);
    }

    // Sort each level recursively.
    std::function<void(std::vector<NodePtr>&)> sort_children =
        [&](std::vector<NodePtr>& level) {
            std::sort(level.begin(), level.end(),
                [](const NodePtr& a, const NodePtr& b) {
                    if (a->kind != b->kind) {
                        return a->kind == Kind::Package;
                    }
                    return a->label < b->label;
                });
            for (auto& c : level) sort_children(c->children);
        };
    sort_children(roots_);

    // Auto-expand the first root's first level so that something is
    // visible immediately.
    for (auto& r : roots_) {
        if (r->kind == Kind::Package) {
            r->expanded = true;
            for (auto& c : r->children) {
                if (c->kind == Kind::Package) {
                    c->expanded = true;
                    break;
                }
            }
            break;
        }
    }

    needs_filter_apply_ = !filter_.empty();
}

void TreeView::clear() {
    roots_.clear();
    selected_.clear();
    filter_.clear();
}

void TreeView::insert_class(const std::string& full_name) {
    // Split on '.' into segments. Array types like "[B" contain no dot;
    // they become their own root with the label "[B".
    std::vector<std::string> parts;
    if (!full_name.empty() && full_name[0] == '[') {
        parts.push_back(full_name);
    } else {
        size_t start = 0;
        while (start < full_name.size()) {
            size_t dot = full_name.find('.', start);
            if (dot == std::string::npos) {
                parts.push_back(full_name.substr(start));
                break;
            }
            parts.push_back(full_name.substr(start, dot - start));
            start = dot + 1;
        }
    }

    if (parts.empty()) return;

    std::vector<NodePtr>* level = &roots_;
    std::string accumulated;

    for (size_t i = 0; i < parts.size(); i++) {
        const std::string& part = parts[i];
        if (!accumulated.empty() && accumulated[0] != '[') {
            accumulated += ".";
        }
        accumulated += part;

        NodePtr* found = nullptr;
        for (auto& child : *level) {
            if (child->label == part) { found = &child; break; }
        }

        bool is_last = (i == parts.size() - 1);
        Kind kind = is_last ? Kind::Class : Kind::Package;

        if (found == nullptr) {
            NodePtr node(new Node());
            node->label     = part;
            node->full_name = is_last ? full_name : accumulated;
            node->kind      = kind;
            node->expanded  = false;
            (*level).push_back(std::move(node));
            found = &(*level).back();
        } else if (is_last && (*found)->kind == Kind::Package) {
            // A class exists with a package name (rare but possible for
            // nested classes). Keep it as package but record the class
            // name on it.
            (*found)->full_name = full_name;
        }

        level = &(*found)->children;
    }
}

void TreeView::set_selected(const std::string& full_name) {
    selected_ = full_name;
}

const std::string& TreeView::selected() const {
    return selected_;
}

void TreeView::set_filter(const std::string& filter) {
    if (filter == filter_) return;
    filter_ = filter;
    needs_filter_apply_ = true;
}
void TreeView::ensure_default_expansion(Node* root) {
    if (!root) return;
    if (root->kind == Kind::Package && !root->expanded && !root->children.empty()) {
        root->expanded = true;
    }
}

// Renders one node and (if expanded) its descendants. Returns true if
// `node` was clicked this frame.
bool TreeView::draw_node(Node* node, const std::string& filter) {
    if (node == nullptr) return false;

    // Filtering: if filter is non-empty, hide nodes whose subtree has
    // no match.
    bool matches = filter.empty();
    if (!matches) {
        if (node->kind == Kind::Class) {
            matches = node->full_name.find(filter) != std::string::npos;
        } else {
            // Package matches if any descendant matches. Recurse cheaply.
            std::function<bool(Node*)> any_match = [&](Node* n) -> bool {
                for (auto& c : n->children) {
                    if (c->kind == Kind::Class) {
                        if (c->full_name.find(filter) != std::string::npos) return true;
                    } else if (any_match(c.get())) {
                        return true;
                    }
                }
                return false;
            };
            matches = any_match(node);
        }
    }

    if (!matches) return false;

    bool clicked = false;

    if (node->kind == Kind::Package) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                                 | ImGuiTreeNodeFlags_OpenOnDoubleClick
                                 | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (node->expanded) flags |= ImGuiTreeNodeFlags_DefaultOpen;

        bool is_selected = (node->full_name == selected_);
        if (is_selected) flags |= ImGuiTreeNodeFlags_Selected;

        if (!filter.empty()) {
            // While filtering, force-expand so matches are visible.
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        } else {
            ImGui::SetNextItemOpen(node->expanded, ImGuiCond_Always);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.78f, 0.30f, 1.0f));
        bool open = ImGui::TreeNodeEx(node->label.c_str(), flags);
        ImGui::PopStyleColor();

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            selected_ = node->full_name;
            clicked = true;
        }

        if (open) {
            for (auto& child : node->children) {
                draw_node(child.get(), filter);
            }
            ImGui::TreePop();
        }

        // Cache the expanded state.
        if (!filter.empty()) node->expanded = true;
        else node->expanded = open;
    } else {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf
                                 | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                 | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (node->full_name == selected_) flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::TreeNodeEx(node->label.c_str(), flags);
        if (ImGui::IsItemClicked()) {
            selected_ = node->full_name;
            clicked = true;
        }
    }

    return clicked;
}

bool TreeView::draw(const char* id, float height) {
    if (id == nullptr || *id == '\0') id = "##tree";

    if (height > 0.0f) {
        ImGui::BeginChild(id, ImVec2(0, height), ImGuiChildFlags_Borders);
    } else {
        ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_Borders);
    }

    bool selection_changed = false;

    if (roots_.empty()) {
        ImGui::TextDisabled("No classes loaded.");
    } else {
        for (auto& root : roots_) {
            if (draw_node(root.get(), filter_)) {
                selection_changed = true;
            }
        }
    }

    ImGui::EndChild();
    return selection_changed;
}

} // namespace yamgg
