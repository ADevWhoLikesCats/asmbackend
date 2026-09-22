#include "ir.h"
#include "backend.h"
#include "assembler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s [options]\n"
        "\n"
        "Emits assembly for several targets and runs `as` on each.\n"
        "Output goes to ./out/.\n"
        "\n"
        "options:\n"
        "  -v           verbose: print the `as` command line\n"
        "  -d DIR       output directory (default: out)\n"
        "  -a PATH      path to the assembler (default: as)\n"
        "  -t TARGET    only emit this target (repeatable).\n"
        "               one of: x86, x86_64, arm, arm64, riscv64, all (default)\n"
        "  -h           this message\n",
        argv0);
}

static int target_matches(const char *want, Arch a) {
    if (strcmp(want, "all") == 0) return 1;
    return strcmp(want, arch_short(a)) == 0;
}

int main(int argc, char **argv) {
    const char *out_dir = "out";
    const char *as_path = NULL;
    int verbose = 0;
    const char *only[8];
    size_t nonly = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
            as_path = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            if (nonly < sizeof only / sizeof only[0])
                only[nonly++] = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    Target targets[] = {
        target_x86(),
        target_x86_64(),
        target_arm(),
        target_arm64(),
        target_riscv64(),
    };
    size_t ntargets = sizeof targets / sizeof targets[0];

    Program *programs[] = {
        build_return_42(),
        build_exit_7(),
    };
    const char *pnames[] = { "return_42", "exit_7" };
    size_t nprograms = sizeof programs / sizeof programs[0];

    int failures = 0;
    int emitted  = 0;

    for (size_t pi = 0; pi < nprograms; ++pi) {
        for (size_t ti = 0; ti < ntargets; ++ti) {
            Arch a = targets[ti].arch;

            int wanted = (nonly == 0);
            for (size_t k = 0; k < nonly && !wanted; ++k)
                if (target_matches(only[k], a))
                    wanted = 1;
            if (!wanted) continue;

            Backend *b = backend_for(targets[ti]);
            if (!b) {
                fprintf(stderr, "no backend for %s\n", arch_short(a));
                continue;
            }

            char *asm_src = backend_compile_to_asm(b, programs[pi], targets[ti]);

            char base[128];
            snprintf(base, sizeof base, "%s_%s", pnames[pi], arch_short(a));

            Toolchain tc;
            toolchain_init(&tc, targets[ti]);
            toolchain_set_verbose(&tc, verbose);
            if (as_path) toolchain_set_as(&tc, as_path);

            int rc = toolchain_assemble_source(&tc, asm_src, out_dir, base);

            if (rc == 0) {
                printf("ok   %s/%s.o\n", out_dir, base);
                emitted++;
            } else {
                printf("FAIL %s/%s.s  (as returned non-zero; "
                       "assembler may be missing or wrong for this target)\n",
                       out_dir, base);
                failures++;
            }

            free(asm_src);
            backend_free(b);
        }
    }

    for (size_t i = 0; i < nprograms; ++i)
        program_free(programs[i]);

    if (emitted == 0 && failures == 0) {
        fprintf(stderr, "no targets matched the -t filter\n");
        return 1;
    }

    if (failures) {
        fprintf(stderr, "%d of %d failed\n", failures, emitted + failures);
        return 1;
    }

    printf("done: %d object file(s) in %s/\n", emitted, out_dir);
    return 0;
}
