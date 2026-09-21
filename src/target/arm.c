#include "../include/backend.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * 32-bit ARM, AAPCS, AT&T-flavored GAS.
 *
 * Conventions used here:
 *   - Virtual registers map to stack slots at -4*(vreg+1)(fp) where fp=r11.
 *   - %r0..%r3 carry the first four arguments; the rest go on the stack.
 *   - Return value is in %r0.
 *   - We use %r4 as a scratch for arithmetic.
 *   - We emit .syntax unified so both ARM and Thumb encodings are accepted
 *     by GAS; you could also switch to .thumb if you want.
 *   - Public interfaces must be 8-byte aligned per AAPCS.  Our prologue
 *     rounds the frame to a multiple of 8.
 */

static void fmt_value(const Value *v, char *out, size_t n) {
    switch (v->kind) {
        case VAL_IMM:   snprintf(out, n, "#%lld", (long long)v->as.imm); break;
        case VAL_REG:   snprintf(out, n, "-%d(%%r11)", 4 * ((int)v->as.reg + 1)); break;
        case VAL_LABEL: snprintf(out, n, "%s", v->as.label); break;
    }
}

static int slot(uint8_t vreg) { return -4 * ((int)vreg + 1); }

/* r0..r3 for the first 4 args. */
static const char *ARG_REG[4] = { "%r0", "%r1", "%r2", "%r3" };

static void emit_op(const Op *op, Emitter *e) {
    char buf[160], a[64], b[64];
    switch (op->kind) {

    case OP_MOV:
        if (op->as.mov.src.kind == VAL_IMM) {
            /* movw handles 16-bit immediates in one shot; for anything
             * bigger, movw+movt is the standard idiom.  For our toy
             * compiler we assume the immediate fits in 16 bits. */
            snprintf(buf, sizeof buf, "movw %%r4, #%lld",
                     (long long)op->as.mov.src.as.imm);
            emitter_line(e, buf);
            snprintf(buf, sizeof buf, "str %%r4, [%%r11, #%d]", slot(op->as.mov.dest));
            emitter_line(e, buf);
        } else {
            fmt_value(&op->as.mov.src, a, sizeof a);
            snprintf(buf, sizeof buf, "ldr %%r4, %s", a);            emitter_line(e, buf);
            snprintf(buf, sizeof buf, "str %%r4, [%%r11, #%d]", slot(op->as.mov.dest));
            emitter_line(e, buf);
        }
        break;

    case OP_BIN:
        if (op->as.bin.lhs.kind == VAL_REG) {
            snprintf(buf, sizeof buf, "ldr %%r4, [%%r11, #%d]",
                     slot(op->as.bin.lhs.as.reg));
            emitter_line(e, buf);
        } else if (op->as.bin.lhs.kind == VAL_IMM) {
            snprintf(buf, sizeof buf, "movw %%r4, #%lld",
                     (long long)op->as.bin.lhs.as.imm);
            emitter_line(e, buf);
        }

        if (op->as.bin.rhs.kind == VAL_REG) {
            snprintf(buf, sizeof buf, "ldr %%r5, [%%r11, #%d]",
                     slot(op->as.bin.rhs.as.reg));
            emitter_line(e, buf);
            strcpy(b, "%r5");
        } else {
            fmt_value(&op->as.bin.rhs, b, sizeof b);
        }

        {
            const char *mn =
                op->as.bin.op == BIN_ADD ? "add" :
                op->as.bin.op == BIN_SUB ? "sub" :
                op->as.bin.op == BIN_MUL ? "mul" : "sdiv";
            snprintf(buf, sizeof buf, "%s %%r4, %%r4, %s", mn, b);
            emitter_line(e, buf);
        }
        snprintf(buf, sizeof buf, "str %%r4, [%%r11, #%d]", slot(op->as.bin.dest));
        emitter_line(e, buf);
        break;

    case OP_CALL: {
        /* First four args in r0..r3. */
        size_t nregs = op->as.call.nargs < 4 ? op->as.call.nargs : 4;
        for (size_t i = 0; i < nregs; ++i) {
            const Value *v = &op->as.call.args[i];
            if (v->kind == VAL_REG) {
                snprintf(buf, sizeof buf, "ldr %s, [%%r11, #%d]",
                         ARG_REG[i], slot(v->as.reg));
                emitter_line(e, buf);
            } else if (v->kind == VAL_IMM) {
                snprintf(buf, sizeof buf, "movw %s, #%lld",
                         ARG_REG[i], (long long)v->as.imm);
                emitter_line(e, buf);
            }
        }

        /* Remaining args pushed right-to-left so that arg[4] ends up at
         * the lowest address (closest to sp) — this is what AAPCS says
         * the callee expects.  Keep sp 8-aligned across the call. */
        size_t stack_args = op->as.call.nargs > 4 ? op->as.call.nargs - 4 : 0;
        size_t pad = (stack_args % 2) ? 1 : 0;   /* 4-byte pushes, 8-byte alignment */

        if (pad) emitter_line(e, "sub sp, sp, #4");

        for (size_t i = op->as.call.nargs; i-- > 4; ) {
            const Value *v = &op->as.call.args[i];
            if (v->kind == VAL_REG) {
                snprintf(buf, sizeof buf, "ldr %%r4, [%%r11, #%d]", slot(v->as.reg));
                emitter_line(e, buf);
            } else if (v->kind == VAL_IMM) {
                snprintf(buf, sizeof buf, "movw %%r4, #%lld",
                         (long long)v->as.imm);
                emitter_line(e, buf);
            } else {
                fmt_value(v, a, sizeof a);
                snprintf(buf, sizeof buf, "ldr %%r4, %s", a);
                emitter_line(e, buf);
            }
            emitter_line(e, "push {%r4}");
        }

        snprintf(buf, sizeof buf, "bl %s", op->as.call.name);
        emitter_line(e, buf);

        /* Caller cleans stack args. */
        size_t cleanup = (stack_args + pad) * 4;
        if (cleanup) {
            snprintf(buf, sizeof buf, "add sp, sp, #%zu", cleanup);
            emitter_line(e, buf);
        }
        break;
    }

    case OP_RET:
        if (op->as.ret.has_value) {
            if (op->as.ret.value.kind == VAL_REG) {
                snprintf(buf, sizeof buf, "ldr %%r0, [%%r11, #%d]",
                         slot(op->as.ret.value.as.reg));
                emitter_line(e, buf);
            } else if (op->as.ret.value.kind == VAL_IMM) {
                snprintf(buf, sizeof buf, "movw %%r0, #%lld",
                         (long long)op->as.ret.value.as.imm);
                emitter_line(e, buf);
            }
        }
        break;

    case OP_JMP: {
        char *g = emitter_user_label(e, op->as.jmp.label);
        snprintf(buf, sizeof buf, "b %s", g);
        emitter_line(e, buf);
        free(g);
        break;
    }

    case OP_BREQ: {
        if (op->as.breq.lhs.kind == VAL_REG) {
            snprintf(buf, sizeof buf, "ldr %%r4, [%%r11, #%d]",
                     slot(op->as.breq.lhs.as.reg));
            emitter_line(e, buf);
        } else if (op->as.breq.lhs.kind == VAL_IMM) {
            snprintf(buf, sizeof buf, "movw %%r4, #%lld",
                     (long long)op->as.breq.lhs.as.imm);
            emitter_line(e, buf);
        }

        if (op->as.breq.rhs.kind == VAL_REG) {
            snprintf(buf, sizeof buf, "ldr %%r5, [%%r11, #%d]",
                     slot(op->as.breq.rhs.as.reg));
            emitter_line(e, buf);
            strcpy(b, "%r5");
        } else {
            fmt_value(&op->as.breq.rhs, b, sizeof b);
        }

        snprintf(buf, sizeof buf, "cmp %%r4, %s", b); emitter_line(e, buf);

        char *g = emitter_user_label(e, op->as.breq.label);
        snprintf(buf, sizeof buf, "beq %s", g); emitter_line(e, buf);
        free(g);
        break;
    }

    case OP_LABEL: {
        char *g = emitter_user_label(e, op->as.label.label);
        emitter_label(e, g);
        free(g);
        break;
    }
    }
}

