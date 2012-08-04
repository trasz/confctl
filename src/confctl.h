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
 * Note that names are not guaranteed to be unique: if you have a config that
 * looks like this: '1 { foo }; 2 { bar }; 1 { baz }', the root element will
 * have three children: '1', '2' and '1'.
 */
struct confctl_var;

struct confctl		*confctl_new(void);
void			confctl_delete(struct confctl *cc);

/*
 * Syntax options.  All of these default to false.
 */
void			confctl_set_equals_sign(struct confctl *cc, bool equals);
void			confctl_set_rewrite_in_place(struct confctl *cc, bool rewrite);
void			confctl_set_semicolon(struct confctl *cc, bool semicolon);
void			confctl_set_slash_slash_comments(struct confctl *cc, bool slash);
void			confctl_set_slash_star_comments(struct confctl *cc, bool star);

/*
 * Loading, writing and retrieving the root.
 */
void			confctl_load(struct confctl *cc, const char *path);
void			confctl_save(struct confctl *cc, const char *path);
struct confctl_var	*confctl_root(struct confctl *cc);

/*
 * Routines to manipulate individual nodes.
 */
const char		*confctl_var_name(struct confctl_var *cv);
void			confctl_var_set_name(struct confctl_var *cv, const char *name);
const char		*confctl_var_value(struct confctl_var *cv);
void			confctl_var_set_value(struct confctl_var *cv, const char *value);
bool			confctl_var_has_children(const struct confctl_var *cv);
bool			confctl_var_has_value(const struct confctl_var *cv);
struct confctl_var	*confctl_var_first_child(struct confctl_var *parent);
struct confctl_var	*confctl_var_next(struct confctl_var *cv);
struct confctl_var	*confctl_var_new(struct confctl_var *parent, const char *name);
void			confctl_var_delete(struct confctl_var *cv);
void			confctl_var_move(struct confctl_var *cv, struct confctl_var *new_parent);

/*
 * Say you have something like this: 'on whatever { some more stuff }'.  In this case,
 * parser will mark the 'on' node as implicit.  What this means is when you delete
 * the 'whatever' node, you'll also want to delete the 'on' one.
 */
bool			confctl_var_is_implicit_container(struct confctl_var *cv);

/*
 * User pointer can be set to whatever value.  Initially it's NULL.  The library
 * does not use this value in any way.
 */
void			*confctl_var_uptr(struct confctl_var *cv);
void			confctl_var_set_uptr(struct confctl_var *cv, void *uptr);

/*
 * Additional utility routines.
 */
struct confctl		*confctl_from_line(const char *line);

#endif /* !CONFCTL_H */
