#ifndef EMITTER_H
#define EMITTER_H

#include <stddef.h>
#include <stdint.h>
#include "target.h"

typedef struct {
    char   *key;   /* user label  */
    char   *gas;   /* mangled name like .Luser_foo */
} LabelMapEntry;

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;

    uint64_t label_counter;
    LabelMapEntry *labels;
    size_t nlabels;
    size_t cap_labels;

    Target target;
} Emitter;

Emitter *emitter_new(Target t);

void emitter_line(Emitter *e, const char *s);
void emitter_raw(Emitter *e, const char *s);
void emitter_label(Emitter *e, const char *name);
void emitter_comment(Emitter *e, const char *s);
void emitter_blank(Emitter *e);

/* Returns a fresh owned string (caller frees). */
char *emitter_fresh_label(Emitter *e, const char *hint);
/* Mangles a user label; returns owned string. */
char *emitter_user_label(Emitter *e, const char *name);

/* Frees the emitter, returns the assembled text as a malloc'd NUL-terminated string. */
char *emitter_finish(Emitter *e);

#endif
