#ifndef IR_H
#define IR_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    VAL_IMM,
    VAL_REG,
    VAL_LABEL,
} ValueKind;

typedef struct {
    ValueKind kind;
    union {
        int64_t  imm;
        uint8_t  reg;      /* virtual register */
        char    *label;    /* owned */
    } as;
} Value;

typedef enum { BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV } BinOp;

typedef enum {
    OP_BIN,
    OP_MOV,
    OP_CALL,
    OP_RET,
    OP_JMP,
    OP_BREQ,
    OP_LABEL,
} OpKind;

typedef struct Op Op;

struct Op {
    OpKind kind;
    union {
        struct { uint8_t dest; BinOp op; Value lhs; Value rhs; } bin;
        struct { uint8_t dest; Value src; } mov;
        struct { char *name; Value *args; size_t nargs; } call;
        struct { int has_value; Value value; } ret;
        struct { char *label; } jmp;
        struct { Value lhs; Value rhs; char *label; } breq;
        struct { char *label; } label;
    } as;
};

typedef struct {
    char  *name;        /* owned */
    size_t params;
    Op    *body;        /* owned array */
    size_t nbody;
    size_t cap_body;
} Function;

typedef struct {
    Function *functions;
    size_t    nfunc;
    size_t    cap_func;

    char    **externs;   /* owned strings */
    size_t    nextern;
    size_t    cap_extern;
} Program;

/* Constructors */
Value val_imm(int64_t i);
Value val_reg(uint8_t r);
Value val_label(const char *s);   /* copies */

void value_free(Value *v);

Program *program_new(void);
void     program_free(Program *p);

Function *program_add_function(Program *p, const char *name);
void      function_add_op(Function *f, Op op);
void      program_add_extern(Program *p, const char *name);

/* Op builders — return by value, take ownership of heap strings passed in */
Op op_bin(uint8_t dest, BinOp op, Value lhs, Value rhs);
Op op_mov(uint8_t dest, Value src);
Op op_call(const char *name, const Value *args, size_t nargs);
Op op_ret(int has_value, Value v);
Op op_jmp(const char *label);
Op op_breq(Value lhs, Value rhs, const char *label);
Op op_label(const char *label);

#endif
