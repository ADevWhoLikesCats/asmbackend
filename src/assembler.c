#define _POSIX_C_SOURCE 200809L
#include "assembler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <process.h>
    #include <direct.h>
    #define mkdir_one(p) _mkdir(p)
#else
    #include <sys/stat.h>
    #include <sys/wait.h>
    #include <unistd.h>
    #define mkdir_one(p) mkdir((p), 0755)
#endif

/* ------------------------------------------------------------------ *
 * Lifecycle
 * ------------------------------------------------------------------ */

void toolchain_init(Toolchain *tc, Target t) {
    tc->target     = t;
    tc->as_program = NULL;
    tc->verbose    = 0;
}

void toolchain_set_as(Toolchain *tc, const char *as_program) {
    tc->as_program = as_program;
}

void toolchain_set_verbose(Toolchain *tc, int verbose) {
    tc->verbose = verbose;
}

/* ------------------------------------------------------------------ *
 * Assembler selection
 *
 * `as` is a per-architecture tool.  Alpine names its cross-assemblers
 * after the full triplet, e.g. aarch64-alpine-linux-musl-as.  On other
 * distros the name differs (Debian uses aarch64-linux-gnu-as, and its
 * native as is x86-only).  We hard-code Alpine's names here because
 * that's what this project targets; users can override via
 * toolchain_set_as() or the `-a` flag on the demo driver.
 * ------------------------------------------------------------------ */

static const char *default_as_for(Target t) {
    switch (t.arch) {
    case ARCH_X86:     return "as";                          /* + --32 */
    case ARCH_X86_64:  return "as";
    case ARCH_ARM:     return "arm-linux-gnueabihf-as";     /* Debian/Ubuntu */
    case ARCH_ARM64:   return "aarch64-linux-gnu-as";
    case ARCH_RISCV64: return "riscv64-linux-gnu-as";
    }
    return "as";
}

/* ------------------------------------------------------------------ *
 * Process spawning
 * ------------------------------------------------------------------ */

static int spawn(const Toolchain *tc, char *const argv[]) {
    if (tc->verbose) {
        fprintf(stderr, "+");
        for (int i = 0; argv[i]; ++i) fprintf(stderr, " %s", argv[i]);
        fprintf(stderr, "\n");
    }
#ifdef _WIN32
    intptr_t rc = _spawnvp(_P_WAIT, argv[0], (const char *const *)argv);
    return rc == 0 ? 0 : -1;
#else
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) {
        execvp(argv[0], argv);
        perror("execvp");
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) { perror("waitpid"); return -1; }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

/* ------------------------------------------------------------------ *
 * Public API
 * ------------------------------------------------------------------ */

int toolchain_assemble(const Toolchain *tc,
                       const char *asm_path,
                       const char *obj_path) {
    const char *as = tc->as_program
                   ? tc->as_program
                   : default_as_for(tc->target);

    char *argv[8];
    int n = 0;
    argv[n++] = (char *)as;

    /* 32-bit x86 needs --32; without it GAS rejects pushl/popl. */
    if (tc->target.arch == ARCH_X86) {
        argv[n++] = (char *)"--32";
    }

    /* RISC-V needs -march to enable the extensions we emit. */
    if (tc->target.arch == ARCH_RISCV64 && tc->target.gas_march) {
        static char march[64];
        snprintf(march, sizeof march, "-march=%s", tc->target.gas_march);
        argv[n++] = march;
    }

    argv[n++] = (char *)asm_path;
    argv[n++] = (char *)"-o";
    argv[n++] = (char *)obj_path;
    argv[n]   = NULL;

    return spawn(tc, argv);
}

int toolchain_assemble_source(const Toolchain *tc,
                              const char *asm_src,
                              const char *work_dir,
                              const char *base) {
    char spath[512];
    char opath[512];
    snprintf(spath, sizeof spath, "%s/%s.s", work_dir, base);
    snprintf(opath, sizeof opath, "%s/%s.o", work_dir, base);

    mkdir_one(work_dir);

    FILE *f = fopen(spath, "w");
    if (!f) { perror("fopen"); return -1; }
    fputs(asm_src, f);
    fclose(f);

    return toolchain_assemble(tc, spath, opath);
}
