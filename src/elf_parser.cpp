/*
 * elf_parser.cpp - ELF/DWARF parser implementation
 *
 * Uses libdwarf for DWARF parsing and ELFIO for symbol table access.
 * Implements iterative (stack-based) traversal to avoid stack overflow
 * on deeply nested types.
 */

#include "elf_parser.h"
#include "config.h"

#include <elfio/elfio.hpp>
#include <dwarf.h>
#include <libdwarf.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <memory>

/* ============================================================================
 * Internal Types
 * ============================================================================ */

/* Forward declaration for recursive structure */
struct TypeInfo;

/* Field info for struct/union members */
struct FieldInfo {
    std::string name;
    uint32_t byte_offset;           /* relative to parent struct */
    std::unique_ptr<TypeInfo> type_info;
    int bit_size;                   /* -1 if not a bitfield */
    int bit_offset;

    FieldInfo() : byte_offset(0), bit_size(-1), bit_offset(0) {}
    FieldInfo(FieldInfo&&) = default;
    FieldInfo& operator=(FieldInfo&&) = default;
};

/* Enum value */
struct EnumValue {
    std::string name;
    int64_t value;
};

/* Intermediate type representation during DWARF parsing */
struct TypeInfo {
    std::string type;               /* "struct", "union", "array", "float", etc. */
    std::string name;               /* type name if applicable */
    std::string typedef_name;       /* typedef alias if present */
    uint32_t size;                  /* size in bytes */
    std::string encoding;           /* for base types: "float", "signed", etc. */

    /* For arrays */
    std::vector<uint32_t> dimensions;
    uint32_t total_elements;

    /* For structs/unions */
    std::vector<FieldInfo> fields;

    /* For enums */
    std::vector<EnumValue> enumerators;

    /* For pointers */
    std::unique_ptr<TypeInfo> pointee;

    /* Type qualifiers */
    bool is_const;
    bool is_volatile;

    TypeInfo()
        : size(0)
        , total_elements(0)
        , is_const(false)
        , is_volatile(false)
    {}

    TypeInfo(TypeInfo&&) = default;
    TypeInfo& operator=(TypeInfo&&) = default;
};

/* DWARF base type encoding names */
static const char* get_encoding_name(int encoding) {
    switch (encoding) {
        case DW_ATE_address:        return "address";
        case DW_ATE_boolean:        return "boolean";
        case DW_ATE_complex_float:  return "complex_float";
        case DW_ATE_float:          return "float";
        case DW_ATE_signed:         return "signed";
        case DW_ATE_signed_char:    return "signed_char";
        case DW_ATE_unsigned:       return "unsigned";
        case DW_ATE_unsigned_char:  return "unsigned_char";
        default:                    return "unknown";
    }
}

/* ============================================================================
 * Error Handling
 * ============================================================================ */

const char* elf_parse_error_str(elf_parse_error_t err) {
    switch (err) {
        case ELF_PARSE_SUCCESS:          return "Success";
        case ELF_PARSE_FILE_NOT_FOUND:   return "File not found";
        case ELF_PARSE_NOT_ELF:          return "Not a valid ELF file";
        case ELF_PARSE_NO_DWARF:         return "No DWARF debug info";
        case ELF_PARSE_SYMBOL_NOT_FOUND: return "Symbol not found";
        case ELF_PARSE_NO_DEBUG_INFO:    return "No debug info for symbol";
        case ELF_PARSE_TYPE_ERROR:       return "Type parsing error";
        case ELF_PARSE_MEMORY_ERROR:     return "Memory allocation error";
        case ELF_PARSE_ERROR:            return "Unknown error";
        default:                         return "Invalid error code";
    }
}

/* ============================================================================
 * Symbol Table Access (ELFIO)
 * ============================================================================ */

