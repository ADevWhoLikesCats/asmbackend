#include "ir.h"
#include "backend.h"
#include "assembler.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * Minimal driver: build a small program in IR, print the assembly for
 * each target, then (if a toolchain is available) assemble and link it.
 *
 * On Windows/MSYS2 the toolchain step won't find a cross-clang, so the
 * assemble/link calls will fail with "No such file or directory".  That
 * is expected.  Do the assemble/run step in WSL or a Linux container.
 */

static Program *build_return_42(void) {
    Program *p = program_new();
    Function *f = program_add_function(p, "main");
    function_add_op(f, op_mov(0, val_imm(42)));
    function_add_op(f, op_ret(1, val_reg(0)));
    return p;
}

static Program *build_exit_7(void) {
    Program *p = program_new();
    program_add_extern(p, "exit");
    Function *f = program_add_function(p, "main");
    Value args[] = { val_imm(7) };
    function_add_op(f, op_call("exit", args, 1));
    function_add_op(f, op_ret(0, val_imm(0)));
    value_free(&args[0]);
    return p;
}

static const char *arch_short(Arch a) {
    switch (a) {
    case ARCH_X86:     return "x86";
    case ARCH_X86_64:  return "x86_64";
    case ARCH_ARM:     return "arm";
    case ARCH_ARM64:   return "arm64";
    case ARCH_RISCV64: return "riscv64";
    }
    return "unknown";
}

int main(void) {
    Target targets[] = {
        target_x86_64(),
        target_arm64(),
        target_riscv64(),
    };

    Program *programs[] = {
        build_return_42(),
        build_exit_7(),
    };

    const char *names[] = { "return_42", "exit_7" };
    size_t ntargets = sizeof targets / sizeof targets[0];
    size_t nprograms = sizeof programs / sizeof programs[0];

    for (size_t pi = 0; pi < nprograms; ++pi) {
        printf("\n########## program: %s ##########\n", names[pi]);
        for (size_t ti = 0; ti < ntargets; ++ti) {
            Backend *b = backend_for(targets[ti]);
            if (!b) {
                fprintf(stderr, "no backend for %s\n",
                        arch_short(targets[ti].arch));
                continue;
            }

            char *asm_src = backend_compile_to_asm(b, programs[pi], targets[ti]);
            printf("\n=== %s / %s ===\n%s",
                   names[pi], targets[ti].triple, asm_src);

            /*
             * Assemble and link if a toolchain is available.  On MSYS2
             * this will fail with "No such file or directory" because
             * there is no cross-clang.  That's fine: the assembly above
             * is the useful output on this host.
             */
            Toolchain tc;
            toolchain_init_clang(&tc, targets[ti]);
            char *bin = NULL;
            char out_name[128];
            snprintf(out_name, sizeof out_name, "%s_%s",
                     names[pi], arch_short(targets[ti].arch));

            if (toolchain_build_executable(&tc, asm_src,
                                           "/tmp/asmbackend",
                                           out_name,
                                           NULL, 0, &bin) == 0) {
                printf("built: %s\n", bin);
            } else {
                printf("(toolchain unavailable on this host — skipped)\n");
            }

            free(bin);
            free(asm_src);
            backend_free(b);
        }
    }

    for (size_t i = 0; i < nprograms; ++i)
        program_free(programs[i]);

    return 0;
}
