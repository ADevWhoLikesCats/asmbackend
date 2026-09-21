#include "../include/backend.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * 32-bit x86, AT&T syntax, cdecl calling convention.
 *
 * Conventions used here:
 *   - Virtual registers map to stack slots at -4*(vreg+1)(%ebp).
 *   - The scratch physical register we use for intermediate arithmetic
 *     is %eax.  %ecx/%edx are reserved for division.
 *   - Arguments are pushed right-to-left onto the stack.
 *   - Caller cleans the stack after the call (cdecl).
 *   - External calls go through @PLT so PIE links work.
 */

static void fmt_value(const Value *v, char *out, size_t n) {
    switch (v->kind) {
        case VAL_IMM:   snprintf(out, n, "$%lld", (long long)v->as.imm); break;
        case VAL_REG:   snprintf(out, n, "-%d(%%ebp)", 4 * ((int)v->as.reg + 1)); break;
        case VAL_LABEL: snprintf(out, n, "%s", v->as.label); break;
    }
}

static int slot(uint8_t vreg) { return -4 * ((int)vreg + 1); }

/* Emit "push <operand>" for a Value, using %eax as a scratch for imms. */
static void push_value(const Value *v, Emitter *e) {
    char buf[128], a[64];
    switch (v->kind) {
    case VAL_IMM:
        /* `pushl $imm` is fine for 32-bit immediates. */
        snprintf(buf, sizeof buf, "pushl $%lld", (long long)v->as.imm);
        emitter_line(e, buf);
        break;
    case VAL_REG:
        snprintf(buf, sizeof buf, "pushl -%d(%%ebp)", 4 * ((int)v->as.reg + 1));
        emitter_line(e, buf);
        break;
    case VAL_LABEL:
        fmt_value(v, a, sizeof a);
        snprintf(buf, sizeof buf, "pushl %s", a);
        emitter_line(e, buf);
        break;
    }
}

static void emit_op(const Op *op, Emitter *e) {
    char buf[160], a[64], b[64];
    switch (op->kind) {

    case OP_MOV:
        fmt_value(&op->as.mov.src, a, sizeof a);
        snprintf(buf, sizeof buf, "movl %s, %d(%%ebp)", a, slot(op->as.mov.dest));
        emitter_line(e, buf);
        break;

    case OP_BIN:
        fmt_value(&op->as.bin.lhs, a, sizeof a);
        fmt_value(&op->as.bin.rhs, b, sizeof b);
        snprintf(buf, sizeof buf, "movl %s, %%eax", a);
        emitter_line(e, buf);
        switch (op->as.bin.op) {
        case BIN_ADD:
            snprintf(buf, sizeof buf, "addl %s, %%eax", b); emitter_line(e, buf);
            break;
        case BIN_SUB:
            snprintf(buf, sizeof buf, "subl %s, %%eax", b); emitter_line(e, buf);
            break;
        case BIN_MUL:
            snprintf(buf, sizeof buf, "imull %s, %%eax", b); emitter_line(e, buf);
            break;
        case BIN_DIV:
            /* Sign-extend eax into edx:eax, then divide. */
            emitter_line(e, "cltd"); /* synonym for cdq; %edx = sign(%eax) */
            snprintf(buf, sizeof buf, "idivl %s", b); emitter_line(e, buf);
            break;
        }
        snprintf(buf, sizeof buf, "movl %%eax, %d(%%ebp)", slot(op->as.bin.dest));
        emitter_line(e, buf);
        break;

    case OP_CALL: {
        /* cdecl: push args right-to-left. */
        for (size_t i = op->as.call.nargs; i-- > 0; ) {
            push_value(&op->as.call.args[i], e);
        }
        /* Keep the stack 16-byte aligned across the call.  Before the
         * pushes, %esp was 16-aligned (because our prologue rounds the
         * frame).  Each push subtracts 4.  Pad with one extra 4-byte
         * slot if the arg count is odd. */
        int pad = (op->as.call.nargs % 2) ? 1 : 0;
        if (pad) emitter_line(e, "subl $4, %esp");

        snprintf(buf, sizeof buf, "call %s@PLT", op->as.call.name);
        emitter_line(e, buf);

        /* Caller cleans: pop pad + args. */
        int cleanup = (int)(op->as.call.nargs + pad) * 4;
        if (cleanup > 0) {
            snprintf(buf, sizeof buf, "addl $%d, %%esp", cleanup);
            emitter_line(e, buf);
        }
        break;
    }

    case OP_RET:
        if (op->as.ret.has_value) {
            fmt_value(&op->as.ret.value, a, sizeof a);
            snprintf(buf, sizeof buf, "movl %s, %%eax", a);
            emitter_line(e, buf);
        }
        break;

    case OP_JMP: {
        char *g = emitter_user_label(e, op->as.jmp.label);
        snprintf(buf, sizeof buf, "jmp %s", g);
        emitter_line(e, buf);
        free(g);
        break;
    }

    case OP_BREQ: {
        fmt_value(&op->as.breq.lhs, a, sizeof a);
        fmt_value(&op->as.breq.rhs, b, sizeof b);
        snprintf(buf, sizeof buf, "movl %s, %%eax", a); emitter_line(e, buf);
        snprintf(buf, sizeof buf, "cmpl %s, %%eax", b); emitter_line(e, buf);
        char *g = emitter_user_label(e, op->as.breq.label);
        snprintf(buf, sizeof buf, "je %s", g); emitter_line(e, buf);
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

static void x86_emit(Backend *self, const Program *p, Emitter *e) {
    (void)self;
    emitter_raw(e, ".text");
    for (size_t i = 0; i < p->nfunc; ++i) {
        const Function *f = &p->functions[i];
        char line[256];

        snprintf(line, sizeof line, ".globl %s", f->name); emitter_raw(e, line);
        snprintf(line, sizeof line, ".type %s, @function", f->name); emitter_raw(e, line);
        emitter_label(e, f->name);

        emitter_line(e, "pushl %ebp");
        emitter_line(e, "movl %esp, %ebp");

        /* Frame: 4*(nbody+1) rounded up to a multiple of 16.
         *   +1 accounts for slot(vreg) starting at -4. */
        int frame = 4 * ((int)f->nbody + 1);
        frame = (frame + 15) & ~15;
        if (frame > 0) {
            snprintf(line, sizeof line, "subl $%d, %%esp", frame);
            emitter_line(e, line);
        }

        for (size_t j = 0; j < f->nbody; ++j) emit_op(&f->body[j], e);

        /* Restore & return.  movl %ebp,%esp undoes our frame; popl %ebp
         * restores the caller's frame.  ret pops the return address. */
        emitter_line(e, "movl %ebp, %esp");
        emitter_line(e, "popl %ebp");
        emitter_line(e, "ret");

        snprintf(line, sizeof line, ".size %s, .-%s", f->name, f->name);
        emitter_raw(e, line);
    }

    for (size_t i = 0; i < p->nextern; ++i) {
        char line[256];
        snprintf(line, sizeof line, ".extern %s", p->externs[i]);
        emitter_raw(e, line);
    }

    /* Mark the stack as non-executable.  @progbits on x86. */
    emitter_raw(e, ".section .note.GNU-stack,\"\",@progbits");
}

static void x86_gas_flags(Backend *self, const char ***out, size_t *count) {
    (void)self;
    static const char *flags[] = { "--32" };
    *out = flags;
    *count = 1;
}

Backend *x86_backend_new(void) {
    Backend *b = calloc(1, sizeof(Backend));
    b->emit      = x86_emit;
    b->gas_flags = x86_gas_flags;
    b->self      = NULL;
    return b;
}
