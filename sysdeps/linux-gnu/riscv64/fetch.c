/*
 * This file is part of ltrace.
 * Copyright (C) 2022 Kai Zhang (laokz)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <assert.h>
#include <errno.h>
#include "backend.h"
#include "fetch.h"
#include "type.h"
#include "proc.h"
#include "value.h"
#include "ptrace.h"
#include "param.h"

#define ARG_REG_FIRST   10
#define ARG_REG_LAST    17

/*
 * Where to fetch argument/return value.
 * According to Integer Calling Convention(ICC) and Hardware
 * Floating-point Calling Convention(FCC), at most 2×XLEN bits
 * -- 2 words be fetched for one value.
 */
enum fetch_class {
    CLASS_G,        /* only from one place, general register, */
    CLASS_F,        /*              float register, or stack, */
    CLASS_STACK,    /*              maybe 1 or 2 word(s)      */
    CLASS_G_F,      /* both from two places */
    CLASS_F_G,
    CLASS_G_STACK,
    CLASS_F_STACK,
    CLASS_STACK_F,
    CLASS_F_F,      /* specific for struct {float;float} */
};

struct fetch_context {
    struct user_regs_struct gregs;
    struct __riscv_d_ext_state fregs;

    int gidx;           /* next argument register index */
    int fidx;           /*              into above regs */
    arch_addr_t sp;     /* next argument stack address */
    int is_variadic;    /* if variadic argument */
    struct value retval;/* used when return value > 128bit */
};

static int
fetch_register_banks(struct process *proc, struct fetch_context *ctx)
{
    struct iovec data = {&ctx->gregs, sizeof(struct user_regs_struct)};
    if (ptrace(PTRACE_GETREGSET, proc->pid, NT_PRSTATUS, &data) == -1) {
        perror("PTRACE_GETREGSET NT_PRSTATUS");
        return -1;
    }

    data.iov_base = &ctx->fregs.f;
    data.iov_len = sizeof(struct __riscv_d_ext_state);
    if (ptrace(PTRACE_GETREGSET, proc->pid, NT_PRFPREG, &data) == -1) {
        perror("PTRACE_GETREGSET NT_PRFPREG");
        return -1;
    }

    return 0;
}

/*
 * Same as type_get_fp_equivalent but for INTEGER.
 * Note, pointer is not INTERGER.
 */
static struct arg_type_info *
type_get_int_equivalent(struct arg_type_info *info)
{
    while (info->type == ARGTYPE_STRUCT) {
        if (type_struct_size(info) != 1)
            return NULL;
        info = type_element(info, 0);
    }

    switch (info->type) {
    case ARGTYPE_CHAR:
    case ARGTYPE_SHORT:
    case ARGTYPE_INT:
    case ARGTYPE_LONG:
    case ARGTYPE_LLONG:
    case ARGTYPE_UINT:
    case ARGTYPE_ULONG:
    case ARGTYPE_ULLONG:
    case ARGTYPE_USHORT:
        return info;
    default:
        break;
    }

    return NULL;
}

static inline enum fetch_class
icc_class(struct fetch_context *ctx, size_t sz)
{
    if (ctx->gidx > ARG_REG_LAST)
        return CLASS_STACK;
    if ((ctx->gidx == ARG_REG_LAST) && (sz > 8))
        return CLASS_G_STACK;
    return CLASS_G;
}

/*
 * Determine fetch_class which typesize > 0 and <= 128bits.
 * Treat syscall exactly the same as function call, hope
 * not break the system??
 */
static enum fetch_class
get_fetch_class(struct fetch_context *ctx, struct process *proc,
                struct arg_type_info *info)
{
    size_t sz = type_sizeof(proc, info);

    /* variadic arguments are passed according to ICC */
    if (ctx->is_variadic) {
        if (sz <= 8)
            return (ctx->gidx > ARG_REG_LAST) ? CLASS_STACK : CLASS_G;

        /* 2×XLEN bits variadic must in an aligned register pair */
        if (ctx->gidx == ARG_REG_LAST) {
            ctx->gidx++;
            return CLASS_STACK;
        }
        if (ctx->gidx % 2)
            ctx->gidx++;
        return CLASS_G;
    }

