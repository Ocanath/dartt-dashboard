#ifndef DARTT_BUFFER_SYNC_H
#define DARTT_BUFFER_SYNC_H

#include "config.h"
#include <vector>

struct MemoryRegion {
    uint32_t start_offset;              // Byte offset from buffer base
    uint32_t length;                    // Total bytes (32-bit aligned)
    std::vector<DarttField*> fields;    // Leaf fields in this region
};

// Build coalesced queues
std::vector<MemoryRegion> build_write_queue(DarttConfig& config);
std::vector<MemoryRegion> build_read_queue(DarttConfig& config);

// Sync values between DarttField.value and flat buffers
void sync_fields_to_ctl_buf(DarttConfig& config, const MemoryRegion& region);
void sync_periph_buf_to_fields(DarttConfig& config, const MemoryRegion& region);

// Clear dirty flags after successful write
void clear_dirty_flags(const MemoryRegion& region);

#endif // DARTT_BUFFER_SYNC_H
