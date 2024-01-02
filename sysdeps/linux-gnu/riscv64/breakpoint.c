/*
 * This file is part of ltrace.
 * Copyright (C) 2011 Petr Machata, Red Hat Inc.
 * Copyright (C) 2006 Ian Wienand
 * Copyright (C) 2002,2008,2009 Juan Cespedes
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

#include "config.h"

#include <sys/ptrace.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "common.h"
#include "backend.h"
#include "sysdep.h"
#include "breakpoint.h"
#include "proc.h"
#include "library.h"

#include "arch.h"

static bool insn_is_compressed(uint32_t insn) {
	return (insn & 0b11) != 0b11;
}

void
arch_enable_breakpoint(pid_t pid, struct breakpoint *sbp)
{
	static unsigned char break_insn_full[] = BREAKPOINT_VALUE;
	static unsigned char break_insn_compressed[] = BREAKPOINT_VALUE_COMPRESSED;

	static unsigned char* bp_insn;
	unsigned int bp_length;

	debug(DEBUG_PROCESS,
	      "arch_enable_breakpoint: pid=%d, addr=%p, symbol=%s",
	      pid, sbp->addr, breakpoint_name(sbp));

	long a = ptrace(PTRACE_PEEKTEXT, pid, sbp->addr, 0);
	if (a == -1 && errno) goto fail;

	memcpy((void*) sbp->orig_value, (void*) &a, sizeof(sbp->orig_value));

	bool orig_insn_is_compressed = insn_is_compressed(a);

	if (orig_insn_is_compressed) {
		bp_insn = break_insn_compressed;
		bp_length = BREAKPOINT_LENGTH_COMPRESSED;
	} else {
		bp_insn = break_insn_full;
		bp_length = BREAKPOINT_LENGTH;
	}

	assert (bp_length <= sizeof(long));

	// fill lower bytes of original value with (c.)ebreak insn.
	memcpy(&a, bp_insn, bp_length);

	a = ptrace(PTRACE_POKETEXT, pid, sbp->addr, a);
	if (a == -1 && errno) goto fail;

	return;

fail:
	fprintf(stderr, "enable_breakpoint"
		" pid=%d, addr=%p, symbol=%s: %s\n",
		pid, sbp->addr, breakpoint_name(sbp),
		strerror(errno));
}

void
arch_disable_breakpoint(pid_t pid, const struct breakpoint *sbp)
{
	debug(DEBUG_PROCESS,
	      "arch_disable_breakpoint: pid=%d, addr=%p, symbol=%s",
	      pid, sbp->addr, breakpoint_name(sbp));

	long a = ptrace(PTRACE_PEEKTEXT, pid, sbp->addr, 0);
	if (a == -1 && errno) goto fail;

	memcpy((void*) &a, (void*) sbp->orig_value, sizeof(sbp->orig_value));

	a = ptrace(PTRACE_POKETEXT, pid, sbp->addr, a);
	if (a == -1 && errno) goto fail;

	return;

fail:
	fprintf(stderr,
		"disable_breakpoint pid=%d, addr=%p: %s\n",
		pid, sbp->addr, strerror(errno));
	return;
}
