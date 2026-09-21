#include "emitter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static void buf_append(Emitter *e, const char *s) {
    size_t n = strlen(s);
    if (e->len + n + 1 > e->cap) {
        while (e->len + n + 1 > e->cap) e->cap = e->cap ? e->cap * 2 : 256;
        e->buf = realloc(e->buf, e->cap);
    }
    memcpy(e->buf + e->len, s, n);
    e->len += n;
    e->buf[e->len] = '\0';
}

Emitter *emitter_new(Target t) {
    Emitter *e = calloc(1, sizeof(Emitter));
    e->target = t;
    buf_append(e, "");   /* allocate */
    return e;
}

void emitter_raw(Emitter *e, const char *s)     { buf_append(e, s); buf_append(e, "\n"); }
void emitter_line(Emitter *e, const char *s)    { buf_append(e, "    "); buf_append(e, s); buf_append(e, "\n"); }
void emitter_blank(Emitter *e)                  { buf_append(e, "\n"); }

void emitter_label(Emitter *e, const char *name) {
    buf_append(e, name);
    buf_append(e, ":\n");
}

void emitter_comment(Emitter *e, const char *s) {
    const char *c = (e->target.arch == ARCH_ARM) ? "@" : "#";
    buf_append(e, "    ");
    buf_append(e, c);
    buf_append(e, " ");
    buf_append(e, s);
    buf_append(e, "\n");
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    memcpy(d, s, n);
    return d;
}

char *emitter_fresh_label(Emitter *e, const char *hint) {
    e->label_counter++;
    /* ".L<hint>_<n>" */
    char tmp[128];
    snprintf(tmp, sizeof(tmp), ".L%s_%llu", hint, (unsigned long long)e->label_counter);
    return xstrdup(tmp);
}

char *emitter_user_label(Emitter *e, const char *name) {
    /* linear scan; small N so fine */
    for (size_t i = 0; i < e->nlabels; ++i)
        if (strcmp(e->labels[i].key, name) == 0)
            return xstrdup(e->labels[i].gas);

    if (e->nlabels == e->cap_labels) {
        e->cap_labels = e->cap_labels ? e->cap_labels * 2 : 16;
        e->labels = realloc(e->labels, e->cap_labels * sizeof(LabelMapEntry));
    }
    char tmp[256];
    snprintf(tmp, sizeof(tmp), ".Luser_%s", name);
    e->labels[e->nlabels].key = xstrdup(name);
    e->labels[e->nlabels].gas = xstrdup(tmp);
    char *out = xstrdup(tmp);
    e->nlabels++;
    return out;
}

char *emitter_finish(Emitter *e) {
    char *out = e->buf;   /* transfer ownership */
    for (size_t i = 0; i < e->nlabels; ++i) {
        free(e->labels[i].key);
        free(e->labels[i].gas);
    }
    free(e->labels);
    free(e);
    return out;
}
