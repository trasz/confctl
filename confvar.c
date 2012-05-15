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
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "confvar.h"
#include "confvar_private.h"

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
buf_append(struct buf *b, char ch)
{

	if (b->b_len + 1 >= b->b_allocated) {
		if (b->b_allocated == 0)
			b->b_allocated = 16;
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
buf_read_junk(FILE *fp, bool middle)
{
	int ch;
	struct buf *b;
	bool comment = false;

	b = buf_new();

	for (;;) {
		ch = getc(fp);
		if (feof(fp) != 0)
			break;
		if (ferror(fp) != 0)
			err(1, "getc");
		if (comment) {
			if (ch == '\n' || ch == '\r')
				comment = false;
			buf_append(b, ch);
			continue;
		}
		if (middle && (ch == '#' || ch == '\n' || ch == '\r' || ch == ';')) {
			ch = ungetc(ch, fp);
			if (ch == EOF)
				err(1, "ungetc");
			break;
		}
		if (ch == '#') {
			comment = true;
			buf_append(b, ch);
			continue;
		}
		if (isspace(ch) || ch == ';') {
			buf_append(b, ch);
			continue;
		}
		ch = ungetc(ch, fp);
		if (ch == EOF)
			err(1, "ungetc");
		break;
	}
	buf_finish(b);
	//fprintf(stderr, "junk '%s'\n", b->b_buf);
	return (b);
}

static struct buf *
buf_read_name(FILE *fp)
{
	int ch;
	struct buf *b;
	bool quoted = false, escaped = false;

	b = buf_new();

	for (;;) {
		ch = getc(fp);
		if (feof(fp) != 0)
			break;
		if (ferror(fp) != 0)
			err(1, "getc");
		if (escaped) {
			buf_append(b, ch);
			escaped = false;
			continue;
		}
		if (ch == '\\') {
			escaped = true;
			buf_append(b, ch);
			continue;
		}
		if (ch == '"')
			quoted = !quoted;
		if (quoted) {
			buf_append(b, ch);
			continue;
		}
		if (isspace(ch) || ch == '#' || ch == ';' || ch == '{' || ch == '}') {
			ch = ungetc(ch, fp);
			if (ch == EOF)
				err(1, "ungetc");
			break;
		}
		buf_append(b, ch);
	}
	buf_finish(b);
	//fprintf(stderr, "name '%s'\n", b->b_buf);
	return (b);
}

static struct buf *
buf_read_value(FILE *fp)
{
	int ch;
	struct buf *b;
	bool quoted = false, escaped = false;

	b = buf_new();

	for (;;) {
		ch = getc(fp);
		if (feof(fp) != 0)
			break;
		if (ferror(fp) != 0)
			err(1, "getc");
		if (escaped) {
			buf_append(b, ch);
			escaped = false;
			continue;
		}
		if (ch == '\\') {
			escaped = true;
			buf_append(b, ch);
			continue;
		}
		if (ch == '"')
			quoted = !quoted;
		if (quoted) {
			buf_append(b, ch);
			continue;
		}
		if ((ch == '{' || ch == '}') && b->b_len == 0) {
			buf_append(b, ch);
			break;
		}
		if (ch == '\n' || ch == '\r' || ch == '#' || ch == ';' || ch == '{' || ch == '}') {
			ch = ungetc(ch, fp);
			if (ch == EOF)
				err(1, "ungetc");
			break;
		}
		buf_append(b, ch);
	}
	buf_finish(b);
	//fprintf(stderr, "value '%s'\n", b->b_buf);
	return (b);
}

static bool
cv_load(struct confvar *parent, FILE *fp)
{
	struct buf *before, *name, *middle, *value, *after;
	bool closing_bracket;
	struct confvar *cv;

	before = buf_read_junk(fp, false);
	name = buf_read_name(fp);
	middle = buf_read_junk(fp, true);
	value = buf_read_value(fp);

	if (strcmp(value->b_buf, "}") == 0)
		return (true);

	if (strcmp(value->b_buf, "{") == 0) {
		cv = cv_new(parent, name);
		for (;;) {
			closing_bracket = cv_load(cv, fp);
			if (closing_bracket)
				break;
		}
	} else
		cv = cv_new_value(parent, name, value);

	after = buf_read_junk(fp, false);

	cv->cv_before = before;
	cv->cv_middle = middle;
	cv->cv_after = after;

	return (false);
}

struct confvar *
confvar_load(const char *path)
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
			err(1, "getc");
		cv_load(cv, fp);
	}

	return (cv);
}

