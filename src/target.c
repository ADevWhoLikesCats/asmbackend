
#include "../include/target.h"
#include <stddef.h>

Target target_x86(void) {
    Target t;
    t.arch      = ARCH_X86;
    t.triple    = "i686-unknown-linux-gnu";
    t.gas_march = NULL;
    return t;
}

Target target_x86_64(void) {
    Target t;
    t.arch      = ARCH_X86_64;
    t.triple    = "x86_64-unknown-linux-gnu";
    t.gas_march = NULL;
    return t;
}

Target target_arm(void) {
    Target t;
    t.arch      = ARCH_ARM;
    t.triple    = "armv7-unknown-linux-gnueabihf";
    t.gas_march = NULL;
    return t;
}

Target target_arm64(void) {
    Target t;
    t.arch      = ARCH_ARM64;
    t.triple    = "aarch64-unknown-linux-gnu";
    t.gas_march = NULL;
    return t;
}

Target target_riscv64(void) {
    Target t;
    t.arch      = ARCH_RISCV64;
    t.triple    = "riscv64-unknown-linux-gnu";
    t.gas_march = "rv64gc";
    return t;
}
