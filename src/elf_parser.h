/*
 * elf_parser.h - ELF/DWARF parser for extracting struct layouts
 *
 * Replaces the Python-based dartt-describe.py workflow with direct C++ parsing.
 *
 * Usage:
 *   elf_parser_ctx parser;
 *   if (elf_parser_init(&parser, "firmware.elf") == ELF_PARSE_SUCCESS) {
 *       elf_parser_generate_json(&parser, "gl_dp", "config.json");
 *       elf_parser_cleanup(&parser);
 *   }
 *
 *   // Or use the all-in-one function:
 *   DarttConfig config;
 *   elf_parser_load_config("firmware.elf", "gl_dp", &config);
 */

#ifndef DARTT_ELF_PARSER_H
#define DARTT_ELF_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <string>
#include <elfio/elfio.hpp>
#include <libdwarf.h>

/* Error codes returned by elf_parser functions */
typedef enum {
    ELF_PARSE_SUCCESS = 0,
    ELF_PARSE_FILE_NOT_FOUND,
    ELF_PARSE_NOT_ELF,
    ELF_PARSE_NO_DWARF,
    ELF_PARSE_SYMBOL_NOT_FOUND,
    ELF_PARSE_NO_DEBUG_INFO,
    ELF_PARSE_TYPE_ERROR,
    ELF_PARSE_MEMORY_ERROR,
    ELF_PARSE_ERROR
} elf_parse_error_t;

/* Returns a human-readable string for an error code */
const char* elf_parse_error_str(elf_parse_error_t err);

/* Parser context - holds ELFIO and libdwarf state */
struct elf_parser_ctx {
    ELFIO::elfio elf;       /* ELFIO reader for symbol table access */
    Dwarf_Debug dbg;        /* libdwarf debug handle */
    std::string path;       /* path to the ELF file */
    bool dwarf_initialized; /* true if libdwarf was successfully initialized */

    elf_parser_ctx() : dbg(nullptr), dwarf_initialized(false) {}
};

/*
 * Initialize parser and load an ELF file.
 *
 * @param parser Pointer to parser struct (caller-allocated, e.g. on stack)
 * @param path   Path to the ELF file
 * @return       ELF_PARSE_SUCCESS or error code
 */
elf_parse_error_t elf_parser_init(elf_parser_ctx* parser, const char* path);

/*
 * Clean up parser internal resources (libdwarf handle, etc).
 * Does not free the parser struct itself.
 *
 * @param parser Pointer to parser struct
 */
void elf_parser_cleanup(elf_parser_ctx* parser);

/*
 * Find a symbol in the ELF symbol table.
 *
 * @param parser   Parser pointer
 * @param name     Symbol name to search for
 * @param out_addr Receives the symbol's address (can be NULL)
 * @param out_size Receives the symbol's size in bytes (can be NULL)
 * @return         true if symbol found, false otherwise
 */
bool elf_parser_find_symbol(elf_parser_ctx* parser, const char* name,
                            uint32_t* out_addr, uint32_t* out_size);

/*
 * Generate JSON output matching dartt-describe.py format.
 *
 * @param parser      Parser pointer
 * @param symbol_name Name of the global variable to describe
 * @param output_path Path to write JSON file, or NULL for stdout
 * @return            ELF_PARSE_SUCCESS or error code
 */
elf_parse_error_t elf_parser_generate_json(
    elf_parser_ctx* parser,
    const char* symbol_name,
    const char* output_path
);

/* Forward declaration for DarttConfig */
struct DarttConfig;

/*
 * Load a DarttConfig directly from an ELF file.
 *
 * This is the high-level function that replaces the Python workflow:
 *   [ELF] -> [Python: dartt-describe.py] -> [JSON] -> [C++: load_dartt_config]
 *
 * With:
 *   [ELF] -> [C++: elf_parser_load_config]
 *
 * @param elf_path    Path to the ELF file with DWARF debug info
 * @param symbol_name Name of the global variable to parse
 * @param config      DarttConfig to populate
 * @return            ELF_PARSE_SUCCESS or error code
 */
elf_parse_error_t elf_parser_load_config(
    const char* elf_path,
    const char* symbol_name,
    DarttConfig* config
);

#endif /* DARTT_ELF_PARSER_H */
