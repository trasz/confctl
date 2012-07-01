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

#include <sys/file.h>
#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vis.h>

#include "queue.h"

#include "confctl.h"
#include "confctl_private.h"

static void	confctl_print_c(struct confctl *cc, FILE *fp);

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

static void
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

static char *
buf_vis(struct buf *b)
{
	char *dst;

	dst = malloc(b->b_len * 4 + 1);
	if (dst == NULL)
		err(1, "malloc");
	strvis(dst, b->b_buf, VIS_NL | VIS_CSTYLE);

	return (dst);
}

static void
buf_unvis(struct buf *b)
{
	char *dst;
	int len;

	dst = malloc(b->b_len + 1);
	if (dst == NULL)
		err(1, "malloc");
	len = strunvis(dst, b->b_buf);
	if (len < 0)
		err(1, "invalid escape sequence");
	assert(len <= b->b_allocated);
	memcpy(b->b_buf, dst, len);
	free(dst);
	b->b_len = len;
	buf_finish(b);
}

static struct buf *
buf_dup(const struct buf *b)
{

	return (buf_new_from_str(b->b_buf));
}

static void
buf_delete(struct buf *b)
{

	if (b == NULL)
		return;
	if (b->b_buf != NULL)
		free(b->b_buf);
	free(b);
}

static struct confctl_var *
cv_new(struct confctl_var *parent, struct buf *name)
{
	struct confctl_var *cv;

	cv = calloc(sizeof(*cv), 1);
	if (cv == NULL)
		err(1, "malloc");

	assert(name != NULL);

	if (parent != NULL) {
		assert(parent->cv_value == NULL);
		cv->cv_parent = parent;
		TAILQ_INSERT_TAIL(&parent->cv_children, cv, cv_next);
	}

	cv->cv_name = name;
	TAILQ_INIT(&cv->cv_children);

	return (cv);
}

static struct confctl_var *
cv_new_root(void)
{
	struct confctl_var *cv;

	cv = cv_new(NULL, buf_new_from_str("HKEY_CLASSES_ROOT"));

	return (cv);
}

static void
cv_delete(struct confctl_var *cv)
{
	struct confctl_var *child;

	TAILQ_FOREACH(child, &cv->cv_children, cv_next)
		cv_delete(child);

	buf_delete(cv->cv_before);
	buf_delete(cv->cv_name);
	buf_delete(cv->cv_middle);
	buf_delete(cv->cv_value);
	buf_delete(cv->cv_after);

	if (cv->cv_parent != NULL)
		TAILQ_REMOVE(&cv->cv_parent->cv_children, cv, cv_next);
}

