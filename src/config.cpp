#include "config.h"
#include "dartt_init.h"
#include "plotting.h"
#include <fstream>
#include <cstdio>
#include <cstring>
#include <vector>

using json = nlohmann::json;

// Work item for iterative JSON parsing
struct ParseWork {
    const json* j;
    DarttField* field;
    bool is_type_info;  // true = parse as type_info, false = parse as field
};

// Work item for iterative UI injection
struct InjectWork {
    json* j;
    const DarttField* field;
};

// Helper: get FieldType from type string
//TODO: consider falling back to uint32_t if the type is unknown and the size is equal to four. 
//TODO: cross reference type with nbytes to confirm that the label matches the expected size.
FieldType parse_field_type(const std::string& type_str) {
    // Check for common type names
    if (type_str == "float")
	{
		return FieldType::FLOAT;
	}
    if (type_str == "double") 
	{
		return FieldType::DOUBLE;
	}
    if (type_str == "int8_t" || type_str == "signed char") 
	{
		return FieldType::INT8;
	}
    if (type_str == "uint8_t" || type_str == "unsigned char")
	{ 
		return FieldType::UINT8;
	}
    if (type_str == "int16_t" || type_str == "short" || type_str == "short int") 
	{
		return FieldType::INT16;
	}
    if (type_str == "uint16_t" || type_str == "unsigned short" || type_str == "unsigned short int") 
	{
		return FieldType::UINT16;
	}
    if (type_str == "int32_t" || type_str == "int" || type_str == "long" || type_str == "long int") return FieldType::INT32;
    if (type_str == "uint32_t" || type_str == "unsigned int" || type_str == "unsigned long" || type_str == "unsigned long int" || type_str == "long unsigned int") return FieldType::UINT32;
    if (type_str == "int64_t" || type_str == "long long" || type_str == "long long int") return FieldType::INT64;
    if (type_str == "uint64_t" || type_str == "unsigned long long" || type_str == "unsigned long long int") return FieldType::UINT64;
    if (type_str == "struct") return FieldType::STRUCT;
    if (type_str == "union") return FieldType::UNION;
    if (type_str == "array") return FieldType::ARRAY;
    if (type_str == "pointer") return FieldType::POINTER;
    if (type_str == "enum") return FieldType::ENUM;

    // Check for pointer types (end with *)
    if (!type_str.empty() && type_str.back() == '*') return FieldType::POINTER;

    // Check for struct/union prefixes
    if (type_str.rfind("struct ", 0) == 0) return FieldType::STRUCT;
    if (type_str.rfind("union ", 0) == 0) return FieldType::UNION;
    if (type_str.rfind("enum ", 0) == 0) return FieldType::ENUM;

    return FieldType::UNKNOWN;
}

// Helper: check if a field type is a primitive (can be read/displayed directly)
bool is_primitive_type(FieldType type) {
    switch (type) {
        case FieldType::FLOAT:
        case FieldType::DOUBLE:
        case FieldType::INT8:
        case FieldType::UINT8:
        case FieldType::INT16:
        case FieldType::UINT16:
        case FieldType::INT32:
        case FieldType::UINT32:
        case FieldType::INT64:
        case FieldType::UINT64:
        case FieldType::POINTER:
        case FieldType::ENUM:
            return true;
        default:
            return false;
    }
}

// Helper: get display string for a field's value
std::string format_field_value(const DarttField& field) 
{
    char buf[64];
    switch (field.type) 
	{
        case FieldType::FLOAT:
            snprintf(buf, sizeof(buf), "%.6f", field.value.f32);
            break;
        case FieldType::DOUBLE:
            snprintf(buf, sizeof(buf), "%.6f", field.value.f64);
            break;
        case FieldType::INT8:
            snprintf(buf, sizeof(buf), "%d", field.value.i8);
            break;
        case FieldType::UINT8:
            snprintf(buf, sizeof(buf), "%u", field.value.u8);
            break;
        case FieldType::INT16:
            snprintf(buf, sizeof(buf), "%d", field.value.i16);
            break;
        case FieldType::UINT16:
            snprintf(buf, sizeof(buf), "%u", field.value.u16);
            break;
        case FieldType::INT32:
            snprintf(buf, sizeof(buf), "%d", field.value.i32);
            break;
        case FieldType::UINT32:
            snprintf(buf, sizeof(buf), "%u", field.value.u32);
            break;
        case FieldType::INT64:
            snprintf(buf, sizeof(buf), "%lld", (long long)field.value.i64);
            break;
        case FieldType::UINT64:
            snprintf(buf, sizeof(buf), "%llu", (unsigned long long)field.value.u64);
            break;
        case FieldType::POINTER:
            snprintf(buf, sizeof(buf), "0x%08X", field.value.u32);
            break;
        case FieldType::ENUM:
            snprintf(buf, sizeof(buf), "%d", field.value.i32);
            break;
        default:
            return "???";
    }
    return std::string(buf);
}

