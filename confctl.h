#ifndef CONFCTL_H
#define	CONFCTL_H

#include <stdio.h>

struct confctl;

struct confctl	*confctl_load(const char *path);
void		confctl_save(struct confctl *cc, const char *path);
void		confctl_print_c(struct confctl *confctl, FILE *fp);
void		confctl_print_lines(struct confctl *confctl, FILE *fp);
void		confctl_parse_line(struct confctl *confctl, const char *line);
void		confctl_filter_line(struct confctl *confctl, const char *line);

#endif /* !CONFCTL_H */
