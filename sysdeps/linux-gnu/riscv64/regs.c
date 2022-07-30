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
#include "proc.h"
#include "ptrace.h"

/* read tracee general registers */
long
riscv64_read_gregs(struct process *proc, struct user_regs_struct *regs)
{
    struct iovec data = {regs, sizeof(*regs)};
    if (ptrace(PTRACE_GETREGSET, proc->pid, NT_PRSTATUS, &data) == -1) {
        perror("riscv64_read_gregs");
        return -1;
    }
    return 0;
}

void *
get_instruction_pointer(struct process *proc)
{
    struct user_regs_struct regs;
    /* RISC-V does not support PTRACE_PEEKUSER, PTRACE_GETREGS */
    if (riscv64_read_gregs(proc, &regs) == -1)
        return NULL;
    return (void *)regs.pc;
}

void
set_instruction_pointer(struct process *proc, void *addr)
{
    struct user_regs_struct regs;
    if (riscv64_read_gregs(proc, &regs) == -1)
        return;
    regs.pc = (unsigned long)addr;
    struct iovec data = {&regs, sizeof(regs)};
    ptrace(PTRACE_SETREGSET, proc->pid, NT_PRSTATUS, &data);
}

void *
get_stack_pointer(struct process *proc)
{
    struct user_regs_struct regs;
    if (riscv64_read_gregs(proc, &regs) == -1)
        return NULL;
    return (void *)regs.sp;
}

void *
get_return_addr(struct process *proc, void *stack_pointer)
{
    struct user_regs_struct regs;
    if (riscv64_read_gregs(proc, &regs) == -1)
        return NULL;
    return (void *)regs.ra;
}