// Parse fields from JSON iteratively using explicit stack
static void parse_fields_iterative(const json& root_type_info, DarttField& root_field) {
    std::vector<ParseWork> stack;
    stack.push_back({&root_type_info, &root_field, true});

    while (!stack.empty()) {
        ParseWork work = stack.back();
        stack.pop_back();

        const json& j = *work.j;
        DarttField& field = *work.field;

        if (work.is_type_info) 
		{
            // Parse as type_info
            if (!j.is_object()) continue;

            std::string type_str = j.value("type", "unknown");
            field.type = parse_field_type(type_str);
            field.nbytes = j.value("size", 0u);

            if (j.contains("typedef")) 
			{
                field.type_name = j["typedef"].get<std::string>();
            } 
			else 
			{
                field.type_name = type_str;
            }

            // Handle struct/union - queue child fields
            if (type_str == "struct" || type_str == "union") 
			{
                if (j.contains("fields") && j["fields"].is_array()) 
				{
                    const json& fields_array = j["fields"];
                    // Pre-allocate children
                    field.children.resize(fields_array.size());
                    // Push in reverse order so first child is processed first
                    for (size_t i = fields_array.size(); i > 0; i--) 
					{
                        stack.push_back({&fields_array[i-1], &field.children[i-1], false});
                    }
                }
            }
            // Handle array
            else if (type_str == "array") 
			{
                field.array_size = j.value("total_elements", 0u);
                if (j.contains("element_type")) 
				{
                    const json& elem = j["element_type"];
                    field.element_nbytes = elem.value("size", 0u);

                    std::string elem_type = elem.value("type", "");
                    if (elem_type == "struct" || elem_type == "union") 
					{
                        // Array of structs - queue element type parsing
                        field.children.resize(1);
                        stack.push_back({&elem, &field.children[0], true});
                    } 
					else 
					{
                        // Primitive array
                        field.type_name = elem.value("typedef", elem.value("type", "unknown"));
                    }
                }
            }
        } 
		else 
		{
            // Parse as field (has name, byte_offset, type_info)
            field.name = j.value("name", "");
            field.byte_offset = j.value("byte_offset", 0u);
            field.dartt_offset = j.value("dartt_offset", 0u);

            // Parse UI settings if present
            if (j.contains("ui")) 
			{
                const json& ui = j["ui"];
                field.subscribed = ui.value("subscribed", false);
                field.expanded = ui.value("expanded", false);
                field.display_scale = ui.value("display_scale", 1.0f);
				field.use_display_scale = ui.value("use_display_scale", false);	//may throw error - unsure how this lib call works
            }

            // Queue type_info parsing
            if (j.contains("type_info")) 
			{
                stack.push_back({&j["type_info"], &field, true});
            }
        }
    }
}

/*
Expand primitive arrays into individual element children.
For any field where array_size > 0 && children.empty() && element_nbytes > 0,
creates array_size children with [i] names and correct offsets/types.
*/
void expand_array_elements(DarttField& root) 
{
    std::vector<DarttField*> stack;
    stack.push_back(&root);
    while (!stack.empty()) 
	{
        DarttField* f = stack.back();
        stack.pop_back();

        if (f->array_size > 0 && f->children.empty() && f->element_nbytes > 0) 
		{
            FieldType elem_type = parse_field_type(f->type_name);

            f->children.resize(f->array_size);
            for (uint32_t i = 0; i < f->array_size; i++) 
			{
                DarttField& elem = f->children[i];
                elem.name = "[" + std::to_string(i) + "]";
                elem.byte_offset = f->byte_offset + i * f->element_nbytes;
                elem.dartt_offset = elem.byte_offset / 4;
                elem.nbytes = f->element_nbytes;
                elem.type = elem_type;
                elem.type_name = f->type_name;
            }
        }

        // Continue DFS into children (including newly created ones)
        for (size_t i = f->children.size(); i > 0; i--) 
		{
            stack.push_back(&f->children[i - 1]);
        }
    }
}