bool elf_parser_find_symbol(elf_parser_ctx* parser, const char* name,
                            uint32_t* out_addr, uint32_t* out_size) {
    if (!parser || !name) return false;

    for (ELFIO::Elf_Half i = 0; i < parser->elf.sections.size(); i++) {
        ELFIO::section* section = parser->elf.sections[i];
        if (section->get_type() == ELFIO::SHT_SYMTAB ||
            section->get_type() == ELFIO::SHT_DYNSYM) {
            ELFIO::const_symbol_section_accessor symbols(parser->elf, section);
            for (ELFIO::Elf_Xword i = 0; i < symbols.get_symbols_num(); i++) {
                std::string sym_name;
                ELFIO::Elf64_Addr value;
                ELFIO::Elf_Xword size;
                unsigned char bind, type, other;
                ELFIO::Elf_Half section_index;

                if (symbols.get_symbol(i, sym_name, value, size, bind, type,
                                       section_index, other)) {
                    if (sym_name == name) {
                        if (out_addr) *out_addr = (uint32_t)value;
                        if (out_size) *out_size = (uint32_t)size;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/* ============================================================================
 * Parser Lifecycle
 * ============================================================================ */

elf_parse_error_t elf_parser_init(elf_parser_ctx* parser, const char* path)
{
    if (!parser || !path) return ELF_PARSE_ERROR;

    parser->path = path;
    parser->dbg = nullptr;
    parser->dwarf_initialized = false;

    /* Load ELF with ELFIO */
    if (!parser->elf.load(path)) {
        return ELF_PARSE_NOT_ELF;
    }

    /* Initialize libdwarf */
    Dwarf_Error err = nullptr;
    int res = dwarf_init_path(path, nullptr, 0, DW_GROUPNUMBER_ANY,
                              nullptr, nullptr, &parser->dbg, &err);
    if (res == DW_DLV_NO_ENTRY) {
        /* No DWARF info, but ELF is valid */
        parser->dwarf_initialized = false;
    } else if (res == DW_DLV_ERROR) {
        if (err) dwarf_dealloc_error(parser->dbg, err);
        return ELF_PARSE_NO_DWARF;
    } else {
        parser->dwarf_initialized = true;
    }

    return ELF_PARSE_SUCCESS;
}

void elf_parser_cleanup(elf_parser_ctx* parser) {
    if (!parser) return;

    if (parser->dwarf_initialized && parser->dbg) {
        dwarf_finish(parser->dbg);
        parser->dbg = nullptr;
        parser->dwarf_initialized = false;
    }
}

/* ============================================================================
 * DWARF Traversal Helpers
 * ============================================================================ */

/* Get a string attribute value from a DIE */
static bool get_die_name(Dwarf_Debug dbg, Dwarf_Die die, std::string& out) {
    char* name = nullptr;
    Dwarf_Error err = nullptr;
    int res = dwarf_diename(die, &name, &err);
    if (res == DW_DLV_OK && name) {
        out = name;
        return true;
    }
    if (err) dwarf_dealloc_error(dbg, err);
    return false;
}

/* Get an unsigned attribute value from a DIE */
static bool get_die_unsigned(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Half attr,
                             Dwarf_Unsigned* out) {
    Dwarf_Attribute at = nullptr;
    Dwarf_Error err = nullptr;

    int res = dwarf_attr(die, attr, &at, &err);
    if (res != DW_DLV_OK) {
        if (err) dwarf_dealloc_error(dbg, err);
        return false;
    }

    res = dwarf_formudata(at, out, &err);
    dwarf_dealloc_attribute(at);

    if (res != DW_DLV_OK) {
        if (err) dwarf_dealloc_error(dbg, err);
        return false;
    }
    return true;
}

/* Get a signed attribute value from a DIE */
static bool get_die_signed(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Half attr,
                           Dwarf_Signed* out) {
    Dwarf_Attribute at = nullptr;
    Dwarf_Error err = nullptr;

    int res = dwarf_attr(die, attr, &at, &err);
    if (res != DW_DLV_OK) {
        if (err) dwarf_dealloc_error(dbg, err);
        return false;
    }

    res = dwarf_formsdata(at, out, &err);
    dwarf_dealloc_attribute(at);

    if (res != DW_DLV_OK) {
        if (err) dwarf_dealloc_error(dbg, err);
        return false;
    }
    return true;
}

/* Get the offset of the type DIE referenced by DW_AT_type */
static bool get_type_ref_offset(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Off* out) {
    Dwarf_Attribute at = nullptr;
    Dwarf_Error err = nullptr;

    int res = dwarf_attr(die, DW_AT_type, &at, &err);
    if (res != DW_DLV_OK) {
        if (err) dwarf_dealloc_error(dbg, err);
        return false;
    }

    Dwarf_Off offset = 0;
    Dwarf_Bool is_info = true;
    res = dwarf_global_formref_b(at, &offset, &is_info, &err);
    dwarf_dealloc_attribute(at);

    if (res != DW_DLV_OK) {
        if (err) dwarf_dealloc_error(dbg, err);
        return false;
    }

    *out = offset;
    return true;
}

/* Get the data member location (byte offset) for a struct member */
static bool get_member_location(Dwarf_Debug dbg, Dwarf_Die die, uint32_t* out) {
    Dwarf_Attribute at = nullptr;
    Dwarf_Error err = nullptr;

    int res = dwarf_attr(die, DW_AT_data_member_location, &at, &err);
    if (res != DW_DLV_OK) {
        if (err) dwarf_dealloc_error(dbg, err);
        *out = 0;
        return true;  /* No location = offset 0 (e.g., first member or union) */
    }

    /* Try to get as unsigned first (common case for simple offsets) */
    Dwarf_Unsigned uval = 0;
    res = dwarf_formudata(at, &uval, &err);
    if (res == DW_DLV_OK) {
        dwarf_dealloc_attribute(at);
        *out = (uint32_t)uval;
        return true;
    }
    if (err) {
        dwarf_dealloc_error(dbg, err);
        err = nullptr;
    }

    /* Try as a location expression (DWARF4+) */
    Dwarf_Unsigned expr_len = 0;
    Dwarf_Ptr expr_ptr = nullptr;
    res = dwarf_formexprloc(at, &expr_len, &expr_ptr, &err);
    if (res == DW_DLV_OK && expr_len > 0) {
        /* Simple case: DW_OP_plus_uconst followed by ULEB128 value */
        const unsigned char* p = (const unsigned char*)expr_ptr;
        if (p[0] == DW_OP_plus_uconst && expr_len >= 2) {
            /* Decode ULEB128 */
            uint32_t result = 0;
            int shift = 0;
            size_t i = 1;
            while (i < expr_len) {
                unsigned char b = p[i++];
                result |= (uint32_t)(b & 0x7f) << shift;
                if ((b & 0x80) == 0) break;
                shift += 7;
            }
            dwarf_dealloc_attribute(at);
            *out = result;
            return true;
        }
    }
    if (err) dwarf_dealloc_error(dbg, err);

    dwarf_dealloc_attribute(at);
    *out = 0;
    return true;
}

/* ============================================================================
 * DWARF Type Resolution (Iterative)
 * ============================================================================ */

/* Type cache to avoid re-parsing the same type */
struct TypeCache {
    std::unordered_map<Dwarf_Off, std::unique_ptr<TypeInfo>> cache;

    TypeInfo* get(Dwarf_Off offset) {
        std::unordered_map<Dwarf_Off, std::unique_ptr<TypeInfo>>::iterator it = cache.find(offset);
        return (it != cache.end()) ? it->second.get() : nullptr;
    }

    void put(Dwarf_Off offset, std::unique_ptr<TypeInfo> ti) {
        cache[offset] = std::move(ti);
    }
};

/* Work item types for iterative traversal */
enum WorkType {
    WORK_RESOLVE_TYPE,      /* Resolve a type DIE */
    WORK_PARSE_MEMBER,      /* Parse a struct/union member */
};

struct DwarfWork {
    WorkType type;
    Dwarf_Off die_offset;
    TypeInfo* result;       /* Where to store result (raw ptr, owned elsewhere) */
    uint32_t base_offset;   /* For members: cumulative byte offset */
    int index;              /* For storing into parent's fields array */

    DwarfWork(WorkType t, Dwarf_Off off, TypeInfo* r, uint32_t base, int idx)
        : type(t), die_offset(off), result(r), base_offset(base), index(idx) {}
};

/* Find a variable DIE by name */
static bool find_variable_die(Dwarf_Debug dbg, const char* name,
                              Dwarf_Die* out_die, Dwarf_Off* out_type_offset) {
    Dwarf_Error err = nullptr;
    Dwarf_Bool is_info = true;
    Dwarf_Unsigned cu_header_length, abbrev_offset, next_cu_header;
    Dwarf_Half version_stamp, address_size, length_size, extension_size;
    Dwarf_Sig8 signature;
    Dwarf_Unsigned typeoffset;
    Dwarf_Half header_cu_type;

    /* Iterate through all CUs */
    while (true) {
        int res = dwarf_next_cu_header_d(dbg, is_info,
            &cu_header_length, &version_stamp, &abbrev_offset,
            &address_size, &length_size, &extension_size,
            &signature, &typeoffset, &next_cu_header, &header_cu_type, &err);

        if (res == DW_DLV_NO_ENTRY) break;
        if (res == DW_DLV_ERROR) {
            if (err) dwarf_dealloc_error(dbg, err);
            return false;
        }

        /* Get the CU DIE */
        Dwarf_Die cu_die = nullptr;
        res = dwarf_siblingof_b(dbg, nullptr, is_info, &cu_die, &err);
        if (res != DW_DLV_OK) {
            if (err) dwarf_dealloc_error(dbg, err);
            continue;
        }

        /* Search for variables in this CU using a stack */
        std::vector<Dwarf_Die> die_stack;
        die_stack.push_back(cu_die);

        while (!die_stack.empty()) {
            Dwarf_Die die = die_stack.back();
            die_stack.pop_back();

            Dwarf_Half tag;
            res = dwarf_tag(die, &tag, &err);
            if (res != DW_DLV_OK) {
                if (err) dwarf_dealloc_error(dbg, err);
                if (die != cu_die) dwarf_dealloc_die(die);
                continue;
            }

            if (tag == DW_TAG_variable) 
			{
                std::string var_name;
                if (get_die_name(dbg, die, var_name) && var_name == name) 
				{
					printf("Found correct name %s\n", var_name.c_str());
                    /* Found it! Get the type reference */
                    Dwarf_Off type_off;
                    if (get_type_ref_offset(dbg, die, &type_off)) 
					{
                        *out_die = die;
                        *out_type_offset = type_off;
                        /* Clean up remaining stack */
                        for (size_t j = 0; j < die_stack.size(); j++) {
                            if (die_stack[j] != cu_die) dwarf_dealloc_die(die_stack[j]);
                        }
                        return true;
                    }
                }
				else
				{
					// printf("Looked at %s, not correct\n", var_name.c_str());
				}
            }

            /* Add children to stack */
            Dwarf_Die child = nullptr;
            res = dwarf_child(die, &child, &err);
            if (res == DW_DLV_OK) {
                die_stack.push_back(child);
                /* Add siblings */
                while (true) {
                    Dwarf_Die sibling = nullptr;
                    res = dwarf_siblingof_b(dbg, child, is_info, &sibling, &err);
                    if (res != DW_DLV_OK) break;
                    die_stack.push_back(sibling);
                    child = sibling;
                }
            }
            if (err) dwarf_dealloc_error(dbg, err);

            if (die != cu_die) dwarf_dealloc_die(die);
        }
    }

    return false;
}

/* Resolve a type given its DIE offset - iterative implementation */
static std::unique_ptr<TypeInfo> resolve_type_iterative(Dwarf_Debug dbg, Dwarf_Off start_offset,
                                                        TypeCache& cache) {
    std::vector<DwarfWork> stack;
    std::unique_ptr<TypeInfo> root_result = std::make_unique<TypeInfo>();

    stack.emplace_back(WORK_RESOLVE_TYPE, start_offset, root_result.get(), 0, 0);

    while (!stack.empty()) {
        DwarfWork work = std::move(stack.back());
        stack.pop_back();

        Dwarf_Error err = nullptr;
        Dwarf_Die die = nullptr;

        if (work.type == WORK_RESOLVE_TYPE) {
            /* Check cache first */
            TypeInfo* cached = cache.get(work.die_offset);
            if (cached) {
                /* Copy cached type info */
                work.result->type = cached->type;
                work.result->name = cached->name;
                work.result->typedef_name = cached->typedef_name;
                work.result->size = cached->size;
                work.result->encoding = cached->encoding;
                work.result->dimensions = cached->dimensions;
                work.result->total_elements = cached->total_elements;
                work.result->is_const = cached->is_const;
                work.result->is_volatile = cached->is_volatile;
                work.result->enumerators = cached->enumerators;
                /* Note: fields and pointee need deep copy - skip for simplicity */
                continue;
            }

            /* Get DIE at offset */
            int res = dwarf_offdie_b(dbg, work.die_offset, true, &die, &err);
            if (res != DW_DLV_OK) {
                if (err) dwarf_dealloc_error(dbg, err);
                work.result->type = "unknown";
                continue;
            }

            Dwarf_Half tag;
            res = dwarf_tag(die, &tag, &err);
            if (res != DW_DLV_OK) {
                if (err) dwarf_dealloc_error(dbg, err);
                dwarf_dealloc_die(die);
                work.result->type = "unknown";
                continue;
            }

            switch (tag) {
                case DW_TAG_base_type: {
                    std::string name;
                    get_die_name(dbg, die, name);
                    work.result->type = name.empty() ? "unknown" : name;
                    work.result->name = name;

                    Dwarf_Unsigned size = 0;
                    if (get_die_unsigned(dbg, die, DW_AT_byte_size, &size)) {
                        work.result->size = (uint32_t)size;
                    }

                    Dwarf_Unsigned encoding = 0;
                    if (get_die_unsigned(dbg, die, DW_AT_encoding, &encoding)) {
                        work.result->encoding = get_encoding_name((int)encoding);
                    }
                    break;
                }

                case DW_TAG_typedef: {
                    std::string typedef_name;
                    get_die_name(dbg, die, typedef_name);

                    Dwarf_Off underlying_offset;
                    if (get_type_ref_offset(dbg, die, &underlying_offset)) {
                        /* Push work to resolve underlying type into same result */
                        stack.emplace_back(WORK_RESOLVE_TYPE, underlying_offset,
                                          work.result, 0, 0);
                        /* We'll set typedef after underlying is resolved */
                        work.result->typedef_name = typedef_name;
                    } else {
                        work.result->type = typedef_name;
                    }
                    break;
                }

                case DW_TAG_pointer_type: {
                    work.result->type = "pointer";
                    Dwarf_Unsigned size = 4;  /* Default 32-bit */
                    get_die_unsigned(dbg, die, DW_AT_byte_size, &size);
                    work.result->size = (uint32_t)size;

                    Dwarf_Off pointee_offset;
                    if (get_type_ref_offset(dbg, die, &pointee_offset)) {
                        work.result->pointee = std::make_unique<TypeInfo>();
                        stack.emplace_back(WORK_RESOLVE_TYPE, pointee_offset,
                                          work.result->pointee.get(), 0, 0);
                    }
                    break;
                }

                case DW_TAG_array_type: {
                    work.result->type = "array";

                    /* Get element type */
                    Dwarf_Off elem_offset;
                    if (get_type_ref_offset(dbg, die, &elem_offset)) {
                        /* Parse dimensions from children first */
                        Dwarf_Die child = nullptr;
                        res = dwarf_child(die, &child, &err);
                        while (res == DW_DLV_OK) {
                            Dwarf_Half child_tag;
                            if (dwarf_tag(child, &child_tag, &err) == DW_DLV_OK &&
                                child_tag == DW_TAG_subrange_type) {
                                Dwarf_Unsigned upper = 0;
                                Dwarf_Unsigned count = 0;
                                if (get_die_unsigned(dbg, child, DW_AT_count, &count)) {
                                    work.result->dimensions.push_back((uint32_t)count);
                                } else if (get_die_unsigned(dbg, child, DW_AT_upper_bound, &upper)) {
                                    work.result->dimensions.push_back((uint32_t)(upper + 1));
                                } else {
                                    work.result->dimensions.push_back(0);  /* Flexible array */
                                }
                            }
                            if (err) dwarf_dealloc_error(dbg, err);

                            Dwarf_Die sibling = nullptr;
                            res = dwarf_siblingof_b(dbg, child, true, &sibling, &err);
                            dwarf_dealloc_die(child);
                            child = sibling;
                        }
                        if (err) dwarf_dealloc_error(dbg, err);

                        /* Calculate total elements */
                        work.result->total_elements = 1;
                        for (uint32_t d : work.result->dimensions) {
                            work.result->total_elements *= d;
                        }

                        /* Create a single "element_type" field to hold element info */
                        work.result->fields.resize(1);
                        work.result->fields[0].name = "__element_type__";
                        work.result->fields[0].byte_offset = 0;
                        work.result->fields[0].type_info = std::make_unique<TypeInfo>();
                        stack.emplace_back(WORK_RESOLVE_TYPE, elem_offset,
                                          work.result->fields[0].type_info.get(), 0, 0);
                    }
                    break;
                }

                case DW_TAG_structure_type:
                case DW_TAG_union_type: {
                    work.result->type = (tag == DW_TAG_structure_type) ? "struct" : "union";
                    std::string name;
                    get_die_name(dbg, die, name);
                    work.result->name = name;

                    Dwarf_Unsigned size = 0;
                    get_die_unsigned(dbg, die, DW_AT_byte_size, &size);
                    work.result->size = (uint32_t)size;

                    /* Count members first */
                    std::vector<Dwarf_Off> member_offsets;
                    Dwarf_Die child = nullptr;
                    res = dwarf_child(die, &child, &err);
                    while (res == DW_DLV_OK) {
                        Dwarf_Half child_tag;
                        if (dwarf_tag(child, &child_tag, &err) == DW_DLV_OK &&
                            child_tag == DW_TAG_member) {
                            Dwarf_Off child_off;
                            if (dwarf_dieoffset(child, &child_off, &err) == DW_DLV_OK) {
                                member_offsets.push_back(child_off);
                            }
                        }
                        if (err) dwarf_dealloc_error(dbg, err);

                        Dwarf_Die sibling = nullptr;
                        res = dwarf_siblingof_b(dbg, child, true, &sibling, &err);
                        dwarf_dealloc_die(child);
                        child = sibling;
                    }
                    if (err) dwarf_dealloc_error(dbg, err);

                    /* Pre-allocate fields */
                    work.result->fields.resize(member_offsets.size());
                    for (size_t i = 0; i < member_offsets.size(); i++) {
                        work.result->fields[i].type_info = std::make_unique<TypeInfo>();
                    }

                    /* Push work items for each member (in reverse order) */
                    for (size_t i = member_offsets.size(); i > 0; i--) {
                        stack.emplace_back(WORK_PARSE_MEMBER, member_offsets[i-1],
                                          work.result, 0, (int)(i-1));
                    }
                    break;
                }

                case DW_TAG_enumeration_type: {
                    work.result->type = "enum";
                    std::string name;
                    get_die_name(dbg, die, name);
                    work.result->name = name;

                    Dwarf_Unsigned size = 4;
                    get_die_unsigned(dbg, die, DW_AT_byte_size, &size);
                    work.result->size = (uint32_t)size;

                    /* Parse enumerators */
                    Dwarf_Die child = nullptr;
                    res = dwarf_child(die, &child, &err);
                    while (res == DW_DLV_OK) {
                        Dwarf_Half child_tag;
                        if (dwarf_tag(child, &child_tag, &err) == DW_DLV_OK &&
                            child_tag == DW_TAG_enumerator) {
                            EnumValue ev;
                            get_die_name(dbg, child, ev.name);
                            Dwarf_Signed val = 0;
                            if (get_die_signed(dbg, child, DW_AT_const_value, &val)) {
                                ev.value = val;
                            }
                            work.result->enumerators.push_back(ev);
                        }
                        if (err) dwarf_dealloc_error(dbg, err);

                        Dwarf_Die sibling = nullptr;
                        res = dwarf_siblingof_b(dbg, child, true, &sibling, &err);
                        dwarf_dealloc_die(child);
                        child = sibling;
                    }
                    if (err) dwarf_dealloc_error(dbg, err);
                    break;
                }

                case DW_TAG_const_type: {
                    Dwarf_Off underlying_offset;
                    if (get_type_ref_offset(dbg, die, &underlying_offset)) {
                        work.result->is_const = true;
                        stack.emplace_back(WORK_RESOLVE_TYPE, underlying_offset,
                                          work.result, 0, 0);
                    } else {
                        work.result->type = "void";
                        work.result->is_const = true;
                    }
                    break;
                }

                case DW_TAG_volatile_type: {
                    Dwarf_Off underlying_offset;
                    if (get_type_ref_offset(dbg, die, &underlying_offset)) {
                        work.result->is_volatile = true;
                        stack.emplace_back(WORK_RESOLVE_TYPE, underlying_offset,
                                          work.result, 0, 0);
                    } else {
                        work.result->type = "void";
                        work.result->is_volatile = true;
                    }
                    break;
                }

                default:
                    work.result->type = "unknown";
                    Dwarf_Unsigned size = 0;
                    get_die_unsigned(dbg, die, DW_AT_byte_size, &size);
                    work.result->size = (uint32_t)size;
                    break;
            }

            dwarf_dealloc_die(die);
        }
        else if (work.type == WORK_PARSE_MEMBER) {
            /* Parse a struct/union member */
            int res = dwarf_offdie_b(dbg, work.die_offset, true, &die, &err);
            if (res != DW_DLV_OK) {
                if (err) dwarf_dealloc_error(dbg, err);
                continue;
            }

            TypeInfo* parent = work.result;
            FieldInfo& field = parent->fields[work.index];

            get_die_name(dbg, die, field.name);
            get_member_location(dbg, die, &field.byte_offset);

            /* Check for bitfields */
            Dwarf_Unsigned bit_size = 0;
            if (get_die_unsigned(dbg, die, DW_AT_bit_size, &bit_size)) {
                field.bit_size = (int)bit_size;
                Dwarf_Unsigned bit_offset = 0;
                Dwarf_Unsigned data_bit_offset = 0;
                if (get_die_unsigned(dbg, die, DW_AT_data_bit_offset, &data_bit_offset)) {
                    field.bit_offset = (int)data_bit_offset;
                } else if (get_die_unsigned(dbg, die, DW_AT_bit_offset, &bit_offset)) {
                    /* Convert from big-endian bit numbering */
                    field.bit_offset = (int)bit_offset;  /* Simplified */
                }
            } else {
                field.bit_size = -1;
                field.bit_offset = 0;
            }

            /* Get the member's type */
            Dwarf_Off type_offset;
            if (get_type_ref_offset(dbg, die, &type_offset)) {
                stack.emplace_back(WORK_RESOLVE_TYPE, type_offset,
                                  field.type_info.get(), 0, 0);
            }

            dwarf_dealloc_die(die);
        }
    }

    return root_result;
}

/* ============================================================================
 * Type to DarttField Conversion
 * ============================================================================ */

/* Get a simple type name from TypeInfo */
static std::string get_simple_type_name(const TypeInfo& ti) {
    if (!ti.typedef_name.empty()) {
        return ti.typedef_name;
    }

    if (ti.type == "pointer") {
        if (ti.pointee) {
            return get_simple_type_name(*ti.pointee) + "*";
        }
        return "void*";
    }

    if (ti.type == "array") {
        std::string result;
        if (!ti.fields.empty() && ti.fields[0].type_info) {
            result = get_simple_type_name(*ti.fields[0].type_info);
        }
        for (uint32_t d : ti.dimensions) {
            result += "[" + std::to_string(d) + "]";
        }
        return result;
    }

    if (ti.type == "struct" || ti.type == "union") {
        if (!ti.name.empty()) {
            return ti.type + " " + ti.name;
        }
        return ti.type;
    }

    if (ti.type == "enum") {
        if (!ti.name.empty()) {
            return "enum " + ti.name;
        }
        return "enum";
    }

    return ti.type;
}

/* Compute absolute dartt_offsets and convert TypeInfo tree to DarttField tree */
struct ConvertWork {
    const TypeInfo* type_info;
    const FieldInfo* field_info;  /* null for root */
    DarttField* out_field;
    uint32_t base_byte_offset;

    ConvertWork(const TypeInfo* ti, const FieldInfo* fi, DarttField* out, uint32_t base)
        : type_info(ti), field_info(fi), out_field(out), base_byte_offset(base) {}
};

static void type_info_to_dartt_field(const TypeInfo& root_ti, DarttField& root_field,
                                     uint32_t base_offset = 0) {
    std::vector<ConvertWork> stack;
    stack.emplace_back(&root_ti, nullptr, &root_field, base_offset);

    while (!stack.empty()) {
        ConvertWork work = std::move(stack.back());
        stack.pop_back();

        const TypeInfo& ti = *work.type_info;
        DarttField& field = *work.out_field;
        uint32_t abs_byte_offset = work.base_byte_offset;

        if (work.field_info) {
            field.name = work.field_info->name;
            abs_byte_offset = work.base_byte_offset + work.field_info->byte_offset;
        }

        field.byte_offset = abs_byte_offset;
        field.dartt_offset = abs_byte_offset / 4;
        field.nbytes = ti.size;
        field.type_name = get_simple_type_name(ti);
        field.type = parse_field_type(ti.type);

        if (ti.type == "struct" || ti.type == "union") {
            /* Pre-allocate children */
            field.children.resize(ti.fields.size());
            /* Push in reverse order */
            for (size_t i = ti.fields.size(); i > 0; i--) {
                if (ti.fields[i-1].type_info) {
                    stack.emplace_back(ti.fields[i-1].type_info.get(), &ti.fields[i-1],
                                      &field.children[i-1], abs_byte_offset);
                }
            }
        }
        else if (ti.type == "array") {
            field.array_size = ti.total_elements;
            if (!ti.fields.empty() && ti.fields[0].type_info) {
                field.element_nbytes = ti.fields[0].type_info->size;

                /* If array of structs/unions, create one child as template */
                if (ti.fields[0].type_info->type == "struct" ||
                    ti.fields[0].type_info->type == "union") {
                    field.children.resize(1);
                    stack.emplace_back(ti.fields[0].type_info.get(), nullptr,
                                      &field.children[0], abs_byte_offset);
                } else {
                    /* Primitive array - set type from element */
                    field.type = parse_field_type(ti.fields[0].type_info->type);
                    field.type_name = get_simple_type_name(*ti.fields[0].type_info);
                }
            }
        }
    }
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

elf_parse_error_t elf_parser_load_config(const char* elf_path,
                                         const char* symbol_name,
                                         DarttConfig* config) {
    if (!elf_path || !symbol_name || !config) return ELF_PARSE_ERROR;

    elf_parser_ctx parser;
    elf_parse_error_t err = elf_parser_init(&parser, elf_path);
    if (err != ELF_PARSE_SUCCESS) return err;

    /* Get symbol address and size */
    uint32_t sym_addr = 0, sym_size = 0;
    if (!elf_parser_find_symbol(&parser, symbol_name, &sym_addr, &sym_size)) {
        elf_parser_cleanup(&parser);
        return ELF_PARSE_SYMBOL_NOT_FOUND;
    }

    config->symbol = symbol_name;
    config->address = sym_addr;
    char addr_buf[32];
    snprintf(addr_buf, sizeof(addr_buf), "0x%08X", sym_addr);
    config->address_str = addr_buf;

    /* Check for DWARF info */
    if (!parser.dwarf_initialized) {
        elf_parser_cleanup(&parser);
        return ELF_PARSE_NO_DWARF;
    }

    /* Find the variable in DWARF */
    Dwarf_Die var_die = nullptr;
    Dwarf_Off type_offset = 0;
    if (!find_variable_die(parser.dbg, symbol_name, &var_die, &type_offset)) {
        elf_parser_cleanup(&parser);
        return ELF_PARSE_NO_DEBUG_INFO;
    }

    /* Resolve the type */
    TypeCache cache;
    std::unique_ptr<TypeInfo> type_info = resolve_type_iterative(parser.dbg, type_offset, cache);
    if (!type_info) {
        if (var_die) dwarf_dealloc_die(var_die);
        elf_parser_cleanup(&parser);
        return ELF_PARSE_TYPE_ERROR;
    }

    /* Set size */
    config->nbytes = (sym_size > 0) ? sym_size : type_info->size;
    config->nwords = (config->nbytes + 3) / 4;

    /* Convert to DarttField tree */
    config->root.name = symbol_name;
    type_info_to_dartt_field(*type_info, config->root, 0);

    /* Expand primitive arrays into element children, then collect leaves */
    expand_array_elements(config->root);
    collect_leaves(config->root, config->leaf_list);

    printf("Loaded config from ELF: symbol=%s, address=0x%08X, nbytes=%u, nwords=%u\n",
           config->symbol.c_str(), config->address, config->nbytes, config->nwords);

    if (var_die) dwarf_dealloc_die(var_die);
    elf_parser_cleanup(&parser);
    return ELF_PARSE_SUCCESS;
}

/* ============================================================================
 * JSON Generation (matches dartt-describe.py output format)
 * ============================================================================ */

#include <nlohmann/json.hpp>

using json = nlohmann::json;

/* Convert TypeInfo to JSON (recursive helper for internal structure) */
static json type_info_to_json(const TypeInfo& ti) {
    json j;
    j["type"] = ti.type;

    if (!ti.typedef_name.empty()) {
        j["typedef"] = ti.typedef_name;
    }

    if (ti.size > 0) {
        j["size"] = ti.size;
    }

    if (!ti.encoding.empty()) {
        j["encoding"] = ti.encoding;
    }

    if (ti.type == "struct" || ti.type == "union") {
        if (!ti.name.empty()) {
            j[ti.type + "_name"] = ti.name;
        }
        json fields = json::array();
        for (size_t i = 0; i < ti.fields.size(); i++) {
            const FieldInfo& f = ti.fields[i];
            json fj;
            fj["name"] = f.name;
            fj["byte_offset"] = f.byte_offset;
            if (f.type_info) {
                fj["type_info"] = type_info_to_json(*f.type_info);
            }
            if (f.bit_size >= 0) {
                fj["bit_size"] = f.bit_size;
                fj["bit_offset"] = f.bit_offset;
            }
            fields.push_back(fj);
        }
        j["fields"] = fields;
    }
    else if (ti.type == "array") {
        j["dimensions"] = ti.dimensions;
        j["total_elements"] = ti.total_elements;
        if (!ti.fields.empty() && ti.fields[0].type_info) {
            j["element_type"] = type_info_to_json(*ti.fields[0].type_info);
        }
    }
    else if (ti.type == "pointer") {
        if (ti.pointee) {
            j["pointee"] = type_info_to_json(*ti.pointee);
        }
    }
    else if (ti.type == "enum") {
        if (!ti.name.empty()) {
            j["enum_name"] = ti.name;
        }
        json enumerators_json = json::array();
        for (size_t i = 0; i < ti.enumerators.size(); i++) {
            const EnumValue& ev = ti.enumerators[i];
            json ej;
            ej["name"] = ev.name;
            ej["value"] = ev.value;
            enumerators_json.push_back(ej);
        }
        j["enumerators"] = enumerators_json;
    }

    if (ti.is_const) j["const"] = true;
    if (ti.is_volatile) j["volatile"] = true;

    return j;
}

/* Add absolute dartt_offset to JSON (modifies in place) */
static void compute_json_dartt_offsets(json& type_json, uint32_t base_offset) {
    std::string type = type_json.value("type", "");

    if (type == "struct" || type == "union") {
        if (type_json.contains("fields")) {
            json& fields_array = type_json["fields"];
            for (size_t i = 0; i < fields_array.size(); i++) {
                json& field = fields_array[i];
                uint32_t rel_offset = field.value("byte_offset", 0u);
                uint32_t abs_offset = base_offset + rel_offset;
                field["byte_offset"] = abs_offset;
                field["dartt_offset"] = abs_offset / 4;

                if (abs_offset % 4 != 0) {
                    field["unaligned"] = true;
                }

                if (field.contains("type_info")) {
                    compute_json_dartt_offsets(field["type_info"], abs_offset);
                }
            }
        }
    }
    else if (type == "array") {
        if (type_json.contains("element_type")) {
            std::string elem_type = type_json["element_type"].value("type", "");
            if (elem_type == "struct" || elem_type == "union") {
                compute_json_dartt_offsets(type_json["element_type"], base_offset);
            }
        }
    }
}

elf_parse_error_t elf_parser_generate_json(elf_parser_ctx* parser,
                                           const char* symbol_name,
                                           const char* output_path) {
    if (!parser || !symbol_name) return ELF_PARSE_ERROR;

    /* Get symbol address and size */
    uint32_t sym_addr = 0, sym_size = 0;
    if (!elf_parser_find_symbol(parser, symbol_name, &sym_addr, &sym_size)) {
        return ELF_PARSE_SYMBOL_NOT_FOUND;
    }

    /* Check for DWARF info */
    if (!parser->dwarf_initialized) {
        return ELF_PARSE_NO_DWARF;
    }

    /* Find the variable in DWARF */
    Dwarf_Die var_die = nullptr;
    Dwarf_Off type_offset = 0;
    if (!find_variable_die(parser->dbg, symbol_name, &var_die, &type_offset)) {
        return ELF_PARSE_NO_DEBUG_INFO;
    }

    /* Resolve the type */
    TypeCache cache;
    std::unique_ptr<TypeInfo> type_info = resolve_type_iterative(parser->dbg, type_offset, cache);
    if (!type_info) {
        if (var_die) dwarf_dealloc_die(var_die);
        return ELF_PARSE_TYPE_ERROR;
    }

    /* Build output JSON */
    uint32_t total_nbytes = (sym_size > 0) ? sym_size : type_info->size;
    json output;
    output["symbol"] = symbol_name;

    char addr_buf[32];
    snprintf(addr_buf, sizeof(addr_buf), "0x%08X", sym_addr);
    output["address"] = addr_buf;
    output["address_int"] = sym_addr;
    output["nbytes"] = total_nbytes;
    output["nwords"] = (total_nbytes + 3) / 4;

    json type_json = type_info_to_json(*type_info);
    compute_json_dartt_offsets(type_json, 0);
    output["type"] = type_json;

    /* Write output */
    std::string json_str = output.dump(2);

    if (output_path) {
        std::ofstream f(output_path);
        if (!f.is_open()) {
            if (var_die) dwarf_dealloc_die(var_die);
            return ELF_PARSE_ERROR;
        }
        f << json_str << "\n";
        f.close();
        fprintf(stderr, "Wrote %s\n", output_path);
    } else {
        printf("%s\n", json_str.c_str());
    }

    if (var_die) dwarf_dealloc_die(var_die);
    return ELF_PARSE_SUCCESS;
}
