#include "../include/backend.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
 * x86_64, AT&T syntax, System V AMD64 ABI.
 *
 *   - vreg N is stored in a 16-byte stack slot at -16*(N+1)(%rbp).
 *   - scratch: %rax, %rdx for division (cltd + idivq).
 *   - args: %rdi, %rsi, %rdx, %rcx, %r8, %r9.
 *   - return: %rax.
 *   - frame rounded to 16 bytes to keep %rsp aligned at calls.
 *   - externals go through @PLT so PIE links work.
 */

static int slot(uint8_t vreg) { return -16 * ((int)vreg + 1); }

static void fmt_value(const Value *v, char *out, size_t n) {
    switch (v->kind) {
        case VAL_IMM:
            snprintf(out, n, "$%lld", (long long)v->as.imm);
            break;
        case VAL_REG:
            /* Register values live on the stack; emit a memory operand. */
            snprintf(out, n, "%d(%%rbp)", slot(v->as.reg));
            break;
        case VAL_LABEL:
            snprintf(out, n, "%s(%%rip)", v->as.label);
            break;
    }
}

static const char *ARG_REG[6] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };

static void emit_op(const Op *op, Emitter *e) {
    char buf[160], a[64], b[64];
    switch (op->kind) {

    case OP_MOV:
        fmt_value(&op->as.mov.src, a, sizeof a);
        snprintf(buf, sizeof buf, "movq %s, %d(%%rbp)", a, slot(op->as.mov.dest));
        emitter_line(e, buf);
        break;

    case OP_BIN:
        /* lhs -> %rax */
        fmt_value(&op->as.bin.lhs, a, sizeof a);
        snprintf(buf, sizeof buf, "movq %s, %%rax", a);
        emitter_line(e, buf);

        /* rhs either as an immediate, a memory operand, or already in
         * %rax if we need a register.  x86 allows at most one memory
         * operand per instruction, and the destination here is %rax,
         * so a memory rhs is fine. */
        fmt_value(&op->as.bin.rhs, b, sizeof b);

        switch (op->as.bin.op) {
        case BIN_ADD:
            snprintf(buf, sizeof buf, "addq %s, %%rax", b);
            emitter_line(e, buf);
            break;
        case BIN_SUB:
            snprintf(buf, sizeof buf, "subq %s, %%rax", b);
            emitter_line(e, buf);
            break;
        case BIN_MUL:
            snprintf(buf, sizeof buf, "imulq %s, %%rax", b);
            emitter_line(e, buf);
            break;
        case BIN_DIV:
            /* idivq operates on %rdx:%rax and can take a memory
             * operand.  cltd sign-extends %eax into %edx:%eax. */
            emitter_line(e, "cltd");
            snprintf(buf, sizeof buf, "idivq %s", b);
            emitter_line(e, buf);
            break;
        }

        snprintf(buf, sizeof buf, "movq %%rax, %d(%%rbp)", slot(op->as.bin.dest));
        emitter_line(e, buf);
        break;

    case OP_CALL:
        /* Args go in rdi, rsi, ... For args beyond 6 you'd push onto
         * the stack; we cap at 6 and warn. */
        for (size_t i = 0; i < op->as.call.nargs && i < 6; ++i) {
            fmt_value(&op->as.call.args[i], a, sizeof a);
            snprintf(buf, sizeof buf, "movq %s, %s", a, ARG_REG[i]);
            emitter_line(e, buf);
        }
        if (op->as.call.nargs > 6) {
            fprintf(stderr, "warning: %s called with %zu args; only 6 in regs\n",
                    op->as.call.name, op->as.call.nargs);
        }
        snprintf(buf, sizeof buf, "call %s@PLT", op->as.call.name);
        emitter_line(e, buf);
        break;

    case OP_RET:
        if (op->as.ret.has_value) {
            fmt_value(&op->as.ret.value, a, sizeof a);
            snprintf(buf, sizeof buf, "movq %s, %%rax", a);
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
        snprintf(buf, sizeof buf, "movq %s, %%rax", a);
        emitter_line(e, buf);
        snprintf(buf, sizeof buf, "cmpq %s, %%rax", b);
        emitter_line(e, buf);
        char *g = emitter_user_label(e, op->as.breq.label);
        snprintf(buf, sizeof buf, "je %s", g);
        emitter_line(e, buf);
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

static void x86_64_emit(Backend *self, const Program *p, Emitter *e) {
    (void)self;
    emitter_raw(e, ".text");

    for (size_t i = 0; i < p->nfunc; ++i) {
        const Function *f = &p->functions[i];
        char line[256];

        snprintf(line, sizeof line, ".globl %s", f->name);
        emitter_raw(e, line);
        snprintf(line, sizeof line, ".type %s, @function", f->name);
        emitter_raw(e, line);
        emitter_label(e, f->name);

        emitter_line(e, "pushq %rbp");
        emitter_line(e, "movq %rsp, %rbp");

        /* 16-byte aligned frame that covers slot(nbody-1). */
        int frame = 16 * ((int)f->nbody + 1);
        frame = (frame + 15) & ~15;
        if (frame > 0) {
            snprintf(line, sizeof line, "subq $%d, %%rsp", frame);
            emitter_line(e, line);
        }

        for (size_t j = 0; j < f->nbody; ++j)
            emit_op(&f->body[j], e);

        emitter_line(e, "movq %rbp, %rsp");
        emitter_line(e, "popq %rbp");
        emitter_line(e, "ret");

        snprintf(line, sizeof line, ".size %s, .-%s", f->name, f->name);
        emitter_raw(e, line);
    }

    for (size_t i = 0; i < p->nextern; ++i) {
        char line[256];
        snprintf(line, sizeof line, ".extern %s", p->externs[i]);
        emitter_raw(e, line);
    }

    emitter_raw(e, ".section .note.GNU-stack,\"\",@progbits");
}

static void x86_64_gas_flags(Backend *self, const char ***out, size_t *count) {
    (void)self;
    static const char *flags[] = { "--64" };
    *out = flags;
    *count = 1;
}

Backend *x86_64_backend_new(void) {
    Backend *b = calloc(1, sizeof(Backend));
    if (!b) { perror("calloc"); exit(1); }
    b->emit      = x86_64_emit;
    b->gas_flags = x86_64_gas_flags;
    b->self      = NULL;
    return b;
}