/*
Function to collect a list of all leaves. One-time depth first search traversal
for leaf-only operations
*/
void collect_leaves(DarttField& root, std::vector<DarttField*> &leaf_list)
{
    std::vector<DarttField*> stack;
    stack.push_back(&root);
    while (!stack.empty()) 
	{
		DarttField* field = stack.back();
        stack.pop_back();
		if(field->children.empty())	//leaf node
		{
			leaf_list.push_back(field);
		}
		else
		{
			//pattern > 0 prevents underflow of the size_t resulting in an infinite loop. size_t matches size() return value and has compile time size guarantees.
			for(size_t i = field->children.size(); i > 0; i--)
			{
				stack.push_back(&field->children[i - 1]);
			}
		}
	}
}

// Main config loader
bool load_dartt_config(const char* json_path, DarttConfig& config, Plotter& plot, Serial & serial, dartt_sync_t& ds)
{
    // Open and parse JSON file
    std::ifstream f(json_path);
    if (!f.is_open())
    {
        fprintf(stderr, "Error: Could not open config file: %s\n", json_path);
        return false;
    }

    json j;
    try
    {
        j = json::parse(f);
    }
    catch (const json::parse_error& e)
    {
        fprintf(stderr, "Error: JSON parse error: %s\n", e.what());
        return false;
    }

	if(j.contains("serial_settings") && j["serial_settings"].is_object())
	{
		const json & ser_settings = j["serial_settings"];

		ds.address = ser_settings.value("dartt_serial_address", 0);
		uint32_t baudrate = ser_settings.value("baudrate", 921600);
		if(baudrate != serial.get_baud_rate())
		{
			printf("Disconnecting serial...\n");
			serial.disconnect();
			printf("done.\n Reconnecting with baudrate %d\n", baudrate);
			if(serial.autoconnect(baudrate))
			{
				printf("Success. Serial connected\n");
			}
			else
			{
				printf("Serial failed to connect\n");
			}
		}

		comm_mode = (CommMode)ser_settings.value("comm_mode", (int)COMM_SERIAL);

		std::string ip = ser_settings.value("udp_ip", "192.168.1.100");
		strncpy(udp_state.ip, ip.c_str(), sizeof(udp_state.ip) - 1);
		udp_state.ip[sizeof(udp_state.ip) - 1] = '\0';
		udp_state.port = ser_settings.value("udp_port", (uint16_t)5000);

		std::string tcp_ip = ser_settings.value("tcp_ip", "192.168.1.100");
		strncpy(tcp_state.ip, tcp_ip.c_str(), sizeof(tcp_state.ip) - 1);
		tcp_state.ip[sizeof(tcp_state.ip) - 1] = '\0';
		tcp_state.port = ser_settings.value("tcp_port", (uint16_t)5000);
	}
	
    // Parse top-level fields
    config.symbol = j.value("symbol", "");
    config.address_str = j.value("address", "");
    config.address = j.value("address_int", 0u);
    config.nbytes = j.value("nbytes", 0u);
    config.nwords = j.value("nwords", 0u);

    // Parse the root type structure
    if (j.contains("type")) {
        config.root.name = config.symbol;
        parse_fields_iterative(j["type"], config.root);
    }

    printf("Loaded config: symbol=%s, address=0x%08X, nbytes=%u, nwords=%u\n",
           config.symbol.c_str(), config.address, config.nbytes, config.nwords);

    expand_array_elements(config.root);
    collect_leaves(config.root, config.leaf_list);

    // Load plotting config if plotter provided
	load_plotting_config(j, plot, config.leaf_list);

    return true;
}

