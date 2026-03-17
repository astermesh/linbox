#ifndef LINBOX_SHIM_RESOLVE_H
#define LINBOX_SHIM_RESOLVE_H

#include <dlfcn.h>
#include <errno.h>

#include "linbox.h"

#define LINBOX_RESOLVE_NEXT(fn_ptr, symbol, type)                                                  \
    do {                                                                                           \
        if ((fn_ptr) == NULL) {                                                                    \
            linbox_state_t *_st = linbox_state();                                                  \
            if (_st->resolving) {                                                                  \
                errno = EAGAIN;                                                                    \
                return -1;                                                                         \
            }                                                                                      \
            _st->resolving = true;                                                                 \
            (fn_ptr) = (type)dlsym(RTLD_NEXT, (symbol));                                           \
            _st->resolving = false;                                                                \
        }                                                                                          \
    } while (0)

#endif