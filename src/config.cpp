#include "config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>
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
    if (type_str == "float") return FieldType::FLOAT;
    if (type_str == "double") return FieldType::DOUBLE;
    if (type_str == "int8_t" || type_str == "signed char") return FieldType::INT8;
    if (type_str == "uint8_t" || type_str == "unsigned char") return FieldType::UINT8;
    if (type_str == "int16_t" || type_str == "short" || type_str == "short int") return FieldType::INT16;
    if (type_str == "uint16_t" || type_str == "unsigned short" || type_str == "unsigned short int") return FieldType::UINT16;
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
std::string format_field_value(const DarttField& field) {
    char buf[64];
    switch (field.type) {
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

        if (work.is_type_info) {
            // Parse as type_info
            if (!j.is_object()) continue;

            std::string type_str = j.value("type", "unknown");
            field.type = parse_field_type(type_str);
            field.nbytes = j.value("size", 0u);

            if (j.contains("typedef")) {
                field.type_name = j["typedef"].get<std::string>();
            } else {
                field.type_name = type_str;
            }

            // Handle struct/union - queue child fields
            if (type_str == "struct" || type_str == "union") {
                if (j.contains("fields") && j["fields"].is_array()) {
                    const auto& fields_array = j["fields"];
                    // Pre-allocate children
                    field.children.resize(fields_array.size());
                    // Push in reverse order so first child is processed first
                    for (size_t i = fields_array.size(); i > 0; i--) {
                        stack.push_back({&fields_array[i-1], &field.children[i-1], false});
                    }
                }
            }
            // Handle array
            else if (type_str == "array") {
                field.array_size = j.value("total_elements", 0u);
                if (j.contains("element_type")) {
                    const auto& elem = j["element_type"];
                    field.element_nbytes = elem.value("size", 0u);

                    std::string elem_type = elem.value("type", "");
                    if (elem_type == "struct" || elem_type == "union") {
                        // Array of structs - queue element type parsing
                        field.children.resize(1);
                        stack.push_back({&elem, &field.children[0], true});
                    } else {
                        // Primitive array
                        field.type_name = elem.value("typedef", elem.value("type", "unknown"));
                    }
                }
            }
        } else {
            // Parse as field (has name, byte_offset, type_info)
            field.name = j.value("name", "");
            field.byte_offset = j.value("byte_offset", 0u);
            field.dartt_offset = j.value("dartt_offset", 0u);

            // Parse UI settings if present
            if (j.contains("ui")) {
                const auto& ui = j["ui"];
                field.subscribed = ui.value("subscribed", false);
                field.expanded = ui.value("expanded", false);
                field.display_scale = ui.value("display_scale", 1.0f);
            }

            // Queue type_info parsing
            if (j.contains("type_info")) {
                stack.push_back({&j["type_info"], &field, true});
            }
        }
    }
}

// Main config loader
bool load_dartt_config(const char* json_path, DarttConfig& config) {
    // Open and parse JSON file
    std::ifstream f(json_path);
    if (!f.is_open()) {
        fprintf(stderr, "Error: Could not open config file: %s\n", json_path);
        return false;
    }

    json j;
    try {
        j = json::parse(f);
    } catch (const json::parse_error& e) {
        fprintf(stderr, "Error: JSON parse error: %s\n", e.what());
        return false;
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
        j["ui"] = ui;

        // Queue children if present (structs/unions)
        if (j.contains("type_info") && j["type_info"].contains("fields")) {
            auto& json_fields = j["type_info"]["fields"];
            for (size_t i = 0; i < json_fields.size() && i < field.children.size(); i++) {
                stack.push_back({&json_fields[i], &field.children[i]});
            }
        }
    }
}

// Save config to JSON file (only adds ui objects, preserves everything else)
bool save_dartt_config(const char* json_path, const DarttConfig& config) {
    // Read original JSON
    std::ifstream f_in(json_path);
    if (!f_in.is_open()) {
        fprintf(stderr, "Error: Could not open config file for reading: %s\n", json_path);
        return false;
    }

    json j;
    try {
        j = json::parse(f_in);
    } catch (const json::parse_error& e) {
        fprintf(stderr, "Error: JSON parse error: %s\n", e.what());
        return false;
    }
    f_in.close();

    // Inject UI settings into type.fields
    if (j.contains("type") && j["type"].contains("fields")) {
        inject_ui_settings_iterative(j["type"]["fields"], config.root.children);
    }

    // Write back
    std::ofstream f_out(json_path);
    if (!f_out.is_open()) {
        fprintf(stderr, "Error: Could not open config file for writing: %s\n", json_path);
        return false;
    }

    f_out << j.dump(2);
    f_out.close();

    printf("Saved UI settings to: %s\n", json_path);
    return true;
}
