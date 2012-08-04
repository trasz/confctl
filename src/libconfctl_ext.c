/*-
 * Copyright (c) 2012 Edward Tomasz Napierala <trasz@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This file contains additional utility routines.  They are all
 * implemented using official libconfctl api, i.e. they are "on top"
 * of the stuff implemented in libconfctl.c.
 */

#include <ctype.h>
#include <err.h>
#include <stdbool.h>
#include <string.h>

#include "vis.h"
#include "confctl.h"

struct confctl *
confctl_from_line(const char *line)
{
	struct confctl *cc;
	struct confctl_var *cv, *parent;
	bool escaped = false, quoted = false, squoted = false;
	int i, j, len;
	char ch;
	char *copy, *name, *value;

	copy = name = strdup(line);
	if (copy == NULL)
		err(1, "strdup");

	cc = confctl_new();
	parent = confctl_root(cc);

	for (i = 0, j = 0;; i++, j++) {
		ch = copy[i];
		copy[j] = copy[i];
		if (ch == '\0') {
			len = strunvis(name, name);
			if (len < 0)
				err(1, "invalid escape sequence");
			cv = confctl_var_new(parent, name);
			return (cc);
		}
		if (escaped) {
			escaped = false;
			continue;
		}
		if (ch == '\\') {
			escaped = true;
			j--;
			continue;
		}
		if (!squoted && ch == '"')
			quoted = !quoted;
		if (!quoted && ch == '\'')
			squoted = !squoted;
		if (quoted || squoted)
			continue;
		if (isspace(ch))
			errx(1, "whitespace inside variable specification");
		if (ch == '.' || ch == '=') {
			copy[j] = '\0';
			len = strunvis(name, name);
			if (len < 0)
				err(1, "invalid escape sequence");
			cv = confctl_var_new(parent, name);
			if (ch == '.') {
				parent = cv;
				name = &(copy[j + 1]);
				continue;
			}
			i++;
			j++;
			value = &(copy[i]);
			len = strunvis(value, value);
			if (len < 0)
				err(1, "invalid escape sequence");
			confctl_var_set_value(cv, value);
			return (cc);
		}
	}
}
