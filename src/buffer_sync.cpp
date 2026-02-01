#include "buffer_sync.h"
#include <algorithm>
#include <cstring>
#include <vector>

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

// Collect all dirty leaf fields
static void collect_dirty_fields(const std::vector<DarttField*> &leaf_list, std::vector<DarttField*>& out) 
{
	for(size_t i = 0; i < leaf_list.size(); i++)
	{
		DarttField * leaf = leaf_list[i];
		if (leaf->dirty) 
		{
			out.push_back(leaf);
		}
	}
}

// Collect all subscribed leaf fields
static void collect_subscribed_fields(const std::vector<DarttField*> &leaf_list, std::vector<DarttField*>& out) 
{
	for(size_t i = 0; i < leaf_list.size(); i++)
	{
		DarttField * leaf = leaf_list[i];
		if (leaf->subscribed) 
		{
			out.push_back(leaf);
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

std::vector<MemoryRegion> build_write_queue(DarttConfig& config, const std::vector<DarttField*> &leaf_list) 
{
    std::vector<DarttField*> dirty_fields;
    collect_dirty_fields(leaf_list, dirty_fields);
    return coalesce_fields(dirty_fields);
}

std::vector<MemoryRegion> build_read_queue(DarttConfig& config, const std::vector<DarttField*> &leaf_list) 
{
    std::vector<DarttField*> subscribed_fields;
    collect_subscribed_fields(leaf_list, subscribed_fields);
    return coalesce_fields(subscribed_fields);
}

bool sync_fields_to_ctl_buf(DarttConfig& config, const MemoryRegion& region) 
{
    if (!config.ctl_buf.buf) 
	{
		return false;
	}

    for (DarttField* field : region.fields) 
	{
        uint8_t* dst = config.ctl_buf.buf + field->byte_offset;
		if(field->byte_offset + field->nbytes > config.ctl_buf.size)
		{
			return false;
		}
		std::memcpy(dst, (unsigned char *)(&field->value.u8), field->nbytes);
    }
	return true;
}

bool sync_periph_buf_to_fields(DarttConfig& config, const MemoryRegion& region) {
    if (!config.periph_buf.buf) 
	{
		return false;
	}

    for (DarttField* field : region.fields) 
	{
        const uint8_t* src = config.periph_buf.buf + field->byte_offset;
		if(field->byte_offset + field->nbytes > config.periph_buf.size)
		{
			return false;
		}
		std::memcpy((unsigned char *)(&field->value.u8), src, field->nbytes);
    }
	return true;
}

void clear_dirty_flags(const MemoryRegion& region)
{
    for (DarttField* field : region.fields) 
	{
        field->dirty = false;
    }
}
