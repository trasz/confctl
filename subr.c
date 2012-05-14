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

#include <sys/queue.h>
#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "confctl.h"
#include "confctl_private.h"

static struct buf *
buf_new(void)
{
	struct buf *b;

	b = calloc(sizeof(*b), 1);
	if (b == NULL)
		err(1, "malloc");
	return (b);
}

static void
buf_delete(struct buf *b)
{

	if (b->b_buf != NULL)
		free(b->b_buf);
#if 1
	memset(b, 42, sizeof(*b)); /* XXX: For debugging. */
#endif
	free(b);
}

static void
buf_append(struct buf *b, char ch)
{

	if (b->b_len + 1 >= b->b_allocated) {
		if (b->b_allocated == 0)
			b->b_allocated = 1; /* XXX */
		else
			b->b_allocated *= 4;
		b->b_buf = realloc(b->b_buf, b->b_allocated);
		if (b->b_buf == NULL)
			err(1, "realloc");
	}

	b->b_buf[b->b_len] = ch;
	b->b_len++;
}

static void
buf_finish(struct buf *b)
{

	buf_append(b, '\0');
}

static struct buf *
buf_new_from_str(const char *str)
{
	struct buf *b;
	const char *p;

	b = buf_new();
	for (p = str; *p != '\0'; p++)
		buf_append(b, *p);
	buf_finish(b);
	return (b);
}

static struct confvar *
cv_new(struct confvar *parent, struct buf *name)
{
	struct confvar *cv;

	cv = calloc(sizeof(*cv), 1);
	if (cv == NULL)
		err(1, "malloc");

	assert(name != NULL);
	assert(name->b_len > 1);

	if (parent != NULL) {
		assert(parent->cv_value == NULL);
		cv->cv_parent = parent;
		TAILQ_INSERT_TAIL(&parent->cv_children, cv, cv_next);
	}

	cv->cv_name = name;
	TAILQ_INIT(&cv->cv_children);

	return (cv);
}

static struct confvar *
cv_new_root(void)
{
	struct confvar *cv;

	cv = cv_new(NULL, buf_new_from_str("HKEY_CLASSES_ROOT"));

	return (cv);
}

static void
cv_delete_quick(struct confvar *cv)
{
	struct confvar *child, *tmp;

	TAILQ_FOREACH_SAFE(child, &cv->cv_children, cv_next, tmp)
		cv_delete_quick(child);

	buf_delete(cv->cv_name);
	if (cv->cv_value != NULL)
		buf_delete(cv->cv_value);

#if 1
	memset(cv, 1984, sizeof(*cv)); /* XXX: For debugging. */
#endif

	free(cv);
}

static void
cv_delete(struct confvar *cv)
{

	if (cv->cv_parent != NULL)
		TAILQ_REMOVE(&cv->cv_parent->cv_children, cv, cv_next);
	cv_delete_quick(cv);
}

static struct confvar *
cv_new_value(struct confvar *parent, struct buf *name, struct buf *value)
{
	struct confvar *cv;

	cv = cv_new(parent, name);
	cv->cv_value = value;

	return (cv);
}


static void
cv_reparent(struct confvar *cv, struct confvar *parent)
{

	if (cv->cv_parent != NULL)
		TAILQ_REMOVE(&cv->cv_parent->cv_children, cv, cv_next);
	cv->cv_parent = parent;
	TAILQ_INSERT_TAIL(&parent->cv_children, cv, cv_next);
}

static struct buf *
cv_read_word(FILE *fp)
{
	int ch;
	struct buf *b;

	b = buf_new();

	for (;;) {
		ch = fgetc(fp);
		if (feof(fp) != 0)
			break;
		if (ferror(fp) != 0)
			err(1, "fgetc");
		if (isspace(ch)) {
			if (b->b_len == 0)
				continue;
			break;
		}
		buf_append(b, ch);
	}

	if (b->b_len == 0) {
		buf_delete(b);
		return (NULL);
	}

	buf_finish(b);
	return (b);
}

static bool
cv_load(struct confvar *parent, FILE *fp)
{
	struct buf *name, *value;
	bool closing_bracket;
	struct confvar *cv;

	name = cv_read_word(fp);
	if (name == NULL)
		return (true);
	if (strcmp(name->b_buf, "}") == 0)
		return (true);
	value = cv_read_word(fp);
	if (value == NULL)
		errx(1, "name without value at EOF");
	if (strcmp(value->b_buf, "{") == 0) {
		cv = cv_new(parent, name);
		for (;;) {
			closing_bracket = cv_load(cv, fp);
			if (closing_bracket)
				break;
		}
	} else
		cv_new_value(parent, name, value);
	return (false);
}

struct confvar *
confctl_load(const char *path)
{
	struct confvar *cv;
	FILE *fp;

	fp = fopen(path, "r");
	if (fp == NULL)
		err(1, "unable to open %s", path);

	cv = cv_new_root();
	for (;;) {
		if (feof(fp) != 0)
			break;
		if (ferror(fp) != 0)
			err(1, "fgetc");
		cv_load(cv, fp);
	}

	return (cv);
}

