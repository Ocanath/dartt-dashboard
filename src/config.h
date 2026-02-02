#ifndef DARTT_CONFIG_H
#define DARTT_CONFIG_H

#include <string>
#include <vector>
#include <cstdint>
#include "dartt_sync.h"
#include "dartt.h"

// Field type classification for parsing and display
enum class FieldType {
    STRUCT,
    UNION,
    ARRAY,
    FLOAT,
    DOUBLE,
    INT8,
    UINT8,
    INT16,
    UINT16,
    INT32,
    UINT32,
    INT64,
    UINT64,
    POINTER,
    ENUM,
    UNKNOWN
};

// Single field in the hierarchy
struct DarttField {
    std::string name;
    uint32_t byte_offset;       // absolute from struct base
    uint32_t dartt_offset;      // 32-bit word index (byte_offset / 4)
    uint32_t nbytes;            // size in bytes
    FieldType type;
    std::string type_name;      // original type string from JSON

    // For arrays
    uint32_t array_size;        // number of elements (0 if not array)
    uint32_t element_nbytes;    // size of each element

    // For structs/unions - child fields
    std::vector<DarttField> children;

    // UI state
    bool subscribed;
    bool dirty;                 // set when value edited, cleared after write
    float display_scale;
    bool expanded;              // tree node expanded in UI

	bool use_display_scale;
	float display_value;	//the True Value, scaled by display scale.

    // Runtime value storage
    union {
        float f32;
        double f64;
        int8_t i8;
        uint8_t u8;
        int16_t i16;
        uint16_t u16;
        int32_t i32;
        uint32_t u32;
        int64_t i64;
        uint64_t u64;
    } value;

    // Default constructor
    DarttField()
        : byte_offset(0)
        , dartt_offset(0)
        , nbytes(0)
        , type(FieldType::UNKNOWN)
        , array_size(0)
        , element_nbytes(0)
        , subscribed(false)
        , dirty(false)
        , display_scale(1.0f)
        , expanded(false)
		, use_display_scale(false)
		, display_value(0.f)
    {
        value.u64 = 0;
    }
};

// Top-level config loaded from JSON
struct DarttConfig {
    std::string symbol;
    std::string address_str;    // hex string "0x20001000"
    uint32_t address;           // numeric address
    uint32_t nbytes;            // total size in bytes
    uint32_t nwords;            // total size in 32-bit words
    DarttField root;            // root struct containing all fields

    // DARTT buffers (allocated after parsing)
	buffer_t ctl_buf;
	buffer_t periph_buf;
    // uint8_t* ctl_buf;           // controller copy (what we want)
    // uint8_t* periph_buf;        // peripheral copy (shadow, what device has)

	//A flat list of the leaves - for easy access, loaded with the initialization from file
	std::vector<DarttField*> leaf_list;
	std::vector<DarttField*> subscribed_list;  // subscribed leaves only
	std::vector<DarttField*> dirty_list;       // dirty leaves only
	
    DarttConfig()
        : address(0)
        , nbytes(0)
        , nwords(0)
        , ctl_buf(0)
        , periph_buf(0)
    {}

    ~DarttConfig() {
        if (ctl_buf.buf)
		{
			free(ctl_buf.buf);
		} 
        if (periph_buf.buf)
		{
			free(periph_buf.buf);
		} 
    }

    // Allocate buffers based on nbytes
    bool allocate_buffers() 
	{
        if (nbytes == 0) 
		{
			return false;
		}

        ctl_buf.buf = (uint8_t*)calloc(1, nbytes);
		ctl_buf.len = nbytes;
		ctl_buf.size = nbytes;

        periph_buf.buf = (uint8_t*)calloc(1, nbytes);
		periph_buf.len = nbytes;
		periph_buf.size = nbytes;
		

        return (ctl_buf.buf != nullptr && periph_buf.buf != nullptr);
    }
};

// Parse config from JSON file
// Returns true on success, false on error (error message printed to stderr)
bool load_dartt_config(const char* json_path, DarttConfig& config);

// Collect a list of all leaves
void collect_leaves(DarttField& root, std::vector<DarttField*> &leaf_list);

// Save config to JSON file (preserves UI settings)
// Returns true on success, false on error (error message printed to stderr)
bool save_dartt_config(const char* json_path, const DarttConfig& config);

// Helper: get FieldType from type string
FieldType parse_field_type(const std::string& type_str);

// Helper: check if a field type is a primitive (can be read/displayed directly)
bool is_primitive_type(FieldType type);

// Helper: get display string for a field's value
std::string format_field_value(const DarttField& field);

#endif // DARTT_CONFIG_H