// Inject UI settings iteratively using explicit stack
static void inject_ui_settings_iterative(json& root_json, const std::vector<DarttField>& root_fields) {
    std::vector<InjectWork> stack;

    // Initialize stack with root fields
    for (size_t i = 0; i < root_json.size() && i < root_fields.size(); i++) {
        stack.push_back({&root_json[i], &root_fields[i]});
    }

    while (!stack.empty()) {
        InjectWork work = stack.back();
        stack.pop_back();

        json& j = *work.j;
        const DarttField& field = *work.field;

        // Add/update ui object for this field
        json ui;
        ui["subscribed"] = field.subscribed;
        ui["expanded"] = field.expanded;
        ui["display_scale"] = field.display_scale;
		ui["use_display_scale"] = field.use_display_scale;
        j["ui"] = ui;

        // Queue children if present (structs/unions)
        if (j.contains("type_info") && j["type_info"].contains("fields")) {
            json& json_fields = j["type_info"]["fields"];
            for (size_t i = 0; i < json_fields.size() && i < field.children.size(); i++) {
                stack.push_back({&json_fields[i], &field.children[i]});
            }
        }
    }
}

void save_serial_settings(json & j, Serial & serial, const dartt_sync_t & ds)
{
	json serial_settings;
	serial_settings["dartt_serial_address"] = ds.address;
	serial_settings["baudrate"] = serial.get_baud_rate();
	serial_settings["comm_mode"] = (int)comm_mode;
	serial_settings["udp_ip"] = std::string(udp_state.ip);
	serial_settings["udp_port"] = udp_state.port;
	serial_settings["tcp_ip"] = std::string(tcp_state.ip);
	serial_settings["tcp_port"] = tcp_state.port;
	j["serial_settings"] = serial_settings;
}

