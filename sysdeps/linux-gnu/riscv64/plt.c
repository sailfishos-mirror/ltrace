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

#include <sys/uio.h>
#include <gelf.h>
#include "ltrace-elf.h"
#include "proc.h"
#include "backend.h"
#include "breakpoint.h"
#include "ptrace.h"
#include "library.h"
#include "trace.h"

GElf_Addr
arch_plt_sym_val(struct ltelf *lte, size_t ndx, GElf_Rela *rela)
{
    if (GELF_R_TYPE(rela->r_info) == R_RISCV_IRELATIVE)
        return rela->r_addend;// what shall we return ??

    return lte->plt_addr + 16 * 2 + (ndx * 16);
}

void *
sym2addr(struct process *proc, struct library_symbol *sym)
{
        return sym->enter_addr;
}

enum plt_status
arch_elf_add_plt_entry(struct process *proc, struct ltelf *lte,
                       const char *name, GElf_Rela *rela,
                       size_t i, struct library_symbol **ret)
{
    if (GELF_R_TYPE(rela->r_info) == R_RISCV_IRELATIVE)
        return linux_elf_add_plt_entry_irelative(proc, lte, rela, i, ret);

    return PLT_DEFAULT;
}

int
arch_elf_init(struct ltelf *lte, struct library *lib)
{
    if ((lte->ehdr.e_flags & EF_RISCV_FLOAT_ABI) !=
            EF_RISCV_FLOAT_ABI_DOUBLE) {
        fprintf(stderr, "failed: only LP64D ABI supported\n");
        return -1;
    }

    return 0;
}

void
arch_elf_destroy(struct ltelf *lte)
{
}
