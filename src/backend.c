#include "backend.h"
#include <stdlib.h>
#include <string.h>

Backend *backend_for(Target t) {
    switch (t.arch) {
        case ARCH_X86:     return x86_backend_new();
        case ARCH_X86_64:  return x86_64_backend_new();
        case ARCH_ARM:     return arm_backend_new();
        case ARCH_ARM64:   return arm64_backend_new();
        case ARCH_RISCV64: return riscv_backend_new();
    }
    return NULL;
}

void backend_free(Backend *b) {
    if (!b) return;
    if (b->destroy) b->destroy(b);
    free(b);
}

char *backend_compile_to_asm(Backend *b, const Program *p, Target t) {
    Emitter *e = emitter_new(t);
    b->emit(b, p, e);
    return emitter_finish(e);   /* takes ownership of Emitter, returns malloc'd string */
}
