#ifndef DARTT_BUFFER_SYNC_H
#define DARTT_BUFFER_SYNC_H

#include "config.h"
#include <vector>

struct MemoryRegion {
    uint32_t start_offset;              // Byte offset from buffer base
    uint32_t length;                    // Total bytes (32-bit aligned)
    std::vector<DarttField*> fields;    // Leaf fields in this region
};


// Auto-subscribe/dirty sibling elements within the same 4-byte word
void align_sub_word_access(std::vector<DarttField*>& leaf_list);

// Collect subscribed/dirty fields into output vectors
void collect_subscribed_fields(const std::vector<DarttField*> &leaf_list, std::vector<DarttField*>& out);
void collect_dirty_fields(const std::vector<DarttField*> &leaf_list, std::vector<DarttField*>& out);

// Build coalesced queues
std::vector<MemoryRegion> build_write_queue(DarttConfig& config);
std::vector<MemoryRegion> build_read_queue(DarttConfig& config);

// Sync values between DarttField.value and flat buffers
bool sync_fields_to_ctl_buf(DarttConfig& config, const MemoryRegion& region);
bool sync_periph_buf_to_fields(DarttConfig& config, const MemoryRegion& region);

// Clear dirty flags after successful write
void clear_dirty_flags(const MemoryRegion& region);

#endif // DARTT_BUFFER_SYNC_H