// Save plotting config to JSON
void save_plotting_config(json& j, const Plotter& plot, const std::vector<DarttField*>& leaf_list)
{
    json plotting;
    json lines_json = json::array();

    for (size_t i = 0; i < plot.lines.size(); i++)
    {
        const Line& line = plot.lines[i];
        json line_json;

        line_json["mode"] = (int)line.mode;

        // X source data
        json xsource_data;
        if (line.xsource == &plot.sys_sec)
        {
            xsource_data["byte_offset"] = -1;
            xsource_data["name"] = "sys_sec";
        }
        else if (line.xsource == nullptr)
        {
            xsource_data["byte_offset"] = -2;
            xsource_data["name"] = "none";
        }
        else
        {
            // Find field in leaf_list
            bool found = false;
            for (size_t k = 0; k < leaf_list.size(); k++)
            {
                if (&leaf_list[k]->display_value == line.xsource)
                {
                    xsource_data["byte_offset"] = (int32_t)leaf_list[k]->byte_offset;
                    xsource_data["name"] = leaf_list[k]->name;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                xsource_data["byte_offset"] = -2;
                xsource_data["name"] = "none";
            }
        }
        line_json["xsource_data"] = xsource_data;

        // Y source data
        json ysource_data;
        if (line.ysource == nullptr)
        {
            ysource_data["byte_offset"] = -2;
            ysource_data["name"] = "none";
        }
        else
        {
            bool found = false;
            for (size_t k = 0; k < leaf_list.size(); k++)
            {
                if (&leaf_list[k]->display_value == line.ysource)
                {
                    ysource_data["byte_offset"] = (int32_t)leaf_list[k]->byte_offset;
                    ysource_data["name"] = leaf_list[k]->name;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                ysource_data["byte_offset"] = -2;
                ysource_data["name"] = "none";
            }
        }
        line_json["ysource_data"] = ysource_data;

        // Color
        line_json["color"] = {line.color.r, line.color.g, line.color.b, line.color.a};

        // Scales and offsets
        line_json["xscale"] = line.xscale;
        line_json["xoffset"] = line.xoffset;
        line_json["yscale"] = line.yscale;
        line_json["yoffset"] = line.yoffset;
		line_json["enqueue_cap"] = line.enqueue_cap;
        lines_json.push_back(line_json);
    }

    plotting["lines"] = lines_json;
    j["plotting"] = plotting;
}

// Save config to JSON file (only adds ui objects, preserves everything else)
// If plot is provided, also saves plotting config
bool save_dartt_config(const char* json_path, const DarttConfig& config, const Plotter& plot, Serial & serial, dartt_sync_t& ds) 
{
    // Read original JSON
    std::ifstream f_in(json_path);
    if (!f_in.is_open()) {
        fprintf(stderr, "Error: Could not open config file for reading: %s\n", json_path);
        return false;
    }

    json j;
    try 
	{
        j = json::parse(f_in);
    } 
	catch (const json::parse_error& e) 
	{
        fprintf(stderr, "Error: JSON parse error: %s\n", e.what());
        return false;
    }
    f_in.close();

    // Inject UI settings into type.fields
    if (j.contains("type") && j["type"].contains("fields")) 
	{
        inject_ui_settings_iterative(j["type"]["fields"], config.root.children);
    }

    // Save plotting config if plotter provided
	save_plotting_config(j, plot, config.leaf_list);

	save_serial_settings(j, serial, ds);

    // Write back
    std::ofstream f_out(json_path);
    if (!f_out.is_open()) 
	{
        fprintf(stderr, "Error: Could not open config file for writing: %s\n", json_path);
        return false;
    }

    f_out << j.dump(2);
    f_out.close();

    printf("Saved UI settings to: %s\n", json_path);
    return true;
}

// Find field by byte_offset and name (both must match)
DarttField* find_field_by_offset_and_name(
    const std::vector<DarttField*>& leaf_list,
    int32_t byte_offset,
    const std::string& name)
{
    for (size_t i = 0; i < leaf_list.size(); i++)
    {
        if ((int32_t)leaf_list[i]->byte_offset == byte_offset &&
            leaf_list[i]->name == name)
        {
            return leaf_list[i];
        }
    }
    return nullptr;
}


// Load plotting config from JSON
void load_plotting_config(const json& j, Plotter& plot, const std::vector<DarttField*>& leaf_list)
{
    if (!j.contains("plotting"))
    {
        printf("No plotting config found, using defaults\n");
        return;
    }

    const json& plotting = j["plotting"];
    if (!plotting.contains("lines") || !plotting["lines"].is_array())
    {
        printf("No lines array in plotting config\n");
        return;
    }

    plot.lines.clear();
    const json& lines_json = plotting["lines"];

    for (size_t i = 0; i < lines_json.size(); i++)
    {
        const json& line_json = lines_json[i];
        Line line;

        // Mode
        line.mode = (timemode_t)line_json.value("mode", 0);

        // X source
        if (line_json.contains("xsource_data"))
        {
            const json& xdata = line_json["xsource_data"];
            int32_t offset = xdata.value("byte_offset", -2);
            std::string name = xdata.value("name", "none");

            if (offset == -1 && name == "sys_sec")
            {
                line.xsource = &plot.sys_sec;
            }
            else if (offset == -2 || name == "none")
            {
                line.xsource = nullptr;
            }
            else
            {
                DarttField* field = find_field_by_offset_and_name(leaf_list, offset, name);
                if (field)
                {
                    line.xsource = &field->display_value;
                }
                else
                {
                    printf("Warning: Could not find xsource field '%s' at offset %d, defaulting to sys_sec\n",
                           name.c_str(), offset);
                    line.xsource = &plot.sys_sec;
                }
            }
        }
        else
        {
            line.xsource = &plot.sys_sec;
        }

        // Y source
        if (line_json.contains("ysource_data"))
        {
            const json& ydata = line_json["ysource_data"];
            int32_t offset = ydata.value("byte_offset", -2);
            std::string name = ydata.value("name", "none");

            if (offset == -2 || name == "none")
            {
                line.ysource = nullptr;
            }
            else
            {
                DarttField* field = find_field_by_offset_and_name(leaf_list, offset, name);
                if (field)
                {
                    line.ysource = &field->display_value;
                }
                else
                {
                    printf("Warning: Could not find ysource field '%s' at offset %d, defaulting to none\n",
                           name.c_str(), offset);
                    line.ysource = nullptr;
                }
            }
        }
        else
        {
            line.ysource = nullptr;
        }

        // Color
        if (line_json.contains("color") && line_json["color"].is_array() && line_json["color"].size() >= 4)
        {
            line.color.r = line_json["color"][0].get<uint8_t>();
            line.color.g = line_json["color"][1].get<uint8_t>();
            line.color.b = line_json["color"][2].get<uint8_t>();
            line.color.a = line_json["color"][3].get<uint8_t>();
        }

        // Scales and offsets
        line.xscale = line_json.value("xscale", 1.0f);
        line.xoffset = line_json.value("xoffset", 0.0f);
        line.yscale = line_json.value("yscale", 1.0f);
        line.yoffset = line_json.value("yoffset", 0.0f);
		line.enqueue_cap = line_json.value("enqueue_cap", 2134);
        plot.lines.push_back(line);
    }

    printf("Loaded %zu plot lines from config\n", plot.lines.size());
}

