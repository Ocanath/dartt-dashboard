/*
 * elf_parser.h - C API for parsing ELF files with DWARF debug info
 *
 * This module provides direct ELF/DWARF parsing to extract struct layouts,
 * replacing the Python-based dartt-describe.py workflow.
 *
 * Usage:
 *   elf_parser_t parser;
 *   if (elf_parser_open("firmware.elf", &parser) == ELF_PARSE_SUCCESS) {
 *       DarttConfig config;
 *       elf_parser_load_config(parser, "gl_dp", &config);
 *       elf_parser_close(parser);
 *   }
 */

#ifndef DARTT_ELF_PARSER_H
#define DARTT_ELF_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/* Opaque parser handle */
typedef struct elf_parser_ctx* elf_parser_t;

/*
 * Open an ELF file for parsing.
 *
 * @param path       Path to the ELF file
 * @param out_parser Receives the parser handle on success
 * @return           ELF_PARSE_SUCCESS or error code
 */
elf_parse_error_t elf_parser_open(const char* path, elf_parser_t* out_parser);

/*
 * Close an ELF parser and free resources.
 *
 * @param parser Parser handle from elf_parser_open()
 */
void elf_parser_close(elf_parser_t parser);

/*
 * Find a symbol in the ELF symbol table.
 *
 * @param parser   Parser handle
 * @param name     Symbol name to search for
 * @param out_addr Receives the symbol's address (can be NULL)
 * @param out_size Receives the symbol's size in bytes (can be NULL)
 * @return         true if symbol found, false otherwise
 */
bool elf_parser_find_symbol(elf_parser_t parser, const char* name,
                            uint32_t* out_addr, uint32_t* out_size);

/*
 * Generate JSON output matching dartt-describe.py format.
 *
 * @param parser      Parser handle
 * @param symbol_name Name of the global variable to describe
 * @param output_path Path to write JSON file, or NULL for stdout
 * @return            ELF_PARSE_SUCCESS or error code
 */
elf_parse_error_t elf_parser_generate_json(
    elf_parser_t parser,
    const char* symbol_name,
    const char* output_path
);

#ifdef __cplusplus
}

/* C++ interface for DarttConfig integration */
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

#endif /* __cplusplus */

#endif /* DARTT_ELF_PARSER_H */
