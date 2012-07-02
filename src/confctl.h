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

#ifndef CONFCTL_H
#define	CONFCTL_H

#include <stdio.h>

/*
 * 'struct confctl' represents the whole configuration tree.
 */
struct confctl;

/*
 * 'struct confctl_var' represents a single node in the configuration tree.
 * Every node has a name, which can be empty, and either a value, or children.
 */
struct confctl_var;

struct confctl		*confctl_new(void);
void			confctl_set_equals_sign(struct confctl *cc, bool equals);
void			confctl_set_rewrite_in_place(struct confctl *cc, bool rewrite);
void			confctl_set_semicolon(struct confctl *cc, bool semicolon);
void			confctl_set_slash_slash_comments(struct confctl *cc, bool slash);
void			confctl_load(struct confctl *cc, const char *path);
void			confctl_save(struct confctl *cc, const char *path);
struct confctl_var	*confctl_root(struct confctl *cc);

const char		*confctl_var_name(struct confctl_var *cv);
const char		*confctl_var_value(struct confctl_var *cv);
void			confctl_var_set_value(struct confctl_var *cv, const char *value);
bool			confctl_var_is_container(const struct confctl_var *cv);
struct confctl_var	*confctl_var_first_child(struct confctl_var *parent);
struct confctl_var	*confctl_var_next(struct confctl_var *cv);
struct confctl_var	*confctl_var_new(struct confctl_var *parent, const char *name);
void			confctl_var_delete(struct confctl_var *cv);
void			confctl_var_move(struct confctl_var *cv, struct confctl_var *new_parent);

bool			confctl_var_delete_when_empty(struct confctl_var *cv);
void			*confctl_var_uptr(struct confctl_var *cv);
void			confctl_var_set_uptr(struct confctl_var *cv, void *uptr);

/*
 * XXX: Perhaps move to confctl.c; it looks specific to confctl(1).
 */
struct confctl		*confctl_from_line(const char *line);

#endif /* !CONFCTL_H */
