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
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "queue.h"

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
	b->b_len--;
}

static char
buf_last(struct buf *b)
{
	char ch;

	assert(b->b_len >= 1);

	ch = b->b_buf[b->b_len - 1];
	assert(ch != '\0');

	return (ch);
}

static char
buf_strip(struct buf *b)
{

	assert(b->b_len > 0);
	b->b_len--;
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

static void
buf_print(struct buf *b, FILE *fp)
{
	size_t written;

	if (b == NULL)
		return;
	assert(b->b_len >= 0);
	if (b->b_len == 0)
		return;
	written = fwrite(b->b_buf, b->b_len, 1, fp);
	if (written != 1)
		err(1, "fwrite");
}

static struct confvar *
cv_new(struct confvar *parent, struct buf *name)
{
	struct confvar *cv;

	cv = calloc(sizeof(*cv), 1);
	if (cv == NULL)
		err(1, "malloc");

	assert(name != NULL);
	assert(name->b_len > 0);

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

static struct buf *
buf_read_before(FILE *fp)
{
	int ch;
	struct buf *b;
	bool comment = false, no_newline = false;;

	b = buf_new();

	for (;;) {
		ch = getc(fp);
		if (feof(fp) != 0)
			break;
		if (ferror(fp) != 0)
			err(1, "getc");
		if (no_newline && (ch == '\n' || ch == '\r')) {
			ch = ungetc(ch, fp);
			if (ch == EOF)
				err(1, "ungetc");
			break;
		}
		if (comment) {
			if (ch == '\n' || ch == '\r')
				comment = false;
			buf_append(b, ch);
			continue;
		}
		if (ch == '#') {
			comment = true;
			buf_append(b, ch);
			continue;
		}
		/*
		 * This is somewhat tricky - this piece of code is also used
		 * to parse junk that will become cv_after of the parent
		 * variable.
		 */
		if (ch == '}') {
			no_newline = true;
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
	//fprintf(stderr, "before '%s'\n", b->b_buf);
	return (b);
}

static struct buf *
buf_read_name(FILE *fp)
{
	int ch;
	struct buf *b;
	bool quoted = false, squoted = false, escaped = false;

	b = buf_new();

	for (;;) {
		ch = getc(fp);
		if (feof(fp) != 0) {
			if (quoted || squoted)
				errx(1, "premature end of file");
			break;
		}
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
		if (!squoted && ch == '"')
			quoted = !quoted;
		if (!quoted && ch == '\'')
			squoted = !squoted;
		if (quoted || squoted) {
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
buf_read_middle(FILE *fp, bool *opening_bracket)
{
	int ch;
	struct buf *b;
	bool escaped = false;

	*opening_bracket = false;

	b = buf_new();

	for (;;) {
		ch = getc(fp);
		if (feof(fp) != 0)
			break;
		if (ferror(fp) != 0)
			err(1, "getc");
		if (ch == '\\') {
			escaped = true;
			buf_append(b, ch);
			continue;
		}
		if (escaped) {
			escaped = false;
			if (ch == '\n' || ch == '\r') {
				buf_append(b, ch);
				continue;
			} else {
				/*
				 * The only escaped thing that's allowed
				 * in cv_middle are newlines.  All the rest
				 * goes to cv_value.
				 */
				ch = ungetc(ch, fp);
				if (ch == EOF)
					err(1, "ungetc");
				ch = buf_last(b);
				assert(ch == '\\');
				buf_strip(b);
				ch = ungetc(ch, fp);
				if (ch == EOF)
					err(1, "ungetc");
				break;
			}
		}
		if (ch == '\n' || ch == '\r' || ch == '#' || ch == ';') {
			ch = ungetc(ch, fp);
			if (ch == EOF)
				err(1, "ungetc");
			break;
		}
		if (ch == '{') {
			*opening_bracket = true;
			buf_append(b, ch);
			break;
		}
		if (isspace(ch)) {
			buf_append(b, ch);
			continue;
		}
		ch = ungetc(ch, fp);
		if (ch == EOF)
			err(1, "ungetc");
		break;
	}
	buf_finish(b);
	//fprintf(stderr, "middle '%s'\n", b->b_buf);
	return (b);
}

static struct buf *
buf_read_value(FILE *fp)
{
	int ch;
	struct buf *b;
	bool quoted = false, squoted = false, escaped = false;

	b = buf_new();

	for (;;) {
		ch = getc(fp);
		if (feof(fp) != 0) {
			if (quoted || squoted)
				errx(1, "premature end of file");
			break;
		}
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
		if (!squoted && ch == '"')
			quoted = !quoted;
		if (!quoted && ch == '\'')
			squoted = !squoted;
		if (quoted || squoted) {
			buf_append(b, ch);
			continue;
		}
		if (ch == '\n' || ch == '\r' || ch == '#' || ch == ';' || ch == '{' || ch == '}') {
			ch = ungetc(ch, fp);
			if (ch == EOF)
				err(1, "ungetc");
			for (;;) {
				if (b->b_len == 0)
					break;
				ch = buf_last(b);
				if (!isspace(ch))
					break;
				buf_strip(b);
				ch = ungetc(ch, fp);
				if (ch == EOF)
					err(1, "ungetc");
			}
			break;
		}
		buf_append(b, ch);
	}
	buf_finish(b);
	//fprintf(stderr, "value '%s'\n", b->b_buf);
	return (b);
}

static struct buf *
buf_read_after(FILE *fp)
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
		if (ch == '\n' || ch == '\r') {
			ch = ungetc(ch, fp);
			if (ch == EOF)
				err(1, "ungetc");
			break;
		}
		if (comment) {
			buf_append(b, ch);
			continue;
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
	//fprintf(stderr, "after '%s'\n", b->b_buf);
	return (b);
}

static bool
cv_load(struct confvar *parent, FILE *fp)
{
	struct buf *before, *name, *middle, *value, *after;
	bool closing_bracket, opening_bracket;
	struct confvar *cv;

	before = buf_read_before(fp);
	name = buf_read_name(fp);

	if (name->b_len == 0) {
		parent->cv_after = before;
		return (true);
	}

	middle = buf_read_middle(fp, &opening_bracket);

	cv = cv_new(parent, name);
	if (opening_bracket) {
		for (;;) {
			closing_bracket = cv_load(cv, fp);
			if (closing_bracket)
				break;
		}
	} else {
		value = buf_read_value(fp);
		cv->cv_value = value;
		after = buf_read_after(fp);
		cv->cv_after = after;
	}

	cv->cv_before = before;
	cv->cv_middle = middle;

	return (false);
}

struct confvar *
confvar_load(const char *path)
{
	struct confvar *cv;
	bool done;
	FILE *fp;

	fp = fopen(path, "r");
	if (fp == NULL)
		err(1, "unable to open %s", path);

	cv = cv_new_root();
	for (;;) {
		done = cv_load(cv, fp);
		if (done)
			break;
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

static void
confvar_save_in_place(struct confvar *cv, const char *path)
{
	FILE *fp;
	int error;

	fp = fopen(path, "w");
	if (fp == NULL)
		err(1, "cannot open %s", path);
	confvar_print_c(cv, fp);
	error = fflush(fp);
	if (error != 0)
		err(1, "fflush");
	error = fsync(fileno(fp));
	if (error != 0)
		err(1, "fsync");
	error = fclose(fp);
	if (error != 0)
		err(1, "fclose");
}

static void
confvar_save_atomic(struct confvar *cv, const char *path)
{
	FILE *fp;
	int error, fd;
	char *tmppath = NULL;

	asprintf(&tmppath, "%s.XXXXXXXXX", path);
	if (tmppath == NULL)
		err(1, "asprintf");
	fd = mkstemp(tmppath);
	if (fd < 0)
		err(1, "cannot create temporary file %s; use -I to rewrite file in place", tmppath);
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
		err(1, "fsync");
	}
	error = fclose(fp);
	if (error != 0) {
		remove_tmpfile(tmppath);
		err(1, "fclose");
	}
	error = rename(tmppath, path);
	if (error != 0) {
		remove_tmpfile(tmppath);
		err(1, "cannot replace %s; use -I to rewrite file in place", path);
	}
}

void
confvar_save(struct confvar *cv, const char *path, bool in_place)
{

	if (in_place)
		confvar_save_in_place(cv, path);
	else
		confvar_save_atomic(cv, path);

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

	buf_print(cv->cv_before, fp);
	buf_print(cv->cv_name, fp);
	buf_print(cv->cv_middle, fp);
	TAILQ_FOREACH(child, &cv->cv_children, cv_next)
		cv_print_c(child, fp);
	buf_print(cv->cv_value, fp);
	buf_print(cv->cv_after, fp);
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
	buf_print(cv->cv_after, fp);
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
	struct buf *b;
	bool escaped = false, quoted = false, squoted = false;
	int i;
	char ch;

	root = parent = cv_new_root();

	b = buf_new();
	for (i = 0;; i++) {
		ch = line[i];
		if (ch == '\0') {
			if (b->b_len == 0)
				errx(1, "empty name at the end of the line");
			buf_finish(b);
			cv = cv_new(parent, b);
			cv->cv_middle = buf_new_from_str(" ");
			return (root);
		}
		if (escaped) {
			buf_append(b, ch);
			escaped = false;
			continue;
		}
		if (ch == '\\') {
			escaped = true;
			continue;
		}
		if (!squoted && ch == '"')
			quoted = !quoted;
		if (!quoted && ch == '\'')
			squoted = !squoted;
		if (quoted || squoted) {
			buf_append(b, ch);
			continue;
		}
		if (ch == '.' || ch == '=') {
			if (b->b_len == 0)
				errx(1, "empty name");
			buf_finish(b);
			cv = cv_new(parent, b);
			cv->cv_middle = buf_new_from_str(" ");
			b = buf_new();
			if (ch == '.') {
				parent = cv;
				continue;
			}
			assert(ch == '=');
			for (i++;; i++) {
				ch = line[i];
				if (ch == '\0') {
					buf_finish(b);
					cv->cv_value = b;
					return (root);
				}
				buf_append(b, ch);
			}
		}
		buf_append(b, ch);
	}
}

static struct buf *
buf_get_indent(struct confvar *cv)
{
	struct buf *b;
	int i;

	b = cv->cv_before;
	if (b == NULL || b->b_len <= 1)
		return (NULL);

	for (i = b->b_len; i >= 0; i--) {
		if (b->b_buf[i] == '\n' || b->b_buf[i] == '\r')
			break;
	}

	b = buf_new_from_str(b->b_buf + i);

	return (b);
}

static void
cv_reindent(struct confvar *cv)
{
	struct buf *b = NULL;
	struct confvar *prev, *child;

	prev = TAILQ_PREV(cv, confvar_head, cv_next);
	if (prev != NULL)
		b = buf_get_indent(prev);
	if (b == NULL) {
		b = buf_get_indent(cv->cv_parent);
		if (b == NULL)
			b = buf_new_from_str("\n");
		if (cv->cv_parent->cv_parent != NULL) {
			buf_append(b, '\t');
			buf_finish(b);
		}
	}
	cv->cv_before = b;

	if (cv_is_container(cv)) {
		cv->cv_middle = buf_new_from_str(" {");
		cv->cv_after = buf_new_from_str(cv->cv_before->b_buf);
		buf_append(cv->cv_after, '}');
		buf_finish(cv->cv_after);

		TAILQ_FOREACH(child, &cv->cv_children, cv_next)
			cv_reindent(child);
	}
}

static void
cv_reparent(struct confvar *cv, struct confvar *parent)
{

	if (cv->cv_parent != NULL)
		TAILQ_REMOVE(&cv->cv_parent->cv_children, cv, cv_next);
	cv->cv_parent = parent;
	TAILQ_INSERT_TAIL(&parent->cv_children, cv, cv_next);
	cv_reindent(cv);
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
		found = false;
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
