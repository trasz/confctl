#ifndef CONFCTL_PRIVATE_H
#define	CONFCTL_PRIVATE_H

#include <sys/queue.h>

struct buf {
	char	*b_buf;
	size_t	b_allocated;
	size_t	b_len;
};

struct confctl_var {
	TAILQ_ENTRY(confctl_var)	cv_next;
	struct buf			*cv_name;
	struct buf			*cv_value;
	struct confctl_var		*cv_parent;
	TAILQ_HEAD(, confctl_var)	cv_vars;
};

struct confctl {
	struct confctl_var		*cc_root;
};

#endif /* !CONFCTL_PRIVATE_H */
