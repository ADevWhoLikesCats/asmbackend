/* include/target.h */
#ifndef TARGET_H
#define TARGET_H

typedef enum { ARCH_X86, ARCH_X86_64, ARCH_ARM, ARCH_ARM64, ARCH_RISCV64 } Arch;

typedef struct {
    Arch        arch;
    const char *triple;      /* e.g. "x86_64-unknown-linux-gnu" */
    const char *gas_march;   /* e.g. "rv64gc" or NULL */
} Target;

Target target_x86_64(void);
Target target_x86(void);
Target target_arm64(void);
Target target_arm(void);
Target target_riscv64(void);

#endif
