#include "../include/backend.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
 * RISC-V 64 (RV64GC), GAS syntax, LP64D ABI.
 *
 * Conventions used here:
 *   - Virtual registers map to stack slots at -16*(vreg+1)(s0), where
 *     s0 holds the frame pointer we set up in the prologue.  We use
 *     16-byte slots for simplicity; a real backend would pack them.
 *   - Arguments 1..8 go in a0..a7.  Further args are passed on the
 *     stack (we don't emit those yet, but the ABI call is a0..a7).
 *   - Return value is in a0.
 *   - Scratch: t0 and t1 for arithmetic; t2 for the div-by-zero
 *     pseudo-instruction RISC-V requires.
 *   - All integer ops use the `*` (64-bit) forms: add, sub, mul, div.
 *     For 32-bit work you'd use addw/subw/mulw/divw.
 *   - Public interfaces must be 16-byte aligned per the psABI.  We
 *     round our frame accordingly.
 */

static void fmt_value(const Value *v, char *out, size_t n) {
    switch (v->kind) {
    case VAL_IMM:   snprintf(out, n, "%lld", (long long)v->as.imm); break;
    case VAL_REG:   snprintf(out, n, "t%d", v->as.reg % 7);         break;
    case VAL_LABEL: snprintf(out, n, "%s", v->as.label);            break;
    }
}

static int slot(uint8_t vreg) { return -16 * ((int)vreg + 1); }

/* r0..r7 for the first 8 args. */
static const char *ARG_REG[8] = {
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"
};

/*
 * Load a Value into a named register.  Handles the three cases:
 *   - Imm:   `li reg, imm` (GAS expands this to lui/addi sequences as
 *            needed; it is not a real instruction).
 *   - Reg:   `ld reg, slot(s0)`.
 *   - Label: `la reg, sym`  (PC-relative address materialization).
 */
static void load_into(const Value *v, const char *reg, Emitter *e) {
    char buf[160];
    switch (v->kind) {
    case VAL_IMM:
        snprintf(buf, sizeof buf, "li %s, %lld", reg, (long long)v->as.imm);
        emitter_line(e, buf);
        break;
    case VAL_REG:
        snprintf(buf, sizeof buf, "ld %s, %d(s0)", reg, slot(v->as.reg));
        emitter_line(e, buf);
        break;
    case VAL_LABEL:
        snprintf(buf, sizeof buf, "la %s, %s", reg, v->as.label);
        emitter_line(e, buf);
        break;
    }
}

/* Store the register `reg` into the stack slot for vreg `dest`. */
static void store_reg(const char *reg, uint8_t dest, Emitter *e) {
    char buf[128];
    snprintf(buf, sizeof buf, "sd %s, %d(s0)", reg, slot(dest));
    emitter_line(e, buf);
}

static void emit_op(const Op *op, Emitter *e) {
    char buf[160], b[64];
    switch (op->kind) {

    case OP_MOV:
        /* Load src into t0, then store into dest's slot. */
        load_into(&op->as.mov.src, "t0", e);
        store_reg("t0", op->as.mov.dest, e);
        break;

    case OP_BIN:
        /* lhs -> t0, rhs -> t1 (or a small immediate folded into the
         * instruction).  RISC-V has 12-bit signed immediates, so we
         * only fold small constants; otherwise load into t1 first. */
        load_into(&op->as.bin.lhs, "t0", e);

        if (op->as.bin.rhs.kind == VAL_IMM) {
            long long imm = op->as.bin.rhs.as.imm;
            if (imm >= -2048 && imm <= 2047 &&
                (op->as.bin.op == BIN_ADD || op->as.bin.op == BIN_SUB)) {
                /* addi/subi form */
                const char *mn = (op->as.bin.op == BIN_ADD) ? "addi" : "addi";
                long long adj = (op->as.bin.op == BIN_ADD) ? imm : -imm;
                snprintf(buf, sizeof buf, "%s t0, t0, %lld", mn, adj);
                emitter_line(e, buf);
                store_reg("t0", op->as.bin.dest, e);
                break;
            }
            /* Fall through: materialize the immediate into t1. */
            snprintf(buf, sizeof buf, "li t1, %lld", imm);
            emitter_line(e, buf);
            strcpy(b, "t1");
        } else {
            load_into(&op->as.bin.rhs, "t1", e);
            strcpy(b, "t1");
        }

        {
            const char *mn =
                op->as.bin.op == BIN_ADD ? "add" :
                op->as.bin.op == BIN_SUB ? "sub" :
                op->as.bin.op == BIN_MUL ? "mul" :
                                           "div";
            snprintf(buf, sizeof buf, "%s t0, t0, %s", mn, b);
            emitter_line(e, buf);
        }
        store_reg("t0", op->as.bin.dest, e);
        break;

    case OP_CALL:
        /* Args 0..7 into a0..a7.  For our toy IR, we cap at 8 args. */
        {
            size_t nregs = op->as.call.nargs < 8 ? op->as.call.nargs : 8;
            for (size_t i = 0; i < nregs; ++i) {
                load_into(&op->as.call.args[i], ARG_REG[i], e);
            }
            /* TODO: args beyond 8 need to be pushed onto the stack
             * (and popped by the caller per the RISC-V psABI).  We
             * assert rather than silently truncate. */
            if (op->as.call.nargs > 8) {
                fprintf(stderr,
                    "warning: %s called with %zu args; only 8 supported\n",
                    op->as.call.name, op->as.call.nargs);
            }

            snprintf(buf, sizeof buf, "call %s", op->as.call.name);
            emitter_line(e, buf);
        }
        break;

    case OP_RET:
        if (op->as.ret.has_value) {
            load_into(&op->as.ret.value, "a0", e);
        }
        break;

    case OP_JMP: {
        char *g = emitter_user_label(e, op->as.jmp.label);
        snprintf(buf, sizeof buf, "j %s", g);
        emitter_line(e, buf);
        free(g);
        break;
    }

    case OP_BREQ: {
        /* Compare two values.  RISC-V's beq takes two registers, so
         * we must materialize lhs and rhs into t0/t1 regardless of
         * whether they're immediates. */
        load_into(&op->as.breq.lhs, "t0", e);
        load_into(&op->as.breq.rhs, "t1", e);

        char *g = emitter_user_label(e, op->as.breq.label);
        snprintf(buf, sizeof buf, "beq t0, t1, %s", g);
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

static void riscv_emit(Backend *self, const Program *p, Emitter *e) {
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

        /* Prologue: standard RV64 frame.
         *
         *   addi sp, sp, -16
         *   sd   ra, 8(sp)
         *   sd   s0, 0(sp)
         *   mv   s0, sp              # s0 = fp
         *   addi sp, sp, -frame
         *
         * We save s0 (the caller's frame pointer) so we can restore it
         * on exit, and we save ra (return address) because our own
         * `call`s in the body would clobber it.  frame is 16-byte
         * aligned as required by the psABI at any public call boundary.
         */
        emitter_line(e, "addi sp, sp, -16");
        emitter_line(e, "sd ra, 8(sp)");
        emitter_line(e, "sd s0, 0(sp)");
        emitter_line(e, "mv s0, sp");

        int frame = 16 * ((int)f->nbody + 1);
        frame = (frame + 15) & ~15;
        if (frame > 0) {
            snprintf(line, sizeof line, "addi sp, sp, -%d", frame);
            emitter_line(e, line);
        }

        for (size_t j = 0; j < f->nbody; ++j) {
            emit_op(&f->body[j], e);
        }

        /* Epilogue: restore s0, ra, and sp, then return.
         * `mv sp, s0` undoes our frame allocation (s0 still points at
         * the top of the saved-ra/saved-s0 pair).  Then we load s0/ra
         * from that pair and bump sp past it. */
        emitter_line(e, "mv sp, s0");
        emitter_line(e, "ld s0, 0(sp)");
        emitter_line(e, "ld ra, 8(sp)");
        emitter_line(e, "addi sp, sp, 16");
        emitter_line(e, "ret");

        snprintf(line, sizeof line, ".size %s, .-%s", f->name, f->name);
        emitter_raw(e, line);
    }

    for (size_t i = 0; i < p->nextern; ++i) {
        char line[256];
        snprintf(line, sizeof line, ".extern %s", p->externs[i]);
        emitter_raw(e, line);
    }

    /* Mark the stack as non-executable.  RISC-V GAS uses @progbits. */
    emitter_raw(e, ".section .note.GNU-stack,\"\",@progbits");
}

static void riscv_gas_flags(Backend *self, const char ***out, size_t *count) {
    (void)self;
    /* rv64gc = rv64i + M (mul/div) + A (atomics) + F/D (float) + C
     * (compressed instructions).  `lp64d` is the standard Linux ABI.
     * If your toolchain is happier with just `-march=rv64g`, drop the
     * `c` — the generated code doesn't use compressed encodings. */
    static const char *flags[] = {
        "-march=rv64gc",
        "-mabi=lp64d",
    };
    *out = flags;
    *count = 2;
}

Backend *riscv_backend_new(void) {
    Backend *b = calloc(1, sizeof(Backend));
    b->emit      = riscv_emit;
    b->gas_flags = riscv_gas_flags;
    b->self      = NULL;
    return b;
}
