#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confctl.h"
#include "confctl_private.h"

static struct confctl_var *
cv_new(struct confctl_var *parent, const char *name)
{
	struct confctl_var *cv;

	cv = calloc(sizeof(*cv), 1);
	if (cv == NULL)
		err(1, "malloc");

	if (parent != NULL) {
		assert(parent->cv_value == NULL);
		cv->cv_parent = parent;
		/* XXX: Kolejność. */
		cv->cv_next = cv->cv_parent->cv_first;
		cv->cv_parent->cv_first = cv;
	}

	cv->cv_name = strdup(name);
	if (cv->cv_name == NULL)
		err(1, "strdup");

	return (cv);
}

static struct confctl_var *
cv_new_value(struct confctl_var *parent, const char *name, const char *value)
{
	struct confctl_var *cv;

	cv = cv_new(parent, name);
	cv->cv_value = strdup(value);
	if (cv->cv_value == NULL)
		err(1, "strdup");

	return (cv);
}

static struct confctl *
confctl_new(void)
{
	struct confctl *cc;

	cc = calloc(sizeof(*cc), 1);
	if (cc == NULL)
		err(1, "malloc");
	cc->cc_first = cv_new(NULL, ".");
	return (cc);
}

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

static char *
confctl_read_word(FILE *fp)
{
	int ch;
	struct buf *b;
	char *str;

	b = buf_new();

	for (;;) {
		ch = fgetc(fp);
		if (feof(fp) != 0)
			break;
		if (ferror(fp) != 0)
			err(1, "fgetc");
		if (isspace(ch))
			break;
		buf_append(b, ch);
	}

	str = strdup(b->b_buf);
	if (str == NULL)
		err(1, "strdup");
	buf_delete(b);

	return (str);
}

struct confctl *
confctl_load(const char *path)
{
	struct confctl *cc;
	FILE *fp;
	char *name, *value;

	fp = fopen(path, "r");
	if (fp == NULL)
		err(1, "unable to open %s", path);

	cc = confctl_new();
	for (;;) {
		if (feof(fp) != 0)
			break;
		if (ferror(fp) != 0)
			err(1, "fgetc");
		name = confctl_read_word(fp);
		value = confctl_read_word(fp);
		cv_new_value(cc->cc_first, name, value);
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

	return (cv->cv_name);
}

static const char *
confctl_var_value(const struct confctl_var *cv)
{

	assert(cv->cv_first == NULL);
	assert(cv->cv_value != NULL);
	return (cv->cv_value);
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
		printf("%*s %s {\n", indent, " ", confctl_var_name(cv));
		for (child = confctl_var_first(cv); child != NULL; child = confctl_var_next(child))
			confctl_var_print_c(child, indent + 8);
		printf("%*s}\n", indent, " ");
	} else {
		printf("%*s%s=%s\n", indent, " ", confctl_var_name(cv), confctl_var_value(cv));
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
			confctl_var_print_lines(child, prefix);
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
