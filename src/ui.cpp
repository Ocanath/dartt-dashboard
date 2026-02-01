#include "ui.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <cstdio>
#include <vector>

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

void set_subscribed_all(DarttField* root, bool subscribed) {
    std::vector<DarttField*> stack;
    stack.push_back(root);

    while (!stack.empty()) {
        DarttField* field = stack.back();
        stack.pop_back();

        field->subscribed = subscribed;

        for (size_t i = 0; i < field->children.size(); i++) {
            stack.push_back(&field->children[i]);
        }
    }
}

bool any_child_subscribed(const DarttField* root) {
    std::vector<const DarttField*> stack;
    stack.push_back(root);

    while (!stack.empty()) {
        const DarttField* field = stack.back();
        stack.pop_back();

        if (field->subscribed) {
            return true;
        }

        for (size_t i = 0; i < field->children.size(); i++) {
            stack.push_back(&field->children[i]);
        }
    }
    return false;
}

bool all_children_subscribed(const DarttField* root) {
    std::vector<const DarttField*> stack;
    stack.push_back(root);

    while (!stack.empty()) {
        const DarttField* field = stack.back();
        stack.pop_back();

        // Only check leaves
        if (field->children.empty()) {
            if (!field->subscribed) {
                return false;
            }
        } else {
            for (size_t i = 0; i < field->children.size(); i++) {
                stack.push_back(&field->children[i]);
            }
        }
    }
    return true;
}

// Work item for iterative field rendering
struct RenderWork {
    DarttField* field;
    bool is_tree_pop;  // true = just call TreePop(), no rendering
};


void float_field_handler(DarttField* field)
{
	if(field == NULL)
	{
		return;
	}
	if (field->use_display_scale == false)
	{
		ImGui::InputFloat("##val", &field->value.f32, 0, 0, "%f");
		field->dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		field->display_value = field->value.f32 * field->display_scale;
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->dirty = true;
			field->value.f32 = (float)(field->display_value / field->display_scale);
		}
	}
}

void int32_field_handler(DarttField * field)
{
	if(field == NULL)
	{
		return;
	}

	if(field->use_display_scale == false)
	{
		ImGui::InputScalar("##val", ImGuiDataType_S32, &field->value.i32, 0, 0);
		field->dirty = ImGui::IsItemDeactivatedAfterEdit(); 
	}
	else
	{
		field->display_value = ((float)field->value.i32)*field->display_scale;
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit()) 
		{ 
			field->dirty = true; 
			field->value.i32 = (int32_t)(field->display_value/field->display_scale);
		}
	}
}

void uint32_field_handler(DarttField * field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputScalar("##val", ImGuiDataType_U32, &field->value.u32, 0, 0);
		field->dirty = ImGui::IsItemDeactivatedAfterEdit(); 
	}
	else
	{
		field->display_value = ((float)field->value.u32)*field->display_scale;
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit()) 
		{ 
			field->dirty = true; 
			field->value.i32 = (uint32_t)(field->display_value/field->display_scale);
		}
	}
}


void int16_field_handler(DarttField* field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputScalar("##val", ImGuiDataType_S16, &field->value.i16, 0, 0);
		field->dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		field->display_value = ((float)field->value.i16) * field->display_scale;
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->dirty = true;
			field->value.i16 = (int16_t)(field->display_value / field->display_scale);
		}
	}
}

void uint16_field_handler(DarttField* field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputScalar("##val", ImGuiDataType_U16, &field->value.u16, 0, 0);
		field->dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		field->display_value = ((float)field->value.u16) * field->display_scale;
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->dirty = true;
			field->value.u16 = (uint16_t)(field->display_value / field->display_scale);
		}
	}
}

void int8_field_handler(DarttField* field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputScalar("##val", ImGuiDataType_S8, &field->value.i8, 0, 0);
		field->dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		field->display_value = ((float)field->value.i8) * field->display_scale;
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->dirty = true;
			field->value.i8 = (int8_t)(field->display_value / field->display_scale);
		}
	}
}

void uint8_field_handler(DarttField* field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputScalar("##val", ImGuiDataType_U8, &field->value.u8, 0, 0);
		field->dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		field->display_value = ((float)field->value.u8) * field->display_scale;
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->dirty = true;
			field->value.u8 = (uint8_t)(field->display_value / field->display_scale);
		}
	}
}

void double_field_handler(DarttField* field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputDouble("##val", &field->value.f64, 0, 0, "%f");
		field->dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		field->display_value = (float)field->value.f64 * field->display_scale;
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->dirty = true;
			field->value.f64 = (double)(field->display_value / field->display_scale);
		}
	}
}

void int64_field_handler(DarttField* field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputScalar("##val", ImGuiDataType_S64, &field->value.i64, 0, 0);
		field->dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		field->display_value = ((float)field->value.i64) * field->display_scale;
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->dirty = true;
			field->value.i64 = (int64_t)(field->display_value / field->display_scale);
		}
	}
}

void uint64_field_handler(DarttField* field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputScalar("##val", ImGuiDataType_U64, &field->value.u64, 0, 0);
		field->dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		field->display_value = ((float)field->value.u64) * field->display_scale;
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->dirty = true;
			field->value.u64 = (uint64_t)(field->display_value / field->display_scale);
		}
	}
}

