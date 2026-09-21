/* include/assembler.h */
#ifndef ASSEMBLER_H
#define ASSEMBLER_H


#include <stddef.h>
#include "target.h"

typedef struct {
    Target      target;
    const char *assembler;   /* NULL = derive from triple, e.g. "<triple>-as" */
    const char *linker;      /* NULL = derive from triple */
    int         use_clang;   /* 1 = use clang driver (recommended) */
} Toolchain;

void   toolchain_init_clang(Toolchain *tc, Target t);
void   toolchain_init_native(Toolchain *tc, Target t);

int    toolchain_assemble(const Toolchain *tc, const char *asm_path, const char *obj_path);
int    toolchain_link(const Toolchain *tc, const char **objs, size_t nobjs,
                      const char *out_path, const char **libs, size_t nlibs);

/* All-in-one: write asm to work_dir/name.s, assemble, link.
   Returns 0 on success. bin_path is written into `out_bin` (owned). */
int    toolchain_build_executable(const Toolchain *tc,
                                  const char *asm_src,
                                  const char *work_dir,
                                  const char *out_name,
                                  const char **libs, size_t nlibs,
                                  char **out_bin);

#endif
