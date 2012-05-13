#ifndef CONFCTL_PRIVATE_H
#define	CONFCTL_PRIVATE_H

struct buf {
	char	*b_buf;
	size_t	b_allocated;
	size_t	b_len;
};

struct confctl_var {
	struct confctl_var	*cv_next;
	struct confctl_var	*cv_parent;
	struct buf		*cv_name;
	struct buf		*cv_value;
	struct confctl_var	*cv_first;
	struct confctl_var	*cv_last;
};

struct confctl {
	struct confctl_var	*cc_first;
};

#endif /* !CONFCTL_PRIVATE_H */
