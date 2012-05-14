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
#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "confvar.h"

static void
usage(void)
{
	fprintf(stderr, "usage: confctl [-cn] config-path [name...]\n");
	fprintf(stderr, "       confctl [-acn] config-path\n");
	fprintf(stderr, "       confctl -w name=value config-path\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int ch, i;
	bool cflag = false, aflag = false, nflag = false;
	struct confvar *root, *cv, *filter = NULL, *merge = NULL;

	while ((ch = getopt(argc, argv, "acnw:")) != -1) {
		switch (ch) {
			case 'a':
				aflag = true;
				break;
			case 'c':
				cflag = true;
				break;
			case 'n':
				nflag = true;
				break;
			case 'w':
				cv = confvar_from_line(optarg);
				confvar_merge(&merge, cv);
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
	if (merge && argc > 1)
		errx(1, "-w and variable names are mutually exclusive");
	if (aflag && merge)
		errx(1, "-a and -w are mutually exclusive");
	if (cflag && merge)
		errx(1, "-c and -w are mutually exclusive");
	if (nflag && merge)
		errx(1, "-n and -w are mutually exclusive");
	if (nflag && cflag)
		errx(1, "-n and -c are mutually exclusive");
	if (aflag && argc > 1)
		errx(1, "-a and variable names are mutually exclusive");
	if (!aflag && !merge && argc == 1)
		errx(1, "neither -a or variable names specified");

	root = confvar_load(argv[0]);
	if (merge == NULL) {
		if (!aflag) {
			for (i = 1; i < argc; i++) {
				cv = confvar_from_line(argv[i]);
				confvar_merge(&filter, cv);
			}
			confvar_filter(root, filter);
		}
		if (cflag)
			confvar_print_c(root, stdout);
		else
			confvar_print_lines(root, stdout, nflag);
	} else {
		confvar_merge(&root, merge);
#if 0
		confvar_save(root, argv[0]);
#else
		confvar_print_c(root, stdout);
#endif
	}

	return (0);
}