static void arm_emit(Backend *self, const Program *p, Emitter *e) {
    (void)self;
    emitter_raw(e, ".syntax unified");
    emitter_raw(e, ".arch armv7-a");
    emitter_raw(e, ".text");

    for (size_t i = 0; i < p->nfunc; ++i) {
        const Function *f = &p->functions[i];
        char line[256];

        snprintf(line, sizeof line, ".globl %s", f->name); emitter_raw(e, line);
        snprintf(line, sizeof line, ".type %s, %%function", f->name); emitter_raw(e, line);
        emitter_label(e, f->name);

        /* AAPCS prologue: push {fp, lr}, set fp, allocate frame.
         * fp = r11, lr = r14 in the unified syntax. */
        emitter_line(e, "push {fp, lr}");
        emitter_line(e, "mov fp, sp");

        int frame = 4 * ((int)f->nbody + 2);
        frame = (frame + 7) & ~7;   /* 8-byte alignment per AAPCS */
        if (frame > 0) {
            snprintf(line, sizeof line, "sub sp, sp, #%d", frame);
            emitter_line(e, line);
        }

        for (size_t j = 0; j < f->nbody; ++j) emit_op(&f->body[j], e);

        /* Tear down and return.  Restoring pc from the saved lr is the
         * idiom: `pop {fp, pc}`. */
        emitter_line(e, "mov sp, fp");
        emitter_line(e, "pop {fp, pc}");

        snprintf(line, sizeof line, ".size %s, .-%s", f->name, f->name);
        emitter_raw(e, line);
    }

    for (size_t i = 0; i < p->nextern; ++i) {
        char line[256];
        snprintf(line, sizeof line, ".extern %s", p->externs[i]);
        emitter_raw(e, line);
    }

    /* %progbits on ARM. */
    emitter_raw(e, ".section .note.GNU-stack,\"\",%progbits");
}

static void arm_gas_flags(Backend *self, const char ***out, size_t *count) {
    (void)self;
    /* -march=armv7-a enables sdiv.  The `g` bit is a safe default on
     * armv7; for older cores drop `sdiv` and emit `bl __aeabi_idiv`. */
    static const char *flags[] = { "-march=armv7-a", "-mfloat-abi=hard" };
    *out = flags;
    *count = 2;
}

Backend *arm_backend_new(void) {
    Backend *b = calloc(1, sizeof(Backend));
    b->emit      = arm_emit;
    b->gas_flags = arm_gas_flags;
    b->self      = NULL;
    return b;
}
