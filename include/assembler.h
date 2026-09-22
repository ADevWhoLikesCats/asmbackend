#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include "target.h"

/*
 * Assemble-only driver.
 *
 *   - Write assembly text to a .s file.
 *   - Run GNU as (GAS) on it to produce a .o file.
 *   - Do not link.  That is the caller's job.
 *
 * `as` is discovered from PATH.  The caller can override it via
 * toolchain_set_as() if they want a specific one (e.g. a cross-as
 * called aarch64-linux-gnu-as).
 */

typedef struct {
    Target      target;
    const char *as_program;   /* NULL -> "as" */
    int         verbose;      /* 1 -> print the command line */
} Toolchain;

void toolchain_init(Toolchain *tc, Target t);
void toolchain_set_as(Toolchain *tc, const char *as_program);
void toolchain_set_verbose(Toolchain *tc, int verbose);

/* Assemble asm_path -> obj_path using GAS.  Returns 0 on success. */
int toolchain_assemble(const Toolchain *tc,
                       const char *asm_path,
                       const char *obj_path);

/* Write asm_src to <work_dir>/<base>.s, assemble to <work_dir>/<base>.o. */
int toolchain_assemble_source(const Toolchain *tc,
                              const char *asm_src,
                              const char *work_dir,
                              const char *base);

#endif
