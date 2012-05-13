#include <assert.h>
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
	fprintf(stderr, "usage: confctl [-c] [-w name=value] config-path [name]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int ch;
	bool cflag = false, aflag = false;
	char *wflag = NULL;
	struct confctl *cc;

	while ((ch = getopt(argc, argv, "acw:")) != -1) {
		switch (ch) {
			case 'a':
				aflag = true;
				break;
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

	if (argc < 1)
		errx(1, "missing config file path");
	if (argc > 2)
		usage();
	if (aflag && wflag)
		errx(1, "-a and -w are mutually exclusive");
	if (cflag && wflag)
		errx(1, "-c and -w are mutually exclusive");
	if (aflag && argc > 1)
		errx(1, "-a and variable names are mutually exclusive");
	if (!aflag && argc == 1)
		errx(1, "neither -a or variable names specified");

	cc = confctl_load(argv[0]);
	if (wflag == NULL) {
		if (!aflag) {
			assert(argv[1] != NULL);
			confctl_filter_line(cc, argv[1]);
		}
		if (cflag)
			confctl_print_c(cc, stdout);
		else
			confctl_print_lines(cc, stdout);
	} else {
		confctl_parse_line(cc, wflag);
		confctl_save(cc, argv[0]);
	}

	return (0);
}
