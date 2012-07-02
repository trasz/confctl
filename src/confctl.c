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
#include "confctl_private.h"

static void
usage(void)
{
	fprintf(stderr, "usage: confctl [-In] config-path [name...]\n");
	fprintf(stderr, "       confctl [-In] -a config-path\n");
	fprintf(stderr, "       confctl [-I] -w name=value config-path\n");
	fprintf(stderr, "       confctl [-I] -x name config-path\n");
	exit(1);
}

static bool
cv_filtered_out(struct confctl_var *cv)
{
	if (confctl_var_uptr(cv) != NULL)
		return (true);
	return (false);
}

static void
cv_set_filtered_out(struct confctl_var *cv, bool v)
{
	if (v)
		confctl_var_set_uptr(cv, (void *)1);
	else
		confctl_var_set_uptr(cv, NULL);
}

static void
cv_merge_existing(struct confctl_var *cv, struct confctl_var *newcv)
{
	struct confctl_var *child, *newchild, *tmp, *newtmp;

	if (strcmp(confctl_var_name(cv), confctl_var_name(newcv)) != 0)
		return;

	if (!confctl_var_is_container(newcv)) {
		if (confctl_var_is_container(cv))
			errx(1, "cannot replace container node with leaf node");
		confctl_var_set_value(cv, confctl_var_value(newcv));
		/*
		 * Mark the node as done, so that we won't try
		 * to add it in cv_merge_new().
		 */
		cv_set_filtered_out(newcv, true);
		return;
	}

	TAILQ_FOREACH_SAFE(newchild, &newcv->cv_children, cv_next, newtmp) {
		TAILQ_FOREACH_SAFE(child, &cv->cv_children, cv_next, tmp)
			cv_merge_existing(child, newchild);
	}
}

static bool
cv_merge_new(struct confctl_var *cv, struct confctl_var *newcv)
{
	struct confctl_var *child, *newchild, *tmp, *newtmp;
	bool found;

	if (strcmp(confctl_var_name(cv), confctl_var_name(newcv)) != 0)
		return (false);

	if (cv_filtered_out(newcv))
		return (true);

	TAILQ_FOREACH_SAFE(newchild, &newcv->cv_children, cv_next, newtmp) {
		found = false;
		TAILQ_FOREACH_SAFE(child, &cv->cv_children, cv_next, tmp) {
			found = cv_merge_new(child, newchild);
			if (found)
				break;
		}
		if (!found)
			confctl_var_move(newchild, cv);
	}

	return (true);
}

static void
cc_var_merge(struct confctl_var **cvp, struct confctl_var *merge)
{

	if (*cvp == NULL)
		*cvp = cv_new_root();

	/*
	 * Reason for doing it in two steps is that we need
	 * to correctly handle duplicate nodes, such as this:
	 * "1 { foo } 2 { bar } 1 { baz }".  In this case,
	 * when merging '1.baz', we want to update the existing
	 * node, not add a new sibling to "foo".
	 */
	cv_merge_existing(*cvp, merge);
	cv_merge_new(*cvp, merge);
}

static void
cc_var_remove(struct confctl_var *cv, struct confctl_var *remove)
{
	struct confctl_var *child, *removechild, *tmp;

	if (remove->cv_value != NULL)
		errx(1, "variable to remove must not specify a value");

	if (strcmp(confctl_var_name(remove), confctl_var_name(cv)) != 0)
		return;

	if (confctl_var_first_child(remove) == NULL) {
		confctl_var_delete(cv);
	} else {
		TAILQ_FOREACH_SAFE(child, &cv->cv_children, cv_next, tmp) {
			TAILQ_FOREACH(removechild, &remove->cv_children, cv_next)
				cc_var_remove(child, removechild);
		}
	}

	if (confctl_var_delete_when_empty(cv) && confctl_var_first_child(cv) == NULL)
		confctl_var_delete(cv);
}

static bool
cv_filter(struct confctl_var *cv, struct confctl_var *filter)
{
	struct confctl_var *child, *filterchild;
	bool found;

	if (filter->cv_value != NULL)
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
			cv_set_filtered_out(child, false);
		else
			cv_set_filtered_out(child, true);
	}

	return (true);
}

static void
cc_var_filter(struct confctl_var *cv, struct confctl_var *filter)
{
	bool found;

	found = cv_filter(cv, filter);
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

	if (cv_filtered_out(cv))
		return;

	if (confctl_var_is_container(cv)) {
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
	} else {
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
	bool aflag = false, Iflag = false, nflag = false;
	struct confctl *cc;
	struct confctl_var *root, *cv, *filter = NULL, *merge = NULL, *remove = NULL;

	if (argc <= 1)
		usage();

	while ((ch = getopt(argc, argv, "aInw:x:")) != -1) {
		switch (ch) {
		case 'a':
			aflag = true;
			break;
		case 'I':
			Iflag = true;
			break;
		case 'n':
			nflag = true;
			break;
		case 'w':
			cv = confctl_var_from_line(optarg);
			cc_var_merge(&merge, cv);
			break;
		case 'x':
			cv = confctl_var_from_line(optarg);
			cc_var_merge(&remove, cv);
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

	cc = confctl_init(Iflag);
	confctl_load(cc, argv[0]);
	root = confctl_root(cc);
	if (merge == NULL && remove == NULL) {
		if (!aflag) {
			for (i = 1; i < argc; i++) {
				cv = confctl_var_from_line(argv[i]);
				cc_var_merge(&filter, cv);
			}
			cc_var_filter(root, filter);
		}
		cc_print(cc, stdout, nflag);
	} else {
		/*
		 * We're not using cc_var_filter() mechanism,
		 * because we really want to remove the nodes here,
		 * so that we can e.g. replace them by using -x
		 * and -w together.  Also, cc_var_filter() works
		 * the other way around, exposing selected nodes
		 * and hiding all the rest; we would need to 'invert'
		 * the filter somehow.
		 */
		if (remove != NULL)
			cc_var_remove(root, remove);
		if (merge != NULL)
			cc_var_merge(&root, merge);
		confctl_save(cc, argv[0]);
	}

	return (0);
}
