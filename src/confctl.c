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

#include <assert.h>
#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vis.h"

#include "confctl.h"

static void
usage(void)
{

	fprintf(stderr, "usage: confctl [-CEISn] config-path [name...]\n");
	fprintf(stderr, "       confctl [-CEISn] -a config-path\n");
	fprintf(stderr, "       confctl [-CEIS] -w name=value config-path\n");
	fprintf(stderr, "       confctl [-CEIS] -x name config-path\n");
	exit(1);
}

/*
 * This is used for two purposes - first, when selecting variables to display
 * (e.g. 'confctl path some.variable some.other.variable'), we mark nodes
 * that should be hidden instead of removing them; this is just a performance
 * optimisation.  Second, when merging, we mark nodes that were already merged.
 */
static bool
cv_marked(struct confctl_var *cv)
{

	if (confctl_var_uptr(cv) != NULL)
		return (true);
	return (false);
}

static void
cv_mark(struct confctl_var *cv, bool v)
{

	if (v)
		confctl_var_set_uptr(cv, (void *)1);
	else
		confctl_var_set_uptr(cv, NULL);
}

static void
cv_merge_existing(struct confctl_var *cv, struct confctl_var *newcv)
{
	struct confctl_var *child, *newchild, *next, *newnext;

	if (strcmp(confctl_var_name(cv), confctl_var_name(newcv)) != 0)
		return;

	if (confctl_var_has_value(newcv)) {
		if (confctl_var_has_children(cv))
			errx(1, "cannot replace container node with leaf node");
		confctl_var_set_value(cv, confctl_var_value(newcv));
		/*
		 * Mark the node as done, so that we won't try
		 * to add it in cv_merge_new().
		 */
		cv_mark(newcv, true);
		return;
	}

	/*
	 * This code implements this:
	 * TAILQ_FOREACH_SAFE(newchild, &newcv->cv_children, cv_next, newtmp) {
	 * 	TAILQ_FOREACH_SAFE(child, &cv->cv_children, cv_next, tmp)
	 * 		cv_merge_existing(child, newchild);
	 * }
	 */
	newchild = confctl_var_first_child(newcv);
	while (newchild != NULL) {
		newnext = confctl_var_next(newchild);

		child = confctl_var_first_child(cv);
		while (child != NULL) {
			next = confctl_var_next(child);
			cv_merge_existing(child, newchild);
			child = next;
		}

		newchild = newnext;
	}
}

static bool
cv_merge_new(struct confctl_var *cv, struct confctl_var *newcv)
{
	struct confctl_var *child, *newchild, *next, *newnext;
	bool found;

	if (cv_marked(newcv))
		return (true);

	if (strcmp(confctl_var_name(cv), confctl_var_name(newcv)) != 0)
		return (false);

	/*
	 * This code implements this:
	 * TAILQ_FOREACH_SAFE(newchild, &newcv->cv_children, cv_next, newtmp) {
	 * 	found = false;
	 * 	TAILQ_FOREACH_SAFE(child, &cv->cv_children, cv_next, tmp) {
	 * 		found = cv_merge_new(child, newchild);
	 * 		if (found)
	 * 			break;
	 * 	}
	 * 	if (!found)
	 * 		confctl_var_move(newchild, cv);
	 * }
	 */
	newchild = confctl_var_first_child(newcv);
	while (newchild != NULL) {
		newnext = confctl_var_next(newchild);

		found = false;
		child = confctl_var_first_child(cv);
		while (child != NULL) {
			next = confctl_var_next(child);

			found = cv_merge_new(child, newchild);
			if (found)
				break;

			child = next;
		}
		if (!found)
			confctl_var_move(newchild, cv);

		newchild = newnext;
	}

	return (true);
}

static void
cc_merge(struct confctl **cc, struct confctl *merge)
{
	struct confctl_var *root, *mergeroot;

	if (*cc == NULL)
		*cc = confctl_new();

	root = confctl_root(*cc);
	mergeroot = confctl_root(merge);

	/*
	 * Reason for doing it in two steps is that we need
	 * to correctly handle duplicate nodes, such as this:
	 * "1 { foo } 2 { bar } 1 { baz }".  In this case,
	 * when merging '1.baz', we want to update the existing
	 * node, not add a new sibling to "foo".
	 */
	cv_merge_existing(root, mergeroot);
	cv_merge_new(root, mergeroot);
}

static void
cv_remove(struct confctl_var *cv, struct confctl_var *remove)
{
	struct confctl_var *child, *removechild, *next;

	if (confctl_var_value(remove) != NULL)
		errx(1, "variable to remove must not specify a value");

	if (strcmp(confctl_var_name(remove), confctl_var_name(cv)) != 0)
		return;

	if (confctl_var_first_child(remove) == NULL) {
		confctl_var_delete(cv);
		return;
	}

	child = confctl_var_first_child(cv);
	while (child != NULL) {
		next = confctl_var_next(child);

		for (removechild = confctl_var_first_child(remove); removechild != NULL; removechild = confctl_var_next(removechild))
			cv_remove(child, removechild);

		child = next;
	}

	if (confctl_var_is_implicit_container(cv) && confctl_var_first_child(cv) == NULL)
		confctl_var_delete(cv);
}

static void
cc_remove(struct confctl *cc, struct confctl *remove)
{

	cv_remove(confctl_root(cc), confctl_root(remove));
}

static bool
cv_filter(struct confctl_var *cv, struct confctl_var *filter)
{
	struct confctl_var *child, *filterchild;
	bool found;

	if (confctl_var_value(filter) != NULL)
		errx(1, "filter must not specify a value");

	if (strcmp(confctl_var_name(filter), confctl_var_name(cv)) != 0)
		return (false);

	for (child = confctl_var_first_child(cv); child != NULL; child = confctl_var_next(child)) {
		if (confctl_var_first_child(filter) == NULL) {
			found = true;
		} else {
			found = false;
			for (filterchild = confctl_var_first_child(filter); filterchild != NULL; filterchild = confctl_var_next(filterchild)) {
				if (cv_filter(child, filterchild))
					found = true;
			}
		}
		if (found)
			cv_mark(child, false);
		else
			cv_mark(child, true);
	}

	return (true);
}

static void
cc_filter(struct confctl *cc, struct confctl *filter)
{
	bool found;

	found = cv_filter(confctl_root(cc), confctl_root(filter));
	assert(found);
}

static char *
cv_safe_name(struct confctl_var *cv)
{
	const char *name;
	char *dst;

	name = confctl_var_name(cv);
	dst = malloc(strlen(name) * 4 + 1);
	if (dst == NULL)
		err(1, "malloc");
	strvis(dst, name, VIS_NL | VIS_CSTYLE);

	return (dst);
}

static char *
cv_safe_value(struct confctl_var *cv)
{
	const char *value;
	char *dst;

	value = confctl_var_value(cv);
	dst = malloc(strlen(value) * 4 + 1);
	if (dst == NULL)
		err(1, "malloc");
	strvis(dst, value, VIS_NL | VIS_CSTYLE);

	return (dst);
}

static void
cv_print(struct confctl_var *cv, FILE *fp, const char *prefix, bool values_only)
{
	struct confctl_var *child;
	char *newprefix, *name, *value;

	if (cv_marked(cv))
		return;

	if (confctl_var_has_children(cv)) {
		name = cv_safe_name(cv);
		if (prefix != NULL)
			asprintf(&newprefix, "%s.%s", prefix, name);
		else
			asprintf(&newprefix, "%s", name);
		free(name);
		if (newprefix == NULL)
			err(1, "asprintf");
		for (child = confctl_var_first_child(cv); child != NULL; child = confctl_var_next(child))
			cv_print(child, fp, newprefix, values_only);
		free(newprefix);
	} else if (confctl_var_has_value(cv)) {
		value = cv_safe_value(cv);
		if (values_only) {
			fprintf(fp, "%s\n", value);
		} else {
			name = cv_safe_name(cv);
			if (prefix != NULL)
				fprintf(fp, "%s.%s=%s\n", prefix, name, value);
			else
				fprintf(fp, "%s=%s\n", name, value);
			free(name);
		}
		free(value);
	}
}

void
cc_print(struct confctl *cc, FILE *fp, bool values_only)
{
	struct confctl_var *cv, *child;

	cv = confctl_root(cc);
	for (child = confctl_var_first_child(cv); child != NULL; child = confctl_var_next(child))
		cv_print(child, fp, NULL, values_only);
}

int
main(int argc, char **argv)
{
	int ch, i;
	bool aflag = false, Cflag = false, Eflag = false, Iflag = false, Sflag = false, nflag = false;
	struct confctl *cc, *line, *merge = NULL, *remove = NULL, *filter = NULL;

	if (argc <= 1)
		usage();

	while ((ch = getopt(argc, argv, "aCEISnw:x:")) != -1) {
		switch (ch) {
		case 'a':
			aflag = true;
			break;
		case 'C':
			Cflag = true;
			break;
		case 'E':
			Eflag = true;
			break;
		case 'I':
			Iflag = true;
			break;
		case 'S':
			Sflag = true;
			break;
		case 'n':
			nflag = true;
			break;
		case 'w':
			line = confctl_from_line(optarg);
			cc_merge(&merge, line);
			break;
		case 'x':
			line = confctl_from_line(optarg);
			cc_merge(&remove, line);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		errx(1, "missing config file path");
	if (merge && argc > 1)
		errx(1, "-w and variable names are mutually exclusive");
	if (remove && argc > 1)
		errx(1, "-x and variable names are mutually exclusive");
	if (aflag && merge)
		errx(1, "-a and -w are mutually exclusive");
	if (aflag && remove)
		errx(1, "-a and -x are mutually exclusive");
	if (nflag && merge)
		errx(1, "-n and -w are mutually exclusive");
	if (nflag && merge)
		errx(1, "-n and -x are mutually exclusive");
	if (aflag && argc > 1)
		errx(1, "-a and variable names are mutually exclusive");
	if (!aflag && !merge && !remove && argc == 1)
		errx(1, "neither -a, -w, -x, or variable names specified");

	cc = confctl_new();
	confctl_set_equals_sign(cc, Eflag);
	confctl_set_rewrite_in_place(cc, Iflag);
	confctl_set_semicolon(cc, Sflag);
	confctl_set_slash_slash_comments(cc, Cflag);
	confctl_set_slash_star_comments(cc, Cflag);
	confctl_load(cc, argv[0]);
	if (merge == NULL && remove == NULL) {
		if (!aflag) {
			for (i = 1; i < argc; i++) {
				line = confctl_from_line(argv[i]);
				cc_merge(&filter, line);
			}
			cc_filter(cc, filter);
		}
		cc_print(cc, stdout, nflag);
	} else {
		/*
		 * We're not using cv_filter() mechanism,
		 * because we really want to remove the nodes here,
		 * so that we can e.g. replace them by using -x
		 * and -w together.  Also, cv_filter() works
		 * the other way around, exposing selected nodes
		 * and hiding all the rest; we would need to 'invert'
		 * the filter somehow.
		 */
		if (remove != NULL)
			cc_remove(cc, remove);
		if (merge != NULL)
			cc_merge(&cc, merge);
		confctl_save(cc, argv[0]);
	}

	/*
	 * Note - this code does not try to free anything, since it would
	 * be useless for a program that does its job in short time and then
	 * exits.
	 */

	return (0);
}
