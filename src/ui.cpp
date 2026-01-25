#include "ui.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <cstdio>

bool init_imgui(SDL_Window* window, SDL_GLContext gl_context) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL2_InitForOpenGL(window, gl_context)) {
        fprintf(stderr, "Failed to init ImGui SDL2 backend\n");
        return false;
    }

    if (!ImGui_ImplOpenGL3_Init("#version 130")) {
        fprintf(stderr, "Failed to init ImGui OpenGL3 backend\n");
        return false;
    }

    return true;
}

void shutdown_imgui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

void set_subscribed_recursive(DarttField& field, bool subscribed) {
    field.subscribed = subscribed;
    for (auto& child : field.children) {
        set_subscribed_recursive(child, subscribed);
    }
}

bool any_child_subscribed(const DarttField& field) {
    if (field.subscribed) return true;
    for (const auto& child : field.children) {
        if (any_child_subscribed(child)) return true;
    }
    return false;
}

bool all_children_subscribed(const DarttField& field) {
    if (field.children.empty()) {
        return field.subscribed;
    }
    for (const auto& child : field.children) {
        if (!all_children_subscribed(child)) return false;
    }
    return true;
}

// Forward declaration for recursive rendering
static bool render_field_row(DarttField& field, int depth);

// Render a single field row, returns true if value was edited
static bool render_field_row(DarttField& field, int depth) {
    bool value_edited = false;
    bool is_leaf = field.children.empty();

    ImGui::TableNextRow();

    // Column 0: Name (with tree indentation)
    ImGui::TableNextColumn();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth;
    if (is_leaf) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (field.expanded) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    // Use a unique ID based on pointer
    ImGui::PushID(&field);

    bool node_open = false;
    if (is_leaf) {
        // Leaf: just show name, no tree node behavior
        ImGui::TreeNodeEx(field.name.c_str(), flags);
        node_open = false;
    } else {
        // Parent: expandable tree node
        node_open = ImGui::TreeNodeEx(field.name.c_str(), flags);
        field.expanded = node_open;
    }

    // Column 1: Value
    ImGui::TableNextColumn();
    if (is_leaf) {
        // Editable value box
        ImGui::SetNextItemWidth(-FLT_MIN); // Fill column width

        // char buf[64];
        // std::string val_str = format_field_value(field);
        // snprintf(buf, sizeof(buf), "%s", val_str.c_str());

        // Different input types based on field type
        // Use IsItemDeactivatedAfterEdit() to detect when editing is complete
        switch (field.type) {
            case FieldType::FLOAT:
                ImGui::InputFloat("##val", &field.value.f32, 0, 0, "%.6f");
                if (ImGui::IsItemDeactivatedAfterEdit()) { value_edited = true; field.dirty = true; }
                break;
            case FieldType::INT32:
            case FieldType::ENUM:
                ImGui::InputInt("##val", &field.value.i32, 0, 0);
                if (ImGui::IsItemDeactivatedAfterEdit()) { value_edited = true; field.dirty = true; }
                break;
            case FieldType::UINT32:
            case FieldType::POINTER:
                ImGui::InputScalar("##val", ImGuiDataType_U32, &field.value.u32, NULL, NULL, "%u");
                if (ImGui::IsItemDeactivatedAfterEdit()) { value_edited = true; field.dirty = true; }
                break;
            case FieldType::INT16:
                ImGui::InputScalar("##val", ImGuiDataType_S16, &field.value.i16, NULL, NULL, "%d");
                if (ImGui::IsItemDeactivatedAfterEdit()) { value_edited = true; field.dirty = true; }
                break;
            case FieldType::UINT16:
                ImGui::InputScalar("##val", ImGuiDataType_U16, &field.value.u16, NULL, NULL, "%u");
                if (ImGui::IsItemDeactivatedAfterEdit()) { value_edited = true; field.dirty = true; }
                break;
            case FieldType::INT8:
                ImGui::InputScalar("##val", ImGuiDataType_S8, &field.value.i8, NULL, NULL, "%d");
                if (ImGui::IsItemDeactivatedAfterEdit()) { value_edited = true; field.dirty = true; }
                break;
            case FieldType::UINT8:
                ImGui::InputScalar("##val", ImGuiDataType_U8, &field.value.u8, NULL, NULL, "%u");
                if (ImGui::IsItemDeactivatedAfterEdit()) { value_edited = true; field.dirty = true; }
                break;
            case FieldType::DOUBLE:
                ImGui::InputDouble("##val", &field.value.f64, 0, 0, "%.6f");
                if (ImGui::IsItemDeactivatedAfterEdit()) { value_edited = true; field.dirty = true; }
                break;
            case FieldType::INT64:
                ImGui::InputScalar("##val", ImGuiDataType_S64, &field.value.i64, NULL, NULL, "%lld");
                if (ImGui::IsItemDeactivatedAfterEdit()) { value_edited = true; field.dirty = true; }
                break;
            case FieldType::UINT64:
                ImGui::InputScalar("##val", ImGuiDataType_U64, &field.value.u64, NULL, NULL, "%llu");
                if (ImGui::IsItemDeactivatedAfterEdit()) { value_edited = true; field.dirty = true; }
                break;
            default:
                ImGui::TextDisabled("???");
                break;
        }
    } else {
        // Parent node: show {...}
        ImGui::TextDisabled("{...}");
    }

    // Column 2: Subscribe checkbox
    ImGui::TableNextColumn();

    // For parent nodes, show mixed state if some but not all children subscribed
    if (!is_leaf) {
        bool all_sub = all_children_subscribed(field);
        bool any_sub = any_child_subscribed(field);

        // Mixed state: use a different visual
        if (any_sub && !all_sub) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.5f, 0.2f, 1.0f));
        }

        bool sub_state = all_sub;
        if (ImGui::Checkbox("##sub", &sub_state)) {
            // Toggle: if was mixed or off, turn all on; if all on, turn all off
            set_subscribed_recursive(field, !all_sub);
        }

        if (any_sub && !all_sub) {
            ImGui::PopStyleColor();
        }
    } else {
        if (ImGui::Checkbox("##sub", &field.subscribed)) {
            // Individual leaf subscription changed
        }
    }

    ImGui::PopID();

    // Render children if node is open
    if (node_open && !is_leaf) {
        for (auto& child : field.children) {
            if (render_field_row(child, depth + 1)) {
                value_edited = true;
            }
        }
        ImGui::TreePop();
    }

    return value_edited;
}

bool render_live_expressions(DarttConfig& config) {
    bool any_edited = false;

    ImGui::Begin("Live Expressions");

    // Show config info
    ImGui::Text("Symbol: %s", config.symbol.c_str());
    ImGui::Text("Address: %s (%u bytes)", config.address_str.c_str(), config.nbytes);
    ImGui::Separator();

    // Create table with 3 columns
    ImGuiTableFlags table_flags = ImGuiTableFlags_BordersV
                                | ImGuiTableFlags_BordersOuterH
                                | ImGuiTableFlags_Resizable
                                | ImGuiTableFlags_RowBg
                                | ImGuiTableFlags_NoBordersInBody;

    if (ImGui::BeginTable("fields_table", 3, table_flags)) {
        // Setup columns
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Sub", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableHeadersRow();

        // Render the root node (e.g., "gl_dp") as the top-level entry
        if (render_field_row(config.root, 0)) {
            any_edited = true;
        }

        ImGui::EndTable();
    }

    ImGui::End();

    return any_edited;
}