    switch (info->type) {
	case ARGTYPE_INT:
	case ARGTYPE_UINT:
	case ARGTYPE_LONG:
	case ARGTYPE_ULONG:
    case ARGTYPE_LLONG:
    case ARGTYPE_ULLONG:
	case ARGTYPE_CHAR:
	case ARGTYPE_SHORT:
	case ARGTYPE_USHORT:
	case ARGTYPE_POINTER:
        if (ctx->gidx > ARG_REG_LAST)
            return CLASS_STACK;
        return CLASS_G;

	case ARGTYPE_FLOAT:
	case ARGTYPE_DOUBLE:
        if ((ctx->gidx > ARG_REG_LAST) && (ctx->fidx > ARG_REG_LAST))
            return CLASS_STACK;
        else if (ctx->fidx > ARG_REG_LAST)
            return CLASS_G;
        return CLASS_F;

    /* not support 'packed' 'aligned' attribute as ltrace doesn't */
	case ARGTYPE_STRUCT:
        /* try FCC first */
        if (type_struct_size(info) == 2) {
            struct arg_type_info *arg0 = type_struct_get(info, 0);
            struct arg_type_info *arg1 = type_struct_get(info, 1);

            /* {float;float} */
            if ((type_get_fp_equivalent(arg0) != NULL) &&
                (type_get_fp_equivalent(arg1) != NULL)) {
                if (ctx->fidx >= ARG_REG_LAST)
                    return icc_class(ctx, sz);
                /*
                 * A struct containing two floating-point reals is passed
                 * in two floating-point registers, though its total size
                 *  might be 8.
                 */
                return CLASS_F_F;
            }

            /* {float;int} */
            if ((type_get_fp_equivalent(arg0) != NULL) &&
                (type_get_int_equivalent(arg1) != NULL)) {
                if (ctx->fidx > ARG_REG_LAST)
                    return icc_class(ctx, sz);
                if (ctx->gidx > ARG_REG_LAST)
                    return CLASS_F_STACK;
                return CLASS_F_G;
            }

            /* {int;float} */
            if ((type_get_int_equivalent(arg0) != NULL) &&
                (type_get_fp_equivalent(arg1) != NULL)) {
                if (ctx->fidx > ARG_REG_LAST)
                    return icc_class(ctx, sz);
                if (ctx->gidx > ARG_REG_LAST)
                    return CLASS_STACK_F;
                return CLASS_G_F;
            }
        } else if (type_get_fp_equivalent(info) != NULL) { /* {float} */
            if (ctx->fidx > ARG_REG_LAST) {
                if (ctx->gidx > ARG_REG_LAST)
                    return CLASS_STACK;
                return CLASS_G;
            }
            return CLASS_F;
        }

        return icc_class(ctx, sz);

	default:
	    abort();
    }
}

static inline unsigned long
fetch_stack_word(struct fetch_context *ctx, struct process *proc)
{
    long v = ptrace(PTRACE_PEEKDATA, proc->pid, ctx->sp, 0);
    if ((v == -1) && errno) {
        perror("PTRACE_PEEKDATA");
        abort();
    }
    ctx->sp += 8;
    return (unsigned long)v;
}

/* Fetch value whose size no more than 128 bits */
static int
fetch_value(struct fetch_context *ctx, struct process *proc,
            struct value *valp, enum fetch_class c, size_t sz)
{
    unsigned long *p = (unsigned long *)value_reserve(valp, align(sz, 8));
    if (p == NULL) {
        fprintf(stderr, "value_reserve failed\n");
        return -1;
    }

    unsigned long *gr = &ctx->gregs.pc;

    switch (c) {
    case CLASS_G:
        p[0] = gr[ctx->gidx++];
        if (sz > 8)
            p[1] = gr[ctx->gidx++];
        break;

    case CLASS_F:
        p[0] = ctx->fregs.f[ctx->fidx++];
        if (sz > 8)
            p[1] = ctx->fregs.f[ctx->fidx++];
        break;

    case CLASS_STACK:
        p[0] = fetch_stack_word(ctx, proc);
        if (sz > 8)
            p[1] = fetch_stack_word(ctx, proc);
        break;

    case CLASS_G_F:
        p[0] = gr[ctx->gidx++];
        p[1] = ctx->fregs.f[ctx->fidx++];
        break;

    case CLASS_F_G:
        p[0] = ctx->fregs.f[ctx->fidx++];
        p[1] = gr[ctx->gidx++];
        break;

    case CLASS_F_F:
        p[0] = ctx->fregs.f[ctx->fidx++];
        unsigned long u = ctx->fregs.f[ctx->fidx++];
        if (sz > 8)
            p[1] = u;
        else /* struct{float;float;} use 2 fregs, occupy 1 word memory */
            p[0] = ((u & 0xFFFFFFFF) << 32) | (p[0] & 0xFFFFFFFF);
        break;

    case CLASS_G_STACK:
        p[0] = gr[ctx->gidx++];
        p[1] = fetch_stack_word(ctx, proc);
        break;

    case CLASS_F_STACK:
        p[0] = ctx->fregs.f[ctx->fidx++];
        p[1] = fetch_stack_word(ctx, proc);
        break;

    case CLASS_STACK_F:
        p[0] = fetch_stack_word(ctx, proc);
        p[1] = ctx->fregs.f[ctx->fidx++];
        break;
    }