static struct buf *
buf_read_before(FILE *fp, bool *closing_bracket)
{
	int ch;
	struct buf *b;
	bool comment = false, no_newline = false, slashed = false;

	*closing_bracket = false;

	b = buf_new();

	for (;;) {
		ch = getc(fp);
		if (ch == EOF) {
			*closing_bracket = true;
			break;
		}
		if (no_newline && (ch == '\n' || ch == '\r' || ch == '}'))
			goto unget;
		if (ch != '/') {
			if (slashed)
				goto unget;
			slashed = false;
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
#ifdef SLASH_SLASH
		/*
		 * Handle "// comments".
		 */
		if (ch == '/') {
			if (slashed) {
				slashed = false;
				comment = true;
			} else
				slashed = true;
			buf_append(b, ch);
			continue;
		}
#endif
		/*
		 * This is somewhat tricky - this piece of code is also used
		 * to parse junk that will become cv_after of the parent
		 * variable.
		 */
		if (ch == '}') {
			no_newline = true;
			*closing_bracket = true;
			buf_append(b, ch);
			continue;
		}
		if (isspace(ch) || ch == ';') {
			buf_append(b, ch);
			continue;
		}
unget:
		ch = ungetc(ch, fp);
		if (ch == EOF)
			err(1, "ungetc");
		if (slashed) {
			buf_strip(b);
			ch = ungetc('/', fp);
			if (ch == EOF)
				err(1, "ungetc");
		}
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
#ifdef SLASH_SLASH
	bool escaped = false, quoted = false, squoted = false, slashed = false;
#else
	bool escaped = false, quoted = false, squoted = false;
#endif

	b = buf_new();

	for (;;) {
		ch = getc(fp);
		if (ch == EOF) {
			if (quoted || squoted)
				errx(1, "premature end of file");
			break;
		}
		if (escaped) {
			buf_append(b, ch);
			escaped = false;
			continue;
		}
#ifdef SLASH_SLASH
		if (ch != '/')
			slashed = false;
#endif
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
		if (isspace(ch) || ch == '#' || ch == ';' || ch == '{' || ch == '}' || ch == '=') {
			ch = ungetc(ch, fp);
			if (ch == EOF)
				err(1, "ungetc");
			break;
		}
#ifdef SLASH_SLASH
		/*
		 * Handle "// comments".
		 */
		if (ch == '/') {
			if (slashed) {
				ch = ungetc(ch, fp);
				if (ch == EOF)
					err(1, "ungetc");
				buf_strip(b);
				ch = ungetc('/', fp);
				if (ch == EOF)
					err(1, "ungetc");
				break;
			}
			slashed = true;
		}
#endif
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
		if (ch == EOF)
			break;
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
		/*
		 * If there is no value, i.e. it's the end of the line,
		 * all that stuff including trailing spaces should go to cv_after,
		 * not cv_middle.
		 */
		if (ch == '\n' || ch == '\r' || ch == '#' || ch == ';') {
			ch = ungetc(ch, fp);
			if (ch == EOF)
				err(1, "ungetc");
			for (;;) {
				if (b->b_len == 0)
					break;
				ch = buf_last(b);
				if (!isspace(ch) && ch != '=')
					break;
				buf_strip(b);
				ch = ungetc(ch, fp);
				if (ch == EOF)
					err(1, "ungetc");
			}
			break;
		}
		if (isspace(ch) || ch == '=') {
			buf_append(b, ch);
			continue;
		}
		if (ch == '{' && *opening_bracket == false) {
			*opening_bracket = true;
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
buf_read_value(FILE *fp, bool *opening_bracket)
{
	int ch;
	struct buf *b;
	bool escaped = false, quoted = false, squoted = false, slashed = false;

	*opening_bracket = false;

	b = buf_new();

	for (;;) {
		ch = getc(fp);
		if (ch == EOF) {
			if (quoted || squoted)
				errx(1, "premature end of file");
			break;
		}
		if (escaped) {
			buf_append(b, ch);
			escaped = false;
			continue;
		}
		if (ch != '/')
			slashed = false;
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
		if (ch == '\n' || ch == '\r' || ch == '#' || ch == ';' || ch == '{' || ch == '}' || (ch == '/' && slashed)) {
			if (ch == '{')
				*opening_bracket = true;
			ch = ungetc(ch, fp);
			if (ch == EOF)
				err(1, "ungetc");
			if (slashed) {
				buf_strip(b);
				ch = ungetc('/', fp);
				if (ch == EOF)
					err(1, "ungetc");
			}

			/*
			 * All the trailing whitespace after the value should go into cv_after.
			 */
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
#ifdef SLASH_SLASH
		if (ch == '/')
			slashed = true;
#endif
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
	bool comment = false, slashed = false;

	b = buf_new();

	for (;;) {
		ch = getc(fp);
		if (ch == EOF)
			break;
		if (ch != '/') {
			if (slashed)
				goto unget;
			slashed = false;
		}
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
#ifdef SLASH_SLASH
		/*
		 * Handle "// comments".
		 */
		if (ch == '/') {
			if (slashed) {
				slashed = false;
				comment = true;
			} else
				slashed = true;
			buf_append(b, ch);
			continue;
		}
#endif
		if (isspace(ch) || ch == ';') {
			buf_append(b, ch);
			continue;
		}
unget:
		ch = ungetc(ch, fp);
		if (ch == EOF)
			err(1, "ungetc");
		if (slashed) {
			buf_strip(b);
			ch = ungetc('/', fp);
			if (ch == EOF)
				err(1, "ungetc");
		}
		break;
	}
	buf_finish(b);
	//fprintf(stderr, "after '%s'\n", b->b_buf);
	return (b);
}

static bool
cv_load(struct confctl_var *parent, FILE *fp)
{
	struct buf *before, *name, *middle, *value, *after;
	bool closing_bracket, opening_bracket;
	struct confctl_var *cv;

	/*
	 * There are three cases here:
	 *
	 * 1. "         variable          variable_value  # a comment"
	 *    |<before>||<name>||<middle>||<-- value ->||<- after ->|
	 *
	 * 2. "         variable         {             some_stuff ...
	 *    |<before>||<name>||<middle>||< before2 >||<name2 >|
	 *
	 * 3. "         variable          whatever_else    {      some_stuff ...
	 *    |<before>||<name>||<middle>||<- name2 ->||<middle2>||<name3 >|
	 */

	before = buf_read_before(fp, &closing_bracket);
	if (closing_bracket) {
		parent->cv_after = before;
		return (true);
	}

	name = buf_read_name(fp);
	middle = buf_read_middle(fp, &opening_bracket);

	cv = cv_new(parent, name);
	cv->cv_before = before;
	cv->cv_middle = middle;

	if (opening_bracket) {
		/*
		 * Case 2 - opening bracket after name.
		 */
		for (;;) {
			closing_bracket = cv_load(cv, fp);
			if (closing_bracket)
				break;
		}
	} else {
		/*
		 * Case 1 or 3.
		 */
		value = buf_read_value(fp, &opening_bracket);
		if (opening_bracket) {
			/*
			 * Case 3.
			 */
			/*
			 * Say we have this in the configuration
			 * file: 'on a { whatever'.  When we do
			 * "confctl -x on.a", we want to remove not
			 * only the 'a' node, but also its parent,
			 * 'on'.
			 */
			cv->cv_delete_when_empty = true;

			middle = buf_read_middle(fp, &opening_bracket);
			assert(opening_bracket);
			cv = cv_new(cv, value);
			cv->cv_middle = middle;

			for (;;) {
				closing_bracket = cv_load(cv, fp);
				if (closing_bracket)
					break;
			}
		} else {
			/*
			 * Case 1.
			 */
			after = buf_read_after(fp);
			cv->cv_value = value;
			cv->cv_after = after;
		}
	}

	return (false);
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
confctl_save_in_place(struct confctl *cc, const char *path)
{
	FILE *fp;
	int error;

	fp = fopen(path, "w");
	if (fp == NULL)
		err(1, "cannot open %s", path);
	error = flock(fileno(fp), LOCK_EX);
	if (error != 0)
		err(1, "unable to lock %s", path);
	confctl_print_c(cc, fp);
	error = fflush(fp);
	if (error != 0)
		err(1, "fflush");
	error = fsync(fileno(fp));
	if (error != 0)
		err(1, "fsync");
	error = flock(fileno(fp), LOCK_UN);
	if (error != 0)
		err(1, "unable to unlock %s", path);
	error = fclose(fp);
	if (error != 0)
		err(1, "fclose");
}

static void
confctl_save_atomic(struct confctl *cc, const char *path)
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
	confctl_print_c(cc, fp);
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
confctl_var_save(struct confctl_var *cv, const char *path, bool in_place)
{


}

static bool
cv_is_container(const struct confctl_var *cv)
{

	if (cv->cv_value == NULL)
		return (true);
	return (false);
}

static void
cv_print_c(struct confctl_var *cv, FILE *fp)
{
	struct confctl_var *child;

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
confctl_print_c(struct confctl *cc, FILE *fp)
{
	struct confctl_var *cv, *child;

	cv = confctl_root(cc);
	TAILQ_FOREACH(child, &cv->cv_children, cv_next)
		cv_print_c(child, fp);
	buf_print(cv->cv_after, fp);
}

static void
cv_print_lines(struct confctl_var *cv, FILE *fp, const char *prefix, bool values_only)
{
	struct confctl_var *child;
	char *newprefix, *name, *value;

	if (cv->cv_filtered_out)
		return;

	if (cv_is_container(cv)) {
		name = buf_vis(cv->cv_name);
		if (prefix != NULL)
			asprintf(&newprefix, "%s.%s", prefix, name);
		else
			asprintf(&newprefix, "%s", name);
		free(name);
		if (newprefix == NULL)
			err(1, "asprintf");
		TAILQ_FOREACH(child, &cv->cv_children, cv_next)
			cv_print_lines(child, fp, newprefix, values_only);
		free(newprefix);
	} else {
		value = buf_vis(cv->cv_value);
		if (values_only) {
			fprintf(fp, "%s\n", value);
		} else {
			name = buf_vis(cv->cv_name);
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
confctl_print_lines(struct confctl *cc, FILE *fp, bool values_only)
{
	struct confctl_var *cv, *child;

	cv = confctl_root(cc);
	TAILQ_FOREACH(child, &cv->cv_children, cv_next)
		cv_print_lines(child, fp, NULL, values_only);
}

struct confctl_var *
confctl_var_from_line(const char *line)
{
	struct confctl_var *cv, *parent, *root;
	struct buf *b;
	bool escaped = false, quoted = false, squoted = false;
	int i;
	char ch;

	root = parent = cv_new_root();

	b = buf_new();
	for (i = 0;; i++) {
		ch = line[i];
		if (ch == '\0') {
			buf_finish(b);
			buf_unvis(b);
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
			buf_finish(b);
			buf_unvis(b);
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
					buf_unvis(b);
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
buf_get_indent(struct confctl_var *cv)
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
cv_reindent(struct confctl_var *cv)
{
	struct buf *b = NULL;
	struct confctl_var *prev, *child;

	prev = TAILQ_PREV(cv, confctl_var_head, cv_next);
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
cv_merge_existing(struct confctl_var *cv, struct confctl_var *newcv)
{
	struct confctl_var *child, *newchild, *tmp, *newtmp;

	if (strcmp(cv->cv_name->b_buf, newcv->cv_name->b_buf) != 0)
		return;

	if (!cv_is_container(newcv)) {
		if (cv_is_container(cv))
			errx(1, "cannot replace container node with leaf node");
		if (cv->cv_middle->b_len == 0)
			cv->cv_middle = buf_dup(newcv->cv_middle);
		cv->cv_value = buf_dup(newcv->cv_value);
		/*
		 * Mark the node as done, so that we won't try
		 * to add it in cv_merge_new().
		 */
		newcv->cv_filtered_out = true;
		return;
	}

	TAILQ_FOREACH_SAFE(newchild, &newcv->cv_children, cv_next, newtmp) {
		TAILQ_FOREACH_SAFE(child, &cv->cv_children, cv_next, tmp)
			cv_merge_existing(child, newchild);
	}
}

static void
cv_reparent(struct confctl_var *cv, struct confctl_var *parent)
{

	if (cv->cv_parent != NULL)
		TAILQ_REMOVE(&cv->cv_parent->cv_children, cv, cv_next);
	cv->cv_parent = parent;
	TAILQ_INSERT_TAIL(&parent->cv_children, cv, cv_next);
	cv_reindent(cv);
}

static bool
cv_merge_new(struct confctl_var *cv, struct confctl_var *newcv)
{
	struct confctl_var *child, *newchild, *tmp, *newtmp;
	bool found;

	if (strcmp(cv->cv_name->b_buf, newcv->cv_name->b_buf) != 0)
		return (false);

	if (newcv->cv_filtered_out)
		return (true);

	TAILQ_FOREACH_SAFE(newchild, &newcv->cv_children, cv_next, newtmp) {
		found = false;
		TAILQ_FOREACH_SAFE(child, &cv->cv_children, cv_next, tmp) {
			found = cv_merge_new(child, newchild);
			if (found)
				break;
		}
		if (!found)
			cv_reparent(newchild, cv);
	}

	return (true);
}

void
confctl_var_merge(struct confctl_var **cvp, struct confctl_var *merge)
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

void
confctl_var_remove(struct confctl_var *cv, struct confctl_var *remove)
{
	struct confctl_var *child, *removechild, *tmp;

	if (remove->cv_value != NULL)
		errx(1, "variable to remove must not specify a value");

	if (strcmp(remove->cv_name->b_buf, cv->cv_name->b_buf) != 0)
		return;

	if (TAILQ_EMPTY(&remove->cv_children)) {
		cv_delete(cv);
	} else {
		TAILQ_FOREACH_SAFE(child, &cv->cv_children, cv_next, tmp) {
			TAILQ_FOREACH(removechild, &remove->cv_children, cv_next)
				confctl_var_remove(child, removechild);
		}
	}

	if (cv->cv_delete_when_empty && TAILQ_EMPTY(&cv->cv_children))
		cv_delete(cv);
}

static bool
cv_filter(struct confctl_var *cv, struct confctl_var *filter)
{
	struct confctl_var *child, *filterchild;
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
confctl_var_filter(struct confctl_var *cv, struct confctl_var *filter)
{
	bool found;

	found = cv_filter(cv, filter);
	assert(found);
}

struct confctl *
confctl_init(bool rewrite_in_place)
{
	struct confctl *cc;

	cc = calloc(sizeof(*cc), 1);
	if (cc == NULL)
		err(1, "calloc");
	cc->cc_rewrite_in_place = rewrite_in_place;
	cc->cc_root = cv_new_root();

	return (cc);
}

void	
confctl_load(struct confctl *cc, const char *path)
{
	bool done;
	FILE *fp;
	int error;

	fp = fopen(path, "r");
	if (fp == NULL)
		err(1, "unable to open %s", path);

	if (cc->cc_rewrite_in_place) {
		error = flock(fileno(fp), LOCK_SH);
		if (error != 0)
			err(1, "unable to lock %s", path);
	}

	for (;;) {
		done = cv_load(confctl_root(cc), fp);
		if (ferror(fp) != 0)
			err(1, "read");
		if (done)
			break;
	}

	if (cc->cc_rewrite_in_place) {
		error = flock(fileno(fp), LOCK_UN);
		if (error != 0)
			err(1, "unable to unlock %s", path);
	}

	error = fclose(fp);
	if (error != 0)
		err(1, "fclose");
}

void	
confctl_save(struct confctl *cc, const char *path)
{

	if (cc->cc_rewrite_in_place)
		confctl_save_in_place(cc, path);
	else
		confctl_save_atomic(cc, path);
}

struct confctl_var *
confctl_root(struct confctl *cc)
{

	return (cc->cc_root);
}