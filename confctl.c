#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "confctl.h"

static void
usage(void)
{
	fprintf(stderr, "usage: confctl [-c] [-w name=value] config-path\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int ch;
	bool cflag = false;
	char *wflag = NULL;
	struct confctl *cc;

	while ((ch = getopt(argc, argv, "cw:")) != -1) {
		switch (ch) {
			case 'c':
				cflag = true;
				break;
			case 'w':
				wflag = strdup(optarg);
				if (wflag == NULL)
					err(1, "strdup");
				break;
			case '?':
			default:
				usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		errx(1, "missing config file path");

	cc = confctl_load(argv[0]);
	if (wflag == NULL) {
		if (cflag)
			confctl_print_c(cc, stdout);
		else
			confctl_print_lines(cc, stdout);
	} else {
		/*
		 * XXX: Modify stuff.
		 */
		confctl_save(cc, argv[0]);
	}

	return (0);
}
