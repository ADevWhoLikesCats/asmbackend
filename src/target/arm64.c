#include "../include/backend.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
 * AArch64 (ARM64), GAS syntax, AAPCS64.
 *
 *   - vreg N lives at -16*(N+1)(x29).
 *   - scratch: x9, x10.
 *   - args: x0..x7.
 *   - return: x0.
 *   - frame rounded to 16 bytes to keep sp aligned at calls.
 *   - x29 is the frame pointer, x30 the link register.
 */

static int slot(uint8_t vreg) { return -16 * ((int)vreg + 1); }



/* Load a value into a physical register.  Used for OP_BIN / OP_CALL /
 * OP_RET so we don't repeat the immediate/reg/label split everywhere. */
static void load_into(const Value *v, const char *reg, Emitter *e) {
    char buf[160];
    switch (v->kind) {
    case VAL_IMM:
        snprintf(buf, sizeof buf, "mov %s, #%lld", reg, (long long)v->as.imm);
        emitter_line(e, buf);
        break;
    case VAL_REG:
        snprintf(buf, sizeof buf, "ldr %s, [x29, #%d]", reg, slot(v->as.reg));
        emitter_line(e, buf);
        break;
    case VAL_LABEL:
        snprintf(buf, sizeof buf, "adrp %s, %s", reg, v->as.label);
        emitter_line(e, buf);
        snprintf(buf, sizeof buf, "add %s, %s, :lo12:%s", reg, reg, v->as.label);
        emitter_line(e, buf);
        break;
    }
}

static void emit_op(const Op *op, Emitter *e) {
    char buf[160];
    switch (op->kind) {

    case OP_MOV:
        load_into(&op->as.mov.src, "x9", e);
        snprintf(buf, sizeof buf, "str x9, [x29, #%d]", slot(op->as.mov.dest));
        emitter_line(e, buf);
        break;

    case OP_BIN:
        load_into(&op->as.bin.lhs, "x9", e);
        load_into(&op->as.bin.rhs, "x10", e);
        {
            const char *mn =
                op->as.bin.op == BIN_ADD ? "add" :
                op->as.bin.op == BIN_SUB ? "sub" :
                op->as.bin.op == BIN_MUL ? "mul" :
                                           "sdiv";
            snprintf(buf, sizeof buf, "%s x9, x9, x10", mn);
            emitter_line(e, buf);
        }
        snprintf(buf, sizeof buf, "str x9, [x29, #%d]", slot(op->as.bin.dest));
        emitter_line(e, buf);
        break;

    case OP_CALL: {
        size_t nregs = op->as.call.nargs < 8 ? op->as.call.nargs : 8;
        for (size_t i = 0; i < nregs; ++i) {
            char reg[8];
            snprintf(reg, sizeof reg, "x%zu", i);
            load_into(&op->as.call.args[i], reg, e);
        }
        if (op->as.call.nargs > 8) {
            fprintf(stderr,
                "warning: %s called with %zu args; only 8 in regs\n",
                op->as.call.name, op->as.call.nargs);
        }
        snprintf(buf, sizeof buf, "bl %s", op->as.call.name);
        emitter_line(e, buf);
        break;
    }

    case OP_RET:
        if (op->as.ret.has_value)
            load_into(&op->as.ret.value, "x0", e);
        break;

    case OP_JMP: {
        char *g = emitter_user_label(e, op->as.jmp.label);
        snprintf(buf, sizeof buf, "b %s", g);
        emitter_line(e, buf);
        free(g);
        break;
    }

    case OP_BREQ:
        load_into(&op->as.breq.lhs, "x9",  e);
        load_into(&op->as.breq.rhs, "x10", e);
        emitter_line(e, "cmp x9, x10");
        {
            char *g = emitter_user_label(e, op->as.breq.label);
            snprintf(buf, sizeof buf, "b.eq %s", g);
            emitter_line(e, buf);
            free(g);
        }
        break;

    case OP_LABEL: {
        char *g = emitter_user_label(e, op->as.label.label);
        emitter_label(e, g);
        free(g);
        break;
    }
    }
}

static void arm64_emit(Backend *self, const Program *p, Emitter *e) {
    (void)self;
    emitter_raw(e, ".text");

    for (size_t i = 0; i < p->nfunc; ++i) {
        const Function *f = &p->functions[i];
        char line[256];

        snprintf(line, sizeof line, ".globl %s", f->name);
        emitter_raw(e, line);
        snprintf(line, sizeof line, ".type %s, %%function", f->name);
        emitter_raw(e, line);
        emitter_label(e, f->name);

        emitter_line(e, "stp x29, x30, [sp, #-16]!");
        emitter_line(e, "mov x29, sp");

        int frame = 16 * ((int)f->nbody + 1);
        frame = (frame + 15) & ~15;
        if (frame > 0) {
            snprintf(line, sizeof line, "sub sp, sp, #%d", frame);
            emitter_line(e, line);
        }

        for (size_t j = 0; j < f->nbody; ++j)
            emit_op(&f->body[j], e);

        emitter_line(e, "mov sp, x29");
        emitter_line(e, "ldp x29, x30, [sp], #16");
        emitter_line(e, "ret");

        snprintf(line, sizeof line, ".size %s, .-%s", f->name, f->name);
        emitter_raw(e, line);
    }

    for (size_t i = 0; i < p->nextern; ++i) {
        char line[256];
        snprintf(line, sizeof line, ".extern %s", p->externs[i]);
        emitter_raw(e, line);
    }

    emitter_raw(e, ".section .note.GNU-stack,\"\",%progbits");
}

static void arm64_gas_flags(Backend *self, const char ***out, size_t *count) {
    (void)self;
    /* No extra flags needed; clang knows aarch64-...  If you want to
     * pin the ISA level, add e.g. "-march=armv8-a". */
    *out = NULL;
    *count = 0;
}

Backend *arm64_backend_new(void) {
    Backend *b = calloc(1, sizeof(Backend));
    if (!b) { perror("calloc"); exit(1); }
    b->emit      = arm64_emit;
    b->gas_flags = arm64_gas_flags;
    b->self      = NULL;
    return b;
}