    return 0;
}

/* value larger than 128bit is transferred to reference */
static int
fetch_larger(struct fetch_context *ctx, struct process *proc,
        struct arg_type_info *info, struct value *valp)
{
    value_init(valp, proc, NULL, info, 0);
    if (value_pass_by_reference(valp) != 0) {
        fprintf(stderr, "value_pass_by_reference failed\n");
        return -1;
    }

    enum fetch_class c = get_fetch_class(ctx, proc, valp->type);
    return fetch_value(ctx, proc, valp, c, 8);
}

struct fetch_context *
arch_fetch_arg_init(enum tof type, struct process *proc,
                    struct arg_type_info *ret_info)
{
    struct fetch_context *ctx = malloc(sizeof(*ctx));
    if (ctx == NULL) {
        perror("arch_fetch_arg_init");
        return NULL;
    }

    if (fetch_register_banks(proc, ctx) == -1)
        goto ERR_OUT;

    ctx->gidx = ARG_REG_FIRST;
    ctx->fidx = ARG_REG_FIRST;
    /*
     * When hit this function, the stack pointer is pointing to the
     * 1st argument, not the return address.
     */
    ctx->sp = (arch_addr_t)ctx->gregs.sp;
    ctx->is_variadic = 0;

    size_t sz = type_sizeof(proc, ret_info);
    assert(sz != (size_t)-1);
    if (sz > 16) {
        /*
         * Return value larger than 128bit will be passed by reference,
         * and address stored as an implicit first parameter.
         * We must fetch and save it first.
         */
        if (fetch_larger(ctx, proc, ret_info, &ctx->retval) == -1)
            goto ERR_OUT;
    } else {
        value_init_detached(&ctx->retval, NULL, NULL, 0);
    }

    return ctx;

ERR_OUT:
    free(ctx);
    return NULL;
}

struct fetch_context *
arch_fetch_arg_clone(struct process *proc, struct fetch_context *ctx)
{
    struct fetch_context *clone = malloc(sizeof(*ctx));
    if (clone == NULL) {
        perror("arch_fetch_arg_clone");
        return NULL;
    }

    *clone = *ctx;
    return clone;
}

int
arch_fetch_retval(struct fetch_context *ctx, enum tof type,
          struct process *proc, struct arg_type_info *info,
          struct value *valp)
{
    if (fetch_register_banks(proc, ctx) == -1)
        return -1;

    /* if we already prefetched its reference address */
    if (ctx->retval.type != NULL) {
        *valp = ctx->retval;
        return 0;
    }

    size_t sz = type_sizeof(proc, info);
    assert(sz != (size_t)-1);

    if (sz == 0)
        return 0;

    ctx->gidx = ARG_REG_FIRST;
    ctx->fidx = ARG_REG_FIRST;
    enum fetch_class c = get_fetch_class(ctx, proc, info);
    return fetch_value(ctx, proc, valp, c, sz);
}

int
arch_fetch_arg_next(struct fetch_context *ctx, enum tof type,
            struct process *proc, struct arg_type_info *info,
            struct value *valp)
{
    /* why we got ARGTYPE_ARRAY?? */
    assert(info->type != ARGTYPE_ARRAY);

    size_t sz = type_sizeof(proc, info);
    assert(sz != (size_t)-1);

    if (sz == 0)
        return 0;

    if (sz > 16)
        return fetch_larger(ctx, proc, info, valp);

    enum fetch_class c = get_fetch_class(ctx, proc, info);
    return fetch_value(ctx, proc, valp, c, sz);
}

void
arch_fetch_arg_done(struct fetch_context *ctx)
{
    free(ctx);
}

int
arch_fetch_param_pack_start(struct fetch_context *ctx,
                        enum param_pack_flavor ppflavor)
{
    /*
     * Leave out PARAM_PACK_ARGS and return garbage if any.
     *
     * For PARAM_PACK_VARARGS - variable arguments, once met
     * than all the left arguments are also variadic.
     */
    if (!ctx->is_variadic && (ppflavor == PARAM_PACK_VARARGS))
        ctx->is_variadic = 1;
    return 0;
}

void
arch_fetch_param_pack_end(struct fetch_context *ctx)
{
}
