#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
	memset(b, 0, sizeof(*b)); /* XXX: For debugging. */
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

static struct confctl_var *
cv_new(struct confctl_var *parent, struct buf *name)
{
	struct confctl_var *cv;

	cv = calloc(sizeof(*cv), 1);
	if (cv == NULL)
		err(1, "malloc");

	assert(name != NULL);
	assert(name->b_len > 1);

	if (parent != NULL) {
		assert(parent->cv_value == NULL);
		cv->cv_parent = parent;
		if (cv->cv_parent->cv_last == NULL) {
			assert(cv->cv_parent->cv_first == NULL);
			cv->cv_parent->cv_first = cv;
		} else {
			assert(cv->cv_parent->cv_first != NULL);
			cv->cv_parent->cv_last->cv_next = cv;
		}
		cv->cv_parent->cv_last = cv;
	}

	cv->cv_name = name;

	return (cv);
}

static struct confctl_var *
cv_new_value(struct confctl_var *parent, struct buf *name, struct buf *value)
{
	struct confctl_var *cv;

	cv = cv_new(parent, name);
	cv->cv_value = value;

	return (cv);
}

static struct confctl *
confctl_new(void)
{
	struct confctl *cc;

	cc = calloc(sizeof(*cc), 1);
	if (cc == NULL)
		err(1, "malloc");
	cc->cc_first = cv_new(NULL, buf_new_from_str("."));
	return (cc);
}

static struct buf *
confctl_read_word(FILE *fp)
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
confctl_var_load(struct confctl_var *parent, FILE *fp)
{
	struct buf *name, *value;
	bool closing_bracket;
	struct confctl_var *cv;

	name = confctl_read_word(fp);
	if (name == NULL)
		return (true);
	if (strcmp(name->b_buf, "}") == 0)
		return (true);
	value = confctl_read_word(fp);
	if (value == NULL)
		errx(1, "name without value at EOF");
	if (strcmp(value->b_buf, "{") == 0) {
		cv = cv_new(parent, name);
		for (;;) {
			closing_bracket = confctl_var_load(cv, fp);
			if (closing_bracket)
				break;
		}
	} else
		cv_new_value(parent, name, value);
	return (false);
}

struct confctl *
confctl_load(const char *path)
{
	struct confctl *cc;
	FILE *fp;

	fp = fopen(path, "r");
	if (fp == NULL)
		err(1, "unable to open %s", path);

	cc = confctl_new();
	for (;;) {
		if (feof(fp) != 0)
			break;
		if (ferror(fp) != 0)
			err(1, "fgetc");
		confctl_var_load(cc->cc_first, fp);
	}

	return (cc);
}

static bool
confctl_var_is_container(const struct confctl_var *cv)
{

	if (cv->cv_first != NULL) {
		assert(cv->cv_value == NULL);
		return (true);
	}
	assert(cv->cv_value != NULL);
	return (false);
}

static const char *
confctl_var_name(const struct confctl_var *cv)
{

	assert(cv->cv_name != NULL);
	assert(cv->cv_name->b_buf != NULL);
	return (cv->cv_name->b_buf);
}

static const char *
confctl_var_value(const struct confctl_var *cv)
{

	assert(cv->cv_first == NULL);
	assert(cv->cv_value != NULL);
	assert(cv->cv_value->b_buf != NULL);
	return (cv->cv_value->b_buf);
}

static struct confctl_var *
confctl_var_first(const struct confctl_var *cv)
{

	assert(confctl_var_is_container(cv));
	return (cv->cv_first);
}

static struct confctl_var *
confctl_var_next(const struct confctl_var *cv)
{

	return (cv->cv_next);
}

static void
confctl_var_print_c(struct confctl_var *cv, int indent)
{
	struct confctl_var *child;

	if (confctl_var_is_container(cv)) {
		printf("%*s%s {\n", indent, " ", confctl_var_name(cv));
		for (child = confctl_var_first(cv); child != NULL; child = confctl_var_next(child))
			confctl_var_print_c(child, indent + 8);
		printf("%*s}\n", indent, " ");
	} else {
		printf("%*s%s %s\n", indent, " ", confctl_var_name(cv), confctl_var_value(cv));
	}
}

static void
confctl_var_print_lines(struct confctl_var *cv, const char *prefix)
{
	struct confctl_var *child;
	char *newprefix;

	if (confctl_var_is_container(cv)) {
		asprintf(&newprefix, "%s.%s", prefix, confctl_var_name(cv));
		if (newprefix == NULL)
			err(1, "asprintf");
		for (child = confctl_var_first(cv); child != NULL; child = confctl_var_next(child))
			confctl_var_print_lines(child, newprefix);
		free(newprefix);
	} else {
		printf("%s.%s=%s\n", prefix, confctl_var_name(cv), confctl_var_value(cv));
	}
}

void
confctl_print_c(struct confctl *cc)
{

	confctl_var_print_c(cc->cc_first, 0);
}

void
confctl_print_lines(struct confctl *cc)
{

	confctl_var_print_lines(cc->cc_first, "");
}