static void
remove_tmpfile(const char *tmppath)
{
	int error, saved_errno;

	saved_errno = errno;
	error = unlink(tmppath);
	if (error != 0)
		warn("unlink");
	errno = saved_errno;
}

void
confvar_save(struct confvar *cv, const char *path)
{
	FILE *fp;
	int error, fd;
	char *tmppath = NULL;

	asprintf(&tmppath, "%s.XXXXXXXXX", path);
	if (tmppath == NULL)
		err(1, "asprintf");
	fd = mkstemp(tmppath);
	if (fd < 0)
		err(1, "cannot create temporary file %s", tmppath);
	fp = fdopen(fd, "w");
	if (fp == NULL) {
		remove_tmpfile(tmppath);
		err(1, "fdopen");
	}
	confvar_print_c(cv, fp);
	error = fflush(fp);
	if (error != 0) {
		remove_tmpfile(tmppath);
		err(1, "fflush");
	}
	error = fsync(fd);
	if (error != 0) {
		remove_tmpfile(tmppath);
		err(1, "sync");
	}
	error = fclose(fp);
	if (error != 0) {
		remove_tmpfile(tmppath);
		err(1, "fclose");
	}
	error = rename(tmppath, path);
	if (error != 0) {
		remove_tmpfile(tmppath);
		err(1, "cannot replace %s", path);
	}
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
cv_print_c(struct confvar *cv, FILE *fp)
{
	struct confvar *child;

	if (cv->cv_filtered_out)
		return;

	if (cv_is_container(cv)) {
		fprintf(fp, "%s%s%s{", cv->cv_before->b_buf, cv->cv_name->b_buf, cv->cv_middle->b_buf);
		TAILQ_FOREACH(child, &cv->cv_children, cv_next)
			cv_print_c(child, fp);
		fprintf(fp, "}%s", cv->cv_after->b_buf);
	} else
		fprintf(fp, "%s%s%s%s%s", cv->cv_before->b_buf, cv->cv_name->b_buf, cv->cv_middle->b_buf, cv->cv_value->b_buf, cv->cv_after->b_buf);
}

static void
cv_print_lines(struct confvar *cv, FILE *fp, const char *prefix, bool values_only)
{
	struct confvar *child;
	char *newprefix;

	if (cv->cv_filtered_out)
		return;

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
confvar_print_c(struct confvar *cv, FILE *fp)
{
	struct confvar *child;

	TAILQ_FOREACH(child, &cv->cv_children, cv_next)
		cv_print_c(child, fp);
}

void
confvar_print_lines(struct confvar *cv, FILE *fp, bool values_only)
{
	struct confvar *child;

	TAILQ_FOREACH(child, &cv->cv_children, cv_next)
		cv_print_lines(child, fp, NULL, values_only);
}

struct confvar *
confvar_from_line(const char *line)
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

	if (strcmp(cv->cv_name->b_buf, newcv->cv_name->b_buf) != 0)
		return (false);

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
confvar_merge(struct confvar **cvp, struct confvar *merge)
{
	bool found;

	if (*cvp == NULL)
		*cvp = cv_new_root();

	found = cv_merge(*cvp, merge);
	assert(found);
}

static bool
cv_filter(struct confvar *cv, struct confvar *filter)
{
	struct confvar *child, *filterchild;
	bool found;

	if (filter->cv_value != NULL)
		errx(1, "filter must not specify a value");

	if (strcmp(filter->cv_name->b_buf, cv->cv_name->b_buf) != 0)
		return (false);

	TAILQ_FOREACH(child, &cv->cv_children, cv_next) {
		if (TAILQ_EMPTY(&filter->cv_children)) {
			found = true;
		} else {
			found = false;
			TAILQ_FOREACH(filterchild, &filter->cv_children, cv_next) {
				if (cv_filter(child, filterchild))
					found = true;
			}
		}
		if (found)
			child->cv_filtered_out = false;
		else
			child->cv_filtered_out = true;
	}

	return (true);
}

void
confvar_filter(struct confvar *cv, struct confvar *filter)
{
	bool found;

	found = cv_filter(cv, filter);
	assert(found);
}
