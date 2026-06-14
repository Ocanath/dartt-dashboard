#include "ui.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <cstdio>
#include <vector>
#include <string>
#include "colors.h"
#include "dartt_link.h"
#include "time_util.h"

bool init_imgui(SDL_Window* window, SDL_GLContext gl_context) 
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL2_InitForOpenGL(window, gl_context)) 
	{
        fprintf(stderr, "Failed to init ImGui SDL2 backend\n");
        return false;
    }

    if (!ImGui_ImplOpenGL3_Init("#version 130")) 
	{
        fprintf(stderr, "Failed to init ImGui OpenGL3 backend\n");
        return false;
    }

    return true;
}

void shutdown_imgui() 
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

void set_subscribed_all(DarttField* root, bool subscribed) 
{
    std::vector<DarttField*> stack;
    stack.push_back(root);

    while (!stack.empty()) 
	{
        DarttField* field = stack.back();
        stack.pop_back();

        field->subscribed = subscribed;

        for (size_t i = 0; i < field->children.size(); i++) 
		{
            stack.push_back(&field->children[i]);
        }
    }
}

bool any_child_subscribed(const DarttField* root) 
{
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
struct RenderWork 
{
    DarttField* field;
    bool is_tree_pop;  // true = just call TreePop(), no rendering
};

/** @brief single dartt field display value helper
 * @param input pointer to leaf
 */
void calculate_display_value(DarttField * leaf)
{
	if(leaf == NULL)
	{
		return;
	}
	if(leaf->subscribed)
	{
		switch (leaf->type) 
		{
			case FieldType::FLOAT:
			{
				leaf->display_value = leaf->value.f32 * leaf->display_scale;
				break;
			}
			case FieldType::INT32:
			{
				leaf->display_value = ((float)leaf->value.i32)*leaf->display_scale;
				break;
			}
			case FieldType::UINT32:
			{
				leaf->display_value = ((float)leaf->value.u32)*leaf->display_scale;
				break;
			}
			case FieldType::INT16:
			{
				leaf->display_value = ((float)leaf->value.i16) * leaf->display_scale;
				break;
			}
			case FieldType::UINT16:
			{
				leaf->display_value = ((float)leaf->value.u16) * leaf->display_scale;
				break;
			}
			case FieldType::INT8:
			{
				leaf->display_value = ((float)leaf->value.i8) * leaf->display_scale;
				break;
			}
			case FieldType::UINT8:
			{
				leaf->display_value = ((float)leaf->value.u8) * leaf->display_scale;
				break;
			}
			case FieldType::DOUBLE:
			{
				leaf->display_value = (float)leaf->value.f64 * leaf->display_scale;
				break;
			}
			case FieldType::INT64:
			{
				leaf->display_value = ((float)leaf->value.i64) * leaf->display_scale;
				break;
			}
			case FieldType::UINT64:
			{
				leaf->display_value = ((float)leaf->value.u64) * leaf->display_scale;
				break;
			}
			default:
				break;
		}
	}
}

void calculate_display_values(const std::vector<DarttField*> &leaf_list)
{
	for(int i = 0; i < leaf_list.size(); i++)
	{
		DarttField * leaf = leaf_list[i];
		calculate_display_value(leaf);
	}
}

/**
*/
void float_field_handler(DarttField* field)
{
	if(field == NULL)
	{
		return;
	}
	if (field->use_display_scale == false)
	{
		ImGui::InputFloat("##val", &field->value.f32, 0, 0, "%f");
		field->state.dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->state.dirty = true;
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
		field->state.dirty = ImGui::IsItemDeactivatedAfterEdit(); 
	}
	else
	{
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit()) 
		{ 
			field->state.dirty = true; 
			field->value.i32 = (int32_t)(field->display_value/field->display_scale);
		}
	}
}

void uint32_field_handler(DarttField * field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputScalar("##val", ImGuiDataType_U32, &field->value.u32, 0, 0);
		field->state.dirty = ImGui::IsItemDeactivatedAfterEdit(); 
	}
	else
	{
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit()) 
		{ 
			field->state.dirty = true; 
			field->value.i32 = (uint32_t)(field->display_value/field->display_scale);
		}
	}
}


