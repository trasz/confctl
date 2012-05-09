#ifndef CONFCTL_H
#define	CONFCTL_H

struct confctl;

struct confctl	*confctl_load(const char *path);
void		confctl_print_c(struct confctl *confctl);
void		confctl_print_lines(struct confctl *confctl);

#endif /* !CONFCTL_H */
