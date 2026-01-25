#include "config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>

using json = nlohmann::json;

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

// Forward declaration for recursive parsing
static void parse_field(const json& j, DarttField& field);

// Parse type_info object and populate field
static void parse_type_info(const json& type_info, DarttField& field) {
    if (!type_info.is_object()) return;

    std::string type_str = type_info.value("type", "unknown");
    field.type = parse_field_type(type_str);

    // Get size from type_info
    field.nbytes = type_info.value("size", 0u);

    // Store the type name (use typedef if present, otherwise type)
    if (type_info.contains("typedef")) {
        field.type_name = type_info["typedef"].get<std::string>();
    } else {
        field.type_name = type_str;
    }

    // Handle struct/union - parse child fields
    if (type_str == "struct" || type_str == "union") {
        if (type_info.contains("fields") && type_info["fields"].is_array()) {
            for (const auto& child_json : type_info["fields"]) {
                DarttField child;
                parse_field(child_json, child);
                field.children.push_back(child);
            }
        }
    }
    // Handle array
    else if (type_str == "array") {
        field.array_size = type_info.value("total_elements", 0u);
        if (type_info.contains("element_type")) {
            const auto& elem = type_info["element_type"];
            field.element_nbytes = elem.value("size", 0u);

            // If array of structs, parse element type's fields
            std::string elem_type = elem.value("type", "");
            if (elem_type == "struct" || elem_type == "union") {
                // Store element structure in a child
                DarttField elem_field;
                parse_type_info(elem, elem_field);
                field.children.push_back(elem_field);
            } else {
                // Primitive array - store element type info
                field.type_name = elem.value("typedef", elem.value("type", "unknown"));
            }
        }
    }
}

// Parse a single field from JSON
static void parse_field(const json& j, DarttField& field) {
    field.name = j.value("name", "");
    field.byte_offset = j.value("byte_offset", 0u);
    field.dartt_offset = j.value("dartt_offset", 0u);

    // Parse type_info if present
    if (j.contains("type_info")) {
        parse_type_info(j["type_info"], field);
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
        parse_type_info(j["type"], config.root);
    }

    printf("Loaded config: symbol=%s, address=0x%08X, nbytes=%u, nwords=%u\n",
           config.symbol.c_str(), config.address, config.nbytes, config.nwords);

    return true;
}