void int16_field_handler(DarttField* field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputScalar("##val", ImGuiDataType_S16, &field->value.i16, 0, 0);
		field->state.dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->state.dirty = true;
			field->value.i16 = (int16_t)(field->display_value / field->display_scale);
		}
	}
}

void uint16_field_handler(DarttField* field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputScalar("##val", ImGuiDataType_U16, &field->value.u16, 0, 0);
		field->state.dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->state.dirty = true;
			field->value.u16 = (uint16_t)(field->display_value / field->display_scale);
		}
	}
}

void int8_field_handler(DarttField* field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputScalar("##val", ImGuiDataType_S8, &field->value.i8, 0, 0);
		field->state.dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->state.dirty = true;
			field->value.i8 = (int8_t)(field->display_value / field->display_scale);
		}
	}
}

void uint8_field_handler(DarttField* field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputScalar("##val", ImGuiDataType_U8, &field->value.u8, 0, 0);
		field->state.dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->state.dirty = true;
			field->value.u8 = (uint8_t)(field->display_value / field->display_scale);
		}
	}
}

void double_field_handler(DarttField* field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputDouble("##val", &field->value.f64, 0, 0, "%f");
		field->state.dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->state.dirty = true;
			field->value.f64 = (double)(field->display_value / field->display_scale);
		}
	}
}

void int64_field_handler(DarttField* field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputScalar("##val", ImGuiDataType_S64, &field->value.i64, 0, 0);
		field->state.dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->state.dirty = true;
			field->value.i64 = (int64_t)(field->display_value / field->display_scale);
		}
	}
}

void uint64_field_handler(DarttField* field)
{
	if(field->use_display_scale == false)
	{
		ImGui::InputScalar("##val", ImGuiDataType_U64, &field->value.u64, 0, 0);
		field->state.dirty = ImGui::IsItemDeactivatedAfterEdit();
	}
	else
	{
		ImGui::InputScalar("###val", ImGuiDataType_Float, &field->display_value, 0, 0, "%f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			field->state.dirty = true;
			field->value.u64 = (uint64_t)(field->display_value / field->display_scale);
		}
	}
}