// Render a single field's row (called from iterative loop)
static bool render_single_field(DarttField* field, bool show_display_props) 
{
    bool is_leaf = field->children.empty();

    ImGui::TableNextRow();

    // Column 0: Name (with tree indentation)
    ImGui::TableNextColumn();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth;
    if (is_leaf) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (field->expanded) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    // Use a unique ID based on pointer
    ImGui::PushID(field);

    bool node_open = false;
    if (is_leaf) 
	{
        // Leaf: just show name, no tree node behavior
        ImGui::TreeNodeEx(field->name.c_str(), flags);
        node_open = false;
    } 
	else 
	{
        // Parent: expandable tree node
        node_open = ImGui::TreeNodeEx(field->name.c_str(), flags);
        field->expanded = node_open;
    }

    // Column 1: Value
    ImGui::TableNextColumn();
    if (is_leaf) 
	{
        // Editable value box
        ImGui::SetNextItemWidth(-FLT_MIN); // Fill column width

        // Different input types based on field type
        switch (field->type) 
		{
            case FieldType::FLOAT:
			{
				float_field_handler(field);
				break;
			}
            case FieldType::INT32:
			{
				int32_field_handler(field);
                break;
			}
            case FieldType::UINT32:
			{
				uint32_field_handler(field);
                break;
			}
            case FieldType::INT16:
			{
				int16_field_handler(field);
				break;
			}
            case FieldType::UINT16:
			{
				uint16_field_handler(field);
				break;
			}
            case FieldType::INT8:
			{
				int8_field_handler(field);
				break;
			}
            case FieldType::UINT8:
			{
				uint8_field_handler(field);
				break;
			}
            case FieldType::DOUBLE:
			{
				double_field_handler(field);
				break;
			}
            case FieldType::INT64:
			{
				int64_field_handler(field);
				break;
			}
            case FieldType::UINT64:
			{
				uint64_field_handler(field);
				break;
			}
			case FieldType::ENUM:
			{
				int32_field_handler(field);
				break;
			}
            default:
                ImGui::TextDisabled("???");
                break;
        }
    } 
	else 
	{
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
            set_subscribed_all(field, !all_sub);
        }

        if (any_sub && !all_sub) {
            ImGui::PopStyleColor();
        }
    } 
	else 
	{
        if (ImGui::Checkbox("##sub", &field->subscribed)) 
		{
            // Individual leaf subscription changed
        }
    }

	/*Handle the display scale value entry box*/
	if (show_display_props)
	{
		ImGui::TableNextColumn();
		ImGui::Checkbox("##native_type", &field->use_display_scale);
		ImGui::SameLine();
		ImGui::InputFloat("##displayscale", &field->display_scale, 0, 0, "%g");
	}
	
    ImGui::PopID();

    return field->dirty;
}

// Render field tree iteratively, returns true if any value was edited
static bool render_field_tree(DarttField* root, bool show_display_props)
{
    bool any_edited = false;
    std::vector<RenderWork> stack;

    stack.push_back({root, false});

    while (!stack.empty())
	{
        RenderWork work = stack.back();
        stack.pop_back();

        // TreePop marker - just pop and continue
        if (work.is_tree_pop)
		{
            ImGui::TreePop();
            continue;
        }

        bool is_leaf = work.field->children.empty();

        // Render this field's row
        if (render_single_field(work.field, show_display_props))
		{
            any_edited = true;
        }

        // If node is open and has children, queue them
        if (work.field->expanded && !is_leaf) 
		{
            // Push TreePop marker first (will be processed after children)
            stack.push_back({NULL, true});

            // Push children in reverse order so first child renders first
            for (size_t i = work.field->children.size(); i > 0; i--) 
			{
                stack.push_back({&work.field->children[i - 1], false});
            }
        }
    }

    return any_edited;
}

bool render_live_expressions(DarttConfig& config) {
    bool any_edited = false;

    ImGui::Begin("Live Expressions");

    // Show config info
    ImGui::Text("Symbol: %s", config.symbol.c_str());
    ImGui::Text("Address: %s (%u bytes)", config.address_str.c_str(), config.nbytes);
    bool save_clicked = ImGui::Button("Save");
	if(save_clicked)
	{
		save_dartt_config("config.json", config);
	}

	ImGui::SameLine();
	static bool show_display_props = false;
	ImGui::Checkbox("Display Properties", &show_display_props);

	ImGui::Separator();

    // Create table with 3 or 4 columns
    ImGuiTableFlags table_flags = ImGuiTableFlags_BordersV
                                | ImGuiTableFlags_BordersOuterH
                                | ImGuiTableFlags_Resizable
                                | ImGuiTableFlags_RowBg
                                | ImGuiTableFlags_NoBordersInBody;
	//TODO: maybe no magic number here - although it's not that bad
	int num_columns = 3;
	if(show_display_props)
	{
		num_columns = 4;
	}
	// int num_columns = show_display_props ? 4 : 3;
    if (ImGui::BeginTable("fields_table", num_columns, table_flags))
	{
        // Setup columns
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Sub", ImGuiTableColumnFlags_WidthFixed, 40.0f);
		if (show_display_props)
		{
			ImGui::TableSetupColumn("Scale", ImGuiTableColumnFlags_WidthFixed, 100.0f);
		}
        ImGui::TableHeadersRow();

        // Render the field tree iteratively
        if (render_field_tree(&config.root, show_display_props)) {
            any_edited = true;
        }

        ImGui::EndTable();
    }

    ImGui::End();

    return any_edited;
}
