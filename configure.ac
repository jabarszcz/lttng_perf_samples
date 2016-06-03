AC_PREREQ([2.69])
AC_INIT([lttng-perf-samples], [0.0.0], [jalex_b at hotmail dot com])

AC_CONFIG_SRCDIR([src/perf.h])
AC_CONFIG_HEADERS([src/autoconf.h])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([config])

AM_INIT_AUTOMAKE([foreign -Wall -Werror -Wno-extra-portability])

# Checks for programs.
AC_PROG_CC
AC_PROG_MAKE_SET

# Checks for libraries.
PKG_CHECK_MODULES(XML, libxml-2.0 >= 2.4)

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdlib.h sys/ioctl.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_PID_T

# Checks for library functions.
AC_FUNC_MALLOC

AC_CONFIG_FILES([Makefile src/Makefile])

LT_INIT
AC_OUTPUT
