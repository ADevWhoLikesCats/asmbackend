/* src/assembler.c */
#define _POSIX_C_SOURCE 200809L
#include "assembler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

void toolchain_init_clang(Toolchain *tc, Target t) {
    tc->target = t; tc->assembler = NULL; tc->linker = NULL; tc->use_clang = 1;
}
void toolchain_init_native(Toolchain *tc, Target t) {
    tc->target = t; tc->assembler = NULL; tc->linker = NULL; tc->use_clang = 0;
}

/* Run argv, print on failure. Returns exit status or -1. */
static int run(char *const argv[]) {
    /* tiny debug print */
    fprintf(stderr, "+");
    for (int i = 0; argv[i]; ++i) fprintf(stderr, " %s", argv[i]);
    fprintf(stderr, "\n");

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
}

int toolchain_assemble(const Toolchain *tc, const char *asm_path, const char *obj_path) {
    if (tc->use_clang) {
        char *argv[16]; int n = 0;
        argv[n++] = (char*)"clang";
        argv[n++] = (char*)"--target"; argv[n++] = (char*)tc->target.triple;
        argv[n++] = (char*)"-c";
        argv[n++] = (char*)asm_path;
        argv[n++] = (char*)"-o";
        argv[n++] = (char*)obj_path;
        if (tc->target.gas_march) {
            static char march[64];
            snprintf(march, sizeof march, "-march=%s", tc->target.gas_march);
            argv[n++] = march;
        }
        argv[n] = NULL;
        return run(argv);
    } else {
        char as_name[128];
        if (tc->assembler) snprintf(as_name, sizeof as_name, "%s", tc->assembler);
        else snprintf(as_name, sizeof as_name, "%s-as", tc->target.triple);

        char *argv[16]; int n = 0;
        argv[n++] = as_name;
        /* extra flags from backend would go here; for simplicity we skip */
        argv[n++] = (char*)asm_path;
        argv[n++] = (char*)"-o";
        argv[n++] = (char*)obj_path;
        argv[n] = NULL;
        return run(argv);
    }
}

int toolchain_link(const Toolchain *tc, const char **objs, size_t nobjs,
                   const char *out_path, const char **libs, size_t nlibs) {
    char *argv[64]; int n = 0;
    if (tc->use_clang) {
        argv[n++] = (char*)"clang";
        argv[n++] = (char*)"--target"; argv[n++] = (char*)tc->target.triple;
    } else {
        static char ld_name[128];
        if (tc->linker) snprintf(ld_name, sizeof ld_name, "%s", tc->linker);
        else snprintf(ld_name, sizeof ld_name, "%s-ld", tc->target.triple);
        argv[n++] = ld_name;
    }
    for (size_t i = 0; i < nobjs; ++i) argv[n++] = (char*)objs[i];
    argv[n++] = (char*)"-o"; argv[n++] = (char*)out_path;
    for (size_t i = 0; i < nlibs; ++i) argv[n++] = (char*)libs[i];
    argv[n] = NULL;
    return run(argv);
}

int toolchain_build_executable(const Toolchain *tc,
                               const char *asm_src,
                               const char *work_dir,
                               const char *out_name,
                               const char **libs, size_t nlibs,
                               char **out_bin) {
    char spath[512], opath[512], bpath[512];
    snprintf(spath, sizeof spath, "%s/%s.s", work_dir, out_name);
    snprintf(opath, sizeof opath, "%s/%s.o", work_dir, out_name);
    snprintf(bpath, sizeof bpath, "%s/%s",   work_dir, out_name);

    mkdir(work_dir, 0755);

    FILE *f = fopen(spath, "w");
    if (!f) { perror("fopen"); return -1; }
    fputs(asm_src, f);
    fclose(f);

    if (toolchain_assemble(tc, spath, opath) != 0) return -1;

    const char *objs[] = { opath };
    if (toolchain_link(tc, objs, 1, bpath, libs, nlibs) != 0) return -1;

    if (out_bin) *out_bin = strdup(bpath);
    return 0;
}