void
confctl_save(struct confvar *cv, const char *path)
{
	FILE *fp;
	int error;

	/*
	 * XXX: Modifying in place.
	 */

	fp = fopen(path, "w");
	if (fp == NULL)
		err(1, "unable to open %s for writing", path);
	confctl_print_c(cv, fp);
	error = fflush(fp);
	if (error != 0)
		err(1, "fflush");
	error = fsync(fileno(fp));
	if (error != 0)
		err(1, "sync");
	error = fclose(fp);
	if (error != 0)
		err(1, "fclose");
}

static bool
cv_is_container(const struct confvar *cv)
{

	if (cv->cv_value == NULL)
		return (true);
	return (false);
}

static const char *
cv_name(const struct confvar *cv)
{

	assert(cv->cv_name != NULL);
	assert(cv->cv_name->b_buf != NULL);
	return (cv->cv_name->b_buf);
}

static const char *
cv_value(const struct confvar *cv)
{

	assert(cv->cv_value != NULL);
	assert(cv->cv_value->b_buf != NULL);
	return (cv->cv_value->b_buf);
}

static void
cv_print_c(struct confvar *cv, FILE *fp, int indent)
{
	struct confvar *child;

	if (cv_is_container(cv)) {
		fprintf(fp, "%*s%s {\n", indent, "", cv_name(cv));
		TAILQ_FOREACH(child, &cv->cv_children, cv_next)
			cv_print_c(child, fp, indent + 8);
		fprintf(fp, "%*s}\n", indent, "");
	} else {
		fprintf(fp, "%*s%s %s\n", indent, "", cv_name(cv), cv_value(cv));
	}
}

static void
cv_print_lines(struct confvar *cv, FILE *fp, const char *prefix, bool values_only)
{
	struct confvar *child;
	char *newprefix;

	if (cv_is_container(cv)) {
		if (prefix != NULL)
			asprintf(&newprefix, "%s.%s", prefix, cv_name(cv));
		else
			asprintf(&newprefix, "%s", cv_name(cv));
		if (newprefix == NULL)
			err(1, "asprintf");
		TAILQ_FOREACH(child, &cv->cv_children, cv_next)
			cv_print_lines(child, fp, newprefix, values_only);
		free(newprefix);
	} else
		if (values_only)
			fprintf(fp, "%s\n", cv_value(cv));
		else if (prefix != NULL)
			fprintf(fp, "%s.%s=%s\n", prefix, cv_name(cv), cv_value(cv));
		else
			fprintf(fp, "%s=%s\n", cv_name(cv), cv_value(cv));
}

void
confctl_print_c(struct confvar *cv, FILE *fp)
{
	struct confvar *child;

	TAILQ_FOREACH(child, &cv->cv_children, cv_next)
		cv_print_c(child, fp, 0);
}

void
confctl_print_lines(struct confvar *cv, FILE *fp, bool values_only)
{
	struct confvar *child;

	TAILQ_FOREACH(child, &cv->cv_children, cv_next)
		cv_print_lines(child, fp, NULL, values_only);
}

struct confvar *
confctl_from_line(const char *line)
{
	struct confvar *cv, *parent, *root;
	char *name, *value, *next, *tofree;

	next = tofree = strdup(line);
	if (next == NULL)
		err(1, "strdup");

	root = parent = cv_new_root();

	while ((name = strsep(&next, ".")) != NULL) {
		value = name;
		name = strsep(&value, "=");
		if (value != NULL) {
			if (next != NULL)
				errx(1, "trailing name (%s) after value (%s)", next, value);
			cv = cv_new_value(parent, buf_new_from_str(name), buf_new_from_str(value));
		} else
			cv = cv_new(parent, buf_new_from_str(name));
		parent = cv;
	}

	free(tofree);

	return (root);
}

static bool
cv_merge(struct confvar *cv, struct confvar *newcv)
{
	struct confvar *child, *newchild, *tmp, *newtmp;
	bool found;

	if (strcmp(cv->cv_name->b_buf, newcv->cv_name->b_buf) != 0) {
		return (false);
	}

	if (TAILQ_EMPTY(&newcv->cv_children)) {
		cv->cv_value = newcv->cv_value;
		TAILQ_FOREACH_SAFE(newchild, &newcv->cv_children, cv_next, newtmp)
			cv_reparent(newchild, cv);
		return (true);
	}

	TAILQ_FOREACH_SAFE(newchild, &newcv->cv_children, cv_next, newtmp) {
		TAILQ_FOREACH_SAFE(child, &cv->cv_children, cv_next, tmp) {
			found = cv_merge(child, newchild);
			if (found)
				break;
		}
		if (!found)
			cv_reparent(newchild, cv);
	}

	return (true);
}

void
confctl_merge(struct confvar **cvp, struct confvar *merge)
{
	bool found;

	if (*cvp == NULL)
		*cvp = cv_new_root();

	found = cv_merge(*cvp, merge);
	assert(found);
}

void
confctl_filter(struct confvar *cv, struct confvar *filter)
{
	struct confvar *child, *tmp;

	if (filter == NULL)
		return;
	if (filter->cv_value != NULL)
		errx(1, "filter must not specify a value");

	if (strcmp(filter->cv_name->b_buf, cv->cv_name->b_buf) != 0) {
		cv_delete(cv);
		return;
	}

	TAILQ_FOREACH_SAFE(child, &cv->cv_children, cv_next, tmp)
		confctl_filter(child, TAILQ_FIRST(&filter->cv_children));
}
