#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_INIT([confctl], [1.2], [trasz@FreeBSD.org], [confctl], [http://github.com/trasz/confctl])
AM_INIT_AUTOMAKE([foreign])

AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([err.h])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
