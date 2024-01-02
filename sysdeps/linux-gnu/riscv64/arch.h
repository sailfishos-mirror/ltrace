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

#ifndef LTRACE_RISCV64_ARCH_H
#define LTRACE_RISCV64_ARCH_H

#include <elf.h>

#define ARCH_ENDIAN_LITTLE

/* NOTE:
    Name 'BREAKPOINT_LENGTH' is expected to exist and be constexpr
    (e.g. to initialize unsigned char orig_value[BREAKPOINT_LENGTH]),
    so it's kind of public, of semantic "max possible size of breakpoint".
*/
/* ebreak */
#define BREAKPOINT_VALUE { 0x73, 0x00, 0x10, 0x00 }
#define BREAKPOINT_LENGTH 4

/* c.ebreak */
#define BREAKPOINT_VALUE_COMPRESSED { 0x02, 0x90 }
#define BREAKPOINT_LENGTH_COMPRESSED 2

#define DECR_PC_AFTER_BREAK 0

#define LT_ELFCLASS    ELFCLASS64
#define LT_ELF_MACHINE    EM_RISCV

#define ARCH_HAVE_SW_SINGLESTEP

#define ARCH_HAVE_ADD_PLT_ENTRY

#define ARCH_HAVE_FETCH_ARG
#define ARCH_HAVE_FETCH_PACK

#define ARCH_HAVE_ENABLE_BREAKPOINT
#define ARCH_HAVE_DISABLE_BREAKPOINT

#define ARCH_HAVE_LTELF_DATA
struct arch_ltelf_data {
};

#endif
