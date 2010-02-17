dnl
dnl $ Id: $
dnl

PHP_ARG_ENABLE(handle_sigxcpu, whether to enable handle_sigxcpu functions,
[  --enable-handle_sigxcpu         Enable handle_sigxcpu support])

if test "$PHP_HANDLE_SIGXCPU" != "no"; then
  export OLD_CPPFLAGS="$CPPFLAGS"
  export CPPFLAGS="$CPPFLAGS $INCLUDES -DHAVE_HANDLE_SIGXCPU"

  AC_MSG_CHECKING(PHP version)
  AC_TRY_COMPILE([#include <php_version.h>], [
#if PHP_VERSION_ID < 40000
#error  this extension requires at least PHP version 4.0.0
#endif
],
[AC_MSG_RESULT(ok)],
[AC_MSG_ERROR([need at least PHP 4.0.0])])

  export CPPFLAGS="$OLD_CPPFLAGS"


  PHP_SUBST(HANDLE_SIGXCPU_SHARED_LIBADD)
  AC_DEFINE(HAVE_HANDLE_SIGXCPU, 1, [ ])

  PHP_NEW_EXTENSION(handle_sigxcpu, handle_sigxcpu.c , $ext_shared)

fi

