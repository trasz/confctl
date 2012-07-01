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
	fprintf(stderr, "usage: confctl [-n] config-path [name...]\n");
	fprintf(stderr, "       confctl [-an] config-path\n");
	fprintf(stderr, "       confctl [-I] -w name=value config-path\n");
	fprintf(stderr, "       confctl [-I] -x name config-path\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int ch, i;
	bool aflag = false, Iflag = false, nflag = false;
	struct confctl *cc;
	struct confctl_var *root, *cv, *filter = NULL, *merge = NULL, *remove = NULL;

	if (argc <= 1)
		usage();

	while ((ch = getopt(argc, argv, "aInw:x:")) != -1) {
		switch (ch) {
		case 'a':
			aflag = true;
			break;
		case 'I':
			Iflag = true;
			break;
		case 'n':
			nflag = true;
			break;
		case 'w':
			cv = confctl_var_from_line(optarg);
			confctl_var_merge(&merge, cv);
			break;
		case 'x':
			cv = confctl_var_from_line(optarg);
			confctl_var_merge(&remove, cv);
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
	if (remove && argc > 1)
		errx(1, "-x and variable names are mutually exclusive");
	if (aflag && merge)
		errx(1, "-a and -w are mutually exclusive");
	if (aflag && remove)
		errx(1, "-a and -x are mutually exclusive");
	if (nflag && merge)
		errx(1, "-n and -w are mutually exclusive");
	if (nflag && merge)
		errx(1, "-n and -x are mutually exclusive");
	if (aflag && argc > 1)
		errx(1, "-a and variable names are mutually exclusive");
	if (!aflag && !merge && !remove && argc == 1)
		errx(1, "neither -a, -w, -x, or variable names specified");

	cc = confctl_init(Iflag);
	confctl_load(cc, argv[0]);
	root = confctl_root(cc);
	if (merge == NULL && remove == NULL) {
		if (!aflag) {
			for (i = 1; i < argc; i++) {
				cv = confctl_var_from_line(argv[i]);
				confctl_var_merge(&filter, cv);
			}
			confctl_var_filter(root, filter);
		}
		confctl_print_lines(cc, stdout, nflag);
	} else {
		/*
		 * We're not using confctl_var_filter() mechanism,
		 * because we really want to remove the nodes here,
		 * so that we can e.g. replace them by using -x
		 * and -w together.  Also, confctl_var_filter() works
		 * the other way around, exposing selected nodes
		 * and hiding all the rest; we would need to 'invert'
		 * the filter somehow.
		 */
		if (remove != NULL)
			confctl_var_remove(root, remove);
		if (merge != NULL)
			confctl_var_merge(&root, merge);
		confctl_save(cc, argv[0]);
	}

	return (0);
}
