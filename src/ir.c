#include "ir.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ *
 * Small helpers
 * ------------------------------------------------------------------ */

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (!d) { perror("malloc"); exit(1); }
    memcpy(d, s, n);
    return d;
}

void value_free(Value *v) {
    if (!v) return;
    if (v->kind == VAL_LABEL && v->as.label) {
        free(v->as.label);
        v->as.label = NULL;
    }
}

/* ------------------------------------------------------------------ *
 * Value constructors
 * ------------------------------------------------------------------ */

Value val_imm(int64_t i) {
    Value v;
    memset(&v, 0, sizeof v);
    v.kind   = VAL_IMM;
    v.as.imm = i;
    return v;
}

Value val_reg(uint8_t r) {
    Value v;
    memset(&v, 0, sizeof v);
    v.kind   = VAL_REG;
    v.as.reg = r;
    return v;
}

Value val_label(const char *s) {
    Value v;
    memset(&v, 0, sizeof v);
    v.kind     = VAL_LABEL;
    v.as.label = xstrdup(s);
    return v;
}

/* ------------------------------------------------------------------ *
 * Op constructors
 * ------------------------------------------------------------------ */

Op op_bin(uint8_t dest, BinOp op, Value lhs, Value rhs) {
    Op o;
    memset(&o, 0, sizeof o);
    o.kind        = OP_BIN;
    o.as.bin.dest = dest;
    o.as.bin.op   = op;
    o.as.bin.lhs  = lhs;
    o.as.bin.rhs  = rhs;
    return o;
}

Op op_mov(uint8_t dest, Value src) {
    Op o;
    memset(&o, 0, sizeof o);
    o.kind        = OP_MOV;
    o.as.mov.dest = dest;
    o.as.mov.src  = src;
    return o;
}

Op op_call(const char *name, const Value *args, size_t nargs) {
    Op o;
    memset(&o, 0, sizeof o);
    o.kind          = OP_CALL;
    o.as.call.name  = xstrdup(name);
    o.as.call.nargs = nargs;
    o.as.call.args  = NULL;
    if (nargs) {
        o.as.call.args = malloc(nargs * sizeof(Value));
        if (!o.as.call.args) { perror("malloc"); exit(1); }
        for (size_t i = 0; i < nargs; ++i)
            o.as.call.args[i] = args[i];
    }
    return o;
}

Op op_ret(int has_value, Value v) {
    Op o;
    memset(&o, 0, sizeof o);
    o.kind             = OP_RET;
    o.as.ret.has_value = has_value;
    o.as.ret.value     = v;
    return o;
}

Op op_jmp(const char *label) {
    Op o;
    memset(&o, 0, sizeof o);
    o.kind         = OP_JMP;
    o.as.jmp.label = xstrdup(label);
    return o;
}

Op op_breq(Value lhs, Value rhs, const char *label) {
    Op o;
    memset(&o, 0, sizeof o);
    o.kind          = OP_BREQ;
    o.as.breq.lhs   = lhs;
    o.as.breq.rhs   = rhs;
    o.as.breq.label = xstrdup(label);
    return o;
}

Op op_label(const char *label) {
    Op o;
    memset(&o, 0, sizeof o);
    o.kind           = OP_LABEL;
    o.as.label.label = xstrdup(label);
    return o;
}

/* ------------------------------------------------------------------ *
 * Program / Function builders
 * ------------------------------------------------------------------ */

Program *program_new(void) {
    Program *p = calloc(1, sizeof(Program));
    if (!p) { perror("calloc"); exit(1); }
    return p;
}

Function *program_add_function(Program *p, const char *name) {
    if (p->nfunc == p->cap_func) {
        size_t ncap = p->cap_func ? p->cap_func * 2 : 8;
        Function *nf = realloc(p->functions, ncap * sizeof(Function));
        if (!nf) { perror("realloc"); exit(1); }
        p->functions = nf;
        p->cap_func  = ncap;
    }
    Function *f = &p->functions[p->nfunc++];
    memset(f, 0, sizeof *f);
    f->name = xstrdup(name);
    return f;
}

void function_add_op(Function *f, Op op) {
    if (f->nbody == f->cap_body) {
        size_t ncap = f->cap_body ? f->cap_body * 2 : 16;
        Op *nb = realloc(f->body, ncap * sizeof(Op));
        if (!nb) { perror("realloc"); exit(1); }
        f->body     = nb;
        f->cap_body = ncap;
    }
    f->body[f->nbody++] = op;
}

void program_add_extern(Program *p, const char *name) {
    if (p->nextern == p->cap_extern) {
        size_t ncap = p->cap_extern ? p->cap_extern * 2 : 8;
        char **ne = realloc(p->externs, ncap * sizeof(char *));
        if (!ne) { perror("realloc"); exit(1); }
        p->externs    = ne;
        p->cap_extern = ncap;
    }
    p->externs[p->nextern++] = xstrdup(name);
}

/* ------------------------------------------------------------------ *
 * Teardown
 * ------------------------------------------------------------------ */

static void op_free(Op *op) {
    switch (op->kind) {
    case OP_BIN:
        value_free(&op->as.bin.lhs);
        value_free(&op->as.bin.rhs);
        break;
    case OP_MOV:
        value_free(&op->as.mov.src);
        break;
    case OP_CALL:
        free(op->as.call.name);
        for (size_t k = 0; k < op->as.call.nargs; ++k)
            value_free(&op->as.call.args[k]);
        free(op->as.call.args);
        break;
    case OP_RET:
        if (op->as.ret.has_value)
            value_free(&op->as.ret.value);
        break;
    case OP_JMP:
        free(op->as.jmp.label);
        break;
    case OP_BREQ:
        value_free(&op->as.breq.lhs);
        value_free(&op->as.breq.rhs);
        free(op->as.breq.label);
        break;
    case OP_LABEL:
        free(op->as.label.label);
        break;
    }
}

void program_free(Program *p) {
    if (!p) return;

    for (size_t i = 0; i < p->nfunc; ++i) {
        Function *f = &p->functions[i];
        free(f->name);
        for (size_t j = 0; j < f->nbody; ++j)
            op_free(&f->body[j]);
        free(f->body);
    }
    free(p->functions);

    for (size_t i = 0; i < p->nextern; ++i)
        free(p->externs[i]);
    free(p->externs);

    free(p);
}