// Render a single field's row (called from iterative loop)
static bool render_single_field(DarttField* field, bool show_display_props, bool& sub_changed)
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
            sub_changed = true;
        }

        if (any_sub && !all_sub) {
            ImGui::PopStyleColor();
        }
    } 
	else 
	{
        if (ImGui::Checkbox("##sub", &field->subscribed))
		{
            sub_changed = true;
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

    return field->state.dirty;
}

// Render field tree iteratively, returns true if any value was edited
static bool render_field_tree(DarttField* root, bool show_display_props, bool& sub_changed)
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
        if (render_single_field(work.field, show_display_props, sub_changed))
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

// Render a tree selector for choosing a DarttField
// Returns selected field pointer, or nullptr if no selection made
static DarttField* render_field_selector_tree(DarttField* root)
{
	DarttField* selected = nullptr;
	std::vector<RenderWork> stack;
	stack.push_back({root, false});

	while (!stack.empty())
	{
		RenderWork work = stack.back();
		stack.pop_back();

		if (work.is_tree_pop)
		{
			ImGui::TreePop();
			continue;
		}

		DarttField* field = work.field;
		bool is_leaf = field->children.empty();

		ImGui::PushID(field);

		if (is_leaf)
		{
			// Leaf node - selectable if subscribed, grayed out otherwise
			if (field->subscribed)
			{
				if (ImGui::Selectable(field->name.c_str(), false))
				{
					selected = field;
				}
			}
			else
			{
				ImGui::TextDisabled("%s", field->name.c_str());
			}
		}
		else
		{
			// Parent node - expandable tree node
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
			if (field->expanded)
			{
				flags |= ImGuiTreeNodeFlags_DefaultOpen;
			}

			bool node_open = ImGui::TreeNodeEx(field->name.c_str(), flags);
			field->expanded = node_open;

			if (node_open)
			{
				// Push TreePop marker
				stack.push_back({nullptr, true});

				// Push children in reverse order
				for (size_t i = field->children.size(); i > 0; i--)
				{
					stack.push_back({&field->children[i - 1], false});
				}
			}
		}

		ImGui::PopID();
	}

	return selected;
}

bool render_plotting_menu(Plotter &plot, DarttField& root, const std::vector<DarttField*> &subscribed_list)
{
	ImGui::Begin("Plot Settings");
	
	// Add line button
	if (ImGui::SmallButton("+"))
	{
		std::lock_guard<std::mutex> lock(plot.plot_mutex);
		plot.lines.push_back(Line());
		plot.lines.back().xsource = &plot.sys_sec;
		int color_index = (plot.lines.size() % NUM_COLORS);
		plot.lines.back().color = template_colors[color_index];
		//consider automatic "Clear" here - will look more professional (and it's really easy to implement), but does wipe data
	}
	ImGui::SameLine();
	ImGui::Text("Add Line");

	// Right-align Clear button
	float clear_width = ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2;
	ImGui::SameLine(ImGui::GetWindowWidth() - clear_width - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("Clear"))
	{
		std::lock_guard<std::mutex> lock(plot.plot_mutex);
		for (size_t i = 0; i < plot.lines.size(); i++)
		{
			plot.lines[i].clear();
		}
	}
	ImGui::Separator();
	
	int line_to_remove = -1;
	for (size_t line_idx = 0; line_idx < plot.lines.size(); line_idx++)
	{
		std::lock_guard<std::mutex> lock(plot.plot_mutex);
		Line& line = plot.lines[line_idx];
		ImGui::PushID((int)line_idx);

		// Line header with remove button (right-aligned)
		char header_label[32];
		snprintf(header_label, sizeof(header_label), "Line %zu", line_idx);
		bool open = ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_AllowOverlap);
		float minus_width = ImGui::CalcTextSize("-").x + ImGui::GetStyle().FramePadding.x * 2;
		ImGui::SameLine(ImGui::GetWindowWidth() - minus_width - ImGui::GetStyle().WindowPadding.x);
		if (ImGui::SmallButton("-"))
		{
			line_to_remove = (int)line_idx;
		}

		if (!open)
		{
			ImGui::PopID();
			continue;
		}

		// Mode selection via radio buttons
		int mode = (int)line.mode;
		if (ImGui::RadioButton("Time Mode", &mode, TIME_MODE))
		{
			line.mode = TIME_MODE;
			// Default to sys_sec if no X source assigned
			if (line.xsource == nullptr)
			{
				line.xsource = &plot.sys_sec;
			}
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("XY Mode", &mode, XY_MODE))
		{
			line.mode = XY_MODE;
		}

		// X source combo with tree selector
		ImGui::Text("X Source:");
		ImGui::SameLine();
		const char* x_preview = "None";
		if (line.xsource == &plot.sys_sec)
		{
			x_preview = "sys_sec";
		}
		else if (line.xsource != nullptr)
		{
			// Find field name by pointer
			for (size_t i = 0; i < subscribed_list.size(); i++)
			{
				if (&subscribed_list[i]->display_value == line.xsource)
				{
					x_preview = subscribed_list[i]->name.c_str();
					break;
				}
			}
		}
		ImGui::SetNextItemWidth(150.0f);
		if (ImGui::BeginCombo("##xsrc", x_preview))
		{
			if (ImGui::Selectable("sys_sec", line.xsource == &plot.sys_sec))
			{
				line.xsource = &plot.sys_sec;
			}
			ImGui::Separator();
			DarttField* selected = render_field_selector_tree(&root);
			if (selected)
			{
				line.xsource = &selected->display_value;
			}
			ImGui::EndCombo();
		}
		
		if(line.mode == XY_MODE)
		{
			ImGui::SameLine();
			ImGui::Text("Xscale:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(60.0f);
			ImGui::InputFloat("##xscale", &line.xscale, 0, 0, "%.2f");
			ImGui::SameLine();
			ImGui::Text("Xoff:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(60.0f);
			ImGui::InputFloat("##xoffset", &line.xoffset, 0, 0, "%.2f");
		}

		// Y source combo with tree selector
		ImGui::Text("Y Source:");
		ImGui::SameLine();
		const char* y_preview = "None";
		if (line.ysource != nullptr)
		{
			for (size_t i = 0; i < subscribed_list.size(); i++)
			{
				if (&subscribed_list[i]->display_value == line.ysource)
				{
					y_preview = subscribed_list[i]->name.c_str();
					break;
				}
			}
		}
		ImGui::SetNextItemWidth(150.0f);
		if (ImGui::BeginCombo("##ysrc", y_preview))
		{
			DarttField* selected = render_field_selector_tree(&root);
			if (selected)
			{
				line.ysource = &selected->display_value;
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::Text("Yscale:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(60.0f);
		ImGui::InputFloat("##yscale", &line.yscale, 0, 0, "%.2f");
		ImGui::SameLine();
		ImGui::Text("Yoff:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(60.0f);
		ImGui::InputFloat("##yoffset", &line.yoffset, 0, 0, "%.2f");

		ImGui::Text("Buffer Size");
		ImGui::SameLine();
		ImGui::InputScalar("##buffsersize", ImGuiDataType_U32, &line.enqueue_cap, 0, 0, "%d");

		// Color picker
		ImGui::Text("Color:");
		ImGui::SameLine();
		float col[4] = {line.color.r/255.f, line.color.g/255.f, line.color.b/255.f, line.color.a/255.f};
		if (ImGui::ColorEdit4("##color", col, ImGuiColorEditFlags_NoInputs))
		{
			line.color.r = (uint8_t)(col[0] * 255);
			line.color.g = (uint8_t)(col[1] * 255);
			line.color.b = (uint8_t)(col[2] * 255);
			line.color.a = (uint8_t)(col[3] * 255);
		}

		ImGui::Spacing();
		ImGui::PopID();
	}

	// Remove line after loop to avoid iterator invalidation
	if (line_to_remove >= 0 && line_to_remove < (int)plot.lines.size())
	{
		std::lock_guard<std::mutex> lock(plot.plot_mutex);
		plot.lines.erase(plot.lines.begin() + line_to_remove);
	}

	ImGui::End();
	return true;
}

bool render_live_expressions(DarttConfig& config, Plotter& plot, const std::string& config_json_path, DarttLink & dl, WavWriter & wav, DataLogger& data_logger)
{
    bool any_edited = false;

    ImGui::Begin("Live Expressions");

	int frame_format = dl.msg_type;
	ImGui::RadioButton("TYPE_SERIAL_MESSAGE", &frame_format, TYPE_SERIAL_MESSAGE);
	ImGui::SameLine();
	ImGui::RadioButton("TYPE_ADDR_MSG", &frame_format, TYPE_ADDR_MESSAGE);
	if(dl.msg_type != frame_format)
	{
		config.subscribed_dirty = true;	//gotta flag out a rebuild, since the read request frame format is stale upon change
	}
	dl.msg_type = (serial_message_type_t)frame_format;

	if(dl.msg_type == TYPE_SERIAL_MESSAGE)
	{
		ImGui::Text("Dartt Address: ");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(50);
		unsigned char prev_address = dl.address;
		ImGui::InputScalar("##dartt_address", ImGuiDataType_U8, &dl.address);
		if (dl.address != prev_address)
			config.subscribed_dirty = true;
	}

	{
		ImGui::SameLine();
		ImGui::Text("Baudrate: ");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80);
		static uint32_t baudrate = 115200;
		ImGui::InputScalar("##baudrate", ImGuiDataType_U32, &baudrate);
		if (dl.serial.connected() && !ImGui::IsItemActive())
			baudrate = (uint32_t)dl.serial.get_baud_rate();
		ImGui::SameLine();
		if (dl.serial.connected())
		{
			if (ImGui::Button("Disconnect"))
				dl.serial.disconnect();
		}
		else
		{
			if (ImGui::Button("Connect"))
				dl.serial.autoconnect(baudrate);
		}
	}

	ImGui::Text("Dartt blob offset: ");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(50);
	ImGui::InputScalar("##dartt_base_offset", ImGuiDataType_U8, &dl.base_offset);

    // Show config info
    ImGui::Text("Symbol: %s", config.symbol.c_str());
    ImGui::Text("Address: %s (%u bytes)", config.address_str.c_str(), config.nbytes);
    bool save_clicked = ImGui::Button("Save");
	if(save_clicked)
	{
		save_dartt_config(config_json_path.c_str(), config, plot, dl);
	}
	
	{
		float sps = (float)(config.num_frames) / (float)(config.elapsed_ms) * 1000.f;
		size_t nsymbols = config.subscribed_list.size();
		float fps = nsymbols > 0 ? sps / (float)nsymbols : 0.f;
		ImGui::Text("SPS: %.1f  FPS: %.1f", sps, fps);
		ImGui::SameLine();
		if(ImGui::Button("Clear Counters"))
		{
			config.num_frames = 0;
			time_start();
		}
	}

	ImGui::SameLine();
	ImGui::Separator();
	if(!wav.is_open())
	{
		if(ImGui::Button("Start Wav (All)"))
		{
			wav.open("output.wav");
		}
	}
	else
	{
		if(ImGui::Button("Close Wav (ALL)"))
		{
			float fps = (float)(config.num_frames)/(float)(config.elapsed_ms) * 1000.f;
			wav.close(fps);
		}
	}

	ImGui::SameLine();
	if (!data_logger.is_running())
	{
		if (ImGui::Button("Start Log"))
		{
			data_logger.start();
			config.subscribed_dirty = true;
		}
	}
	else
	{
		if (ImGui::Button("Stop Log"))
		{
			data_logger.stop();
			for (size_t i = 0; i < config.subscribed_list.size(); i++)
			{
				config.subscribed_list[i]->log_ring = nullptr;
			}
			data_logger.clear_channels();
		}
	}

	static bool show_display_props = false;
	ImGui::Checkbox("Display Properties", &show_display_props);

	ImGui::SameLine();
	//streaming mode toggle
	if(ImGui::Checkbox("Streaming Mode", &dl.streaming_mode))
	{
		if(dl.streaming_mode)
		{
			dl.clear_subscriptions();
		}
		config.subscribed_dirty = true;	//flag out a deferred read request queue rebuild
	}


	ImGui::Separator();

    // Create table with 4 or 5 columns (Plot column always present)
    ImGuiTableFlags table_flags = ImGuiTableFlags_BordersV
                                | ImGuiTableFlags_BordersOuterH
                                | ImGuiTableFlags_Resizable
                                | ImGuiTableFlags_RowBg
                                | ImGuiTableFlags_NoBordersInBody;
	int num_columns = show_display_props ? 4 : 3;
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
        bool sub_changed = false;
        if (render_field_tree(&config.root, show_display_props, sub_changed)) {
            any_edited = true;
        }
        config.subscribed_dirty |= sub_changed;

        ImGui::EndTable();
    }

    ImGui::End();

    return any_edited;
}

bool render_elf_load_popup(bool* show, const std::string& elf_path,
                           char* var_name_buf, size_t buf_size,
                           std::string& error_msg)
{
    bool load_requested = false;

    if (*show)
    {
        ImGui::OpenPopup("Load ELF");
        *show = false;  // Only open once; modal stays open until dismissed
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Load ELF", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("File:");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", elf_path.c_str());

        ImGui::Separator();

        ImGui::Text("Variable name:");
        ImGui::SetNextItemWidth(-FLT_MIN);
        bool enter_pressed = ImGui::InputText("##varname", var_name_buf, buf_size,
                                               ImGuiInputTextFlags_EnterReturnsTrue);

        // Focus the text input on first appearance
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere(-1);
        }

        // Error message
        if (!error_msg.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::TextWrapped("%s", error_msg.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Separator();

        bool name_valid = (var_name_buf[0] != '\0');

        if (!name_valid)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Load", ImVec2(120, 0)) || (enter_pressed && name_valid))
        {
            load_requested = true;
            // Don't close popup yet - caller decides based on success/failure
        }
        if (!name_valid)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            error_msg.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    return load_requested;
}
