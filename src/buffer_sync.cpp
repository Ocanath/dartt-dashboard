#include "buffer_sync.h"
#include <algorithm>
#include <cstring>

// Helper: collect all leaf fields matching a predicate
static void collect_leaf_fields(
    DarttField& field,
    std::vector<DarttField*>& out,
    bool (*predicate)(const DarttField&)
) {
    if (field.children.empty()) {
        // Leaf node
        if (predicate(field)) {
            out.push_back(&field);
        }
    } else {
        // Recurse into children
        for (auto& child : field.children) {
            collect_leaf_fields(child, out, predicate);
        }
    }
}

// Align offset down to 32-bit boundary
static uint32_t align_down_32(uint32_t offset) {
    return offset & ~3u;
}

// Align offset up to 32-bit boundary
static uint32_t align_up_32(uint32_t offset) {
    return (offset + 3u) & ~3u;
}

// Coalesce sorted fields into contiguous memory regions
static std::vector<MemoryRegion> coalesce_fields(std::vector<DarttField*>& fields) {
    std::vector<MemoryRegion> regions;

    if (fields.empty()) {
        return regions;
    }

    // Sort by byte_offset
    std::sort(fields.begin(), fields.end(), [](DarttField* a, DarttField* b) {
        return a->byte_offset < b->byte_offset;
    });

    // Start first region
    MemoryRegion current;
    current.start_offset = align_down_32(fields[0]->byte_offset);
    uint32_t current_end = align_up_32(fields[0]->byte_offset + fields[0]->nbytes);
    current.fields.push_back(fields[0]);

    for (size_t i = 1; i < fields.size(); i++) {
        DarttField* f = fields[i];
        uint32_t f_start = align_down_32(f->byte_offset);
        uint32_t f_end = align_up_32(f->byte_offset + f->nbytes);

        // Check if contiguous or overlapping with current region
        if (f_start <= current_end) {
            // Extend current region
            if (f_end > current_end) {
                current_end = f_end;
            }
            current.fields.push_back(f);
        } else {
            // Finalize current region and start new one
            current.length = current_end - current.start_offset;
            regions.push_back(current);

            current = MemoryRegion();
            current.start_offset = f_start;
            current_end = f_end;
            current.fields.push_back(f);
        }
    }

    // Finalize last region
    current.length = current_end - current.start_offset;
    regions.push_back(current);

    return regions;
}

std::vector<MemoryRegion> build_write_queue(DarttConfig& config) {
    std::vector<DarttField*> dirty_fields;

    collect_leaf_fields(config.root, dirty_fields, [](const DarttField& f) {
        return f.dirty;
    });

    return coalesce_fields(dirty_fields);
}

std::vector<MemoryRegion> build_read_queue(DarttConfig& config) {
    std::vector<DarttField*> subscribed_fields;

    collect_leaf_fields(config.root, subscribed_fields, [](const DarttField& f) {
        return f.subscribed;
    });

    return coalesce_fields(subscribed_fields);
}

void sync_fields_to_ctl_buf(DarttConfig& config, const MemoryRegion& region) {
    if (!config.ctl_buf) return;

    for (DarttField* field : region.fields) {
        uint8_t* dst = config.ctl_buf + field->byte_offset;

        switch (field->type) {
            case FieldType::FLOAT:
                std::memcpy(dst, &field->value.f32, sizeof(float));
                break;
            case FieldType::DOUBLE:
                std::memcpy(dst, &field->value.f64, sizeof(double));
                break;
            case FieldType::INT8:
                std::memcpy(dst, &field->value.i8, sizeof(int8_t));
                break;
            case FieldType::UINT8:
                std::memcpy(dst, &field->value.u8, sizeof(uint8_t));
                break;
            case FieldType::INT16:
                std::memcpy(dst, &field->value.i16, sizeof(int16_t));
                break;
            case FieldType::UINT16:
                std::memcpy(dst, &field->value.u16, sizeof(uint16_t));
                break;
            case FieldType::INT32:
            case FieldType::ENUM:
                std::memcpy(dst, &field->value.i32, sizeof(int32_t));
                break;
            case FieldType::UINT32:
            case FieldType::POINTER:
                std::memcpy(dst, &field->value.u32, sizeof(uint32_t));
                break;
            case FieldType::INT64:
                std::memcpy(dst, &field->value.i64, sizeof(int64_t));
                break;
            case FieldType::UINT64:
                std::memcpy(dst, &field->value.u64, sizeof(uint64_t));
                break;
            default:
                break;
        }
    }
}

void sync_periph_buf_to_fields(DarttConfig& config, const MemoryRegion& region) {
    if (!config.periph_buf) return;

    for (DarttField* field : region.fields) {
        const uint8_t* src = config.periph_buf + field->byte_offset;

        switch (field->type) {
            case FieldType::FLOAT:
                std::memcpy(&field->value.f32, src, sizeof(float));
                break;
            case FieldType::DOUBLE:
                std::memcpy(&field->value.f64, src, sizeof(double));
                break;
            case FieldType::INT8:
                std::memcpy(&field->value.i8, src, sizeof(int8_t));
                break;
            case FieldType::UINT8:
                std::memcpy(&field->value.u8, src, sizeof(uint8_t));
                break;
            case FieldType::INT16:
                std::memcpy(&field->value.i16, src, sizeof(int16_t));
                break;
            case FieldType::UINT16:
                std::memcpy(&field->value.u16, src, sizeof(uint16_t));
                break;
            case FieldType::INT32:
            case FieldType::ENUM:
                std::memcpy(&field->value.i32, src, sizeof(int32_t));
                break;
            case FieldType::UINT32:
            case FieldType::POINTER:
                std::memcpy(&field->value.u32, src, sizeof(uint32_t));
                break;
            case FieldType::INT64:
                std::memcpy(&field->value.i64, src, sizeof(int64_t));
                break;
            case FieldType::UINT64:
                std::memcpy(&field->value.u64, src, sizeof(uint64_t));
                break;
            default:
                break;
        }
    }
}

void clear_dirty_flags(const MemoryRegion& region) {
    for (DarttField* field : region.fields) {
        field->dirty = false;
    }
}
