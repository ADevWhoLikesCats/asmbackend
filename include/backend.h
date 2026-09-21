/* include/backend.h */
#ifndef BACKEND_H
#define BACKEND_H

#include "ir.h"
#include "emitter.h"
#include "target.h"

/*
 * A backend is a struct of function pointers + an opaque self.
 * This mirrors Rust's `Box<dyn Backend>`.
 */
typedef struct Backend Backend;

struct Backend {
    void  (*emit)(Backend *self, const Program *p, Emitter *e);
    void  (*gas_flags)(Backend *self, const char ***out, size_t *count);
    void  (*destroy)(Backend *self);
    void   *self;   /* per-arch state (usually NULL) */
};

/* Pick the right backend for a target. Caller frees via backend_free(). */
Backend *backend_for(Target t);
void     backend_free(Backend *b);

/* Convenience pipeline (mirrors compile_to_asm) */
char *backend_compile_to_asm(Backend *b, const Program *p, Target t);   /* owned string */

/* Backends register themselves through these: */
Backend *x86_backend_new(void);
Backend *x86_64_backend_new(void);
Backend *arm_backend_new(void);
Backend *arm64_backend_new(void);
Backend *riscv_backend_new(void);

#endif
