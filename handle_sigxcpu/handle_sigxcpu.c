/*
   +----------------------------------------------------------------------+
   | All rights reserved                                                  |
   |                                                                      |
   | Redistribution and use in source and binary forms, with or without   |
   | modification, are permitted provided that the following conditions   |
   | are met:                                                             |
   |                                                                      |
   | 1. Redistributions of source code must retain the above copyright    |
   |    notice, this list of conditions and the following disclaimer.     |
   | 2. Redistributions in binary form must reproduce the above copyright |
   |    notice, this list of conditions and the following disclaimer in   |
   |    the documentation and/or other materials provided with the        |
   |    distribution.                                                     |
   | 3. The names of the authors may not be used to endorse or promote    |
   |    products derived from this software without specific prior        |
   |    written permission.                                               |
   |                                                                      |
   | THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS  |
   | "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT    |
   | LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS    |
   | FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE       |
   | COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,  |
   | INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, |
   | BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;     |
   | LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER     |
   | CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT   |
   | LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN    | 
   | ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE      |
   | POSSIBILITY OF SUCH DAMAGE.                                          |
   +----------------------------------------------------------------------+
   | Authors: Hiroya Ito <hiroyan@gmail.com>                              |
   +----------------------------------------------------------------------+
*/

/* $ Id: $ */

#include <stdio.h>
#include <signal.h>
#include <locale.h>
#include <string.h>
#include <stdarg.h>
#include "php_handle_sigxcpu.h"

#if HAVE_HANDLE_SIGXCPU

ZEND_EXTERN_MODULE_GLOBALS(handle_sigxcpu)

static void php_handle_sigxcpu_init_globals(zend_handle_sigxcpu_globals *globals)
{
	//handle_sigxcpu_globals.enabled = 1;
}

/* {{{ handle_sigxcpu_functions[] */
function_entry handle_sigxcpu_functions[] = {
	{ NULL, NULL, NULL }
};
/* }}} */

/* {{{ handle_sigxcpu_module_entry
 */
zend_module_entry handle_sigxcpu_module_entry = {
	STANDARD_MODULE_HEADER,
	"handle_sigxcpu",
	handle_sigxcpu_functions,
	PHP_MINIT(handle_sigxcpu),     /* Replace with NULL if there is nothing to do at php startup   */ 
	PHP_MSHUTDOWN(handle_sigxcpu), /* Replace with NULL if there is nothing to do at php shutdown  */
	PHP_RINIT(handle_sigxcpu),     /* Replace with NULL if there is nothing to do at request start */
	PHP_RSHUTDOWN(handle_sigxcpu), /* Replace with NULL if there is nothing to do at request end   */
	PHP_MINFO(handle_sigxcpu),
	"0.0.1", 
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_HANDLE_SIGXCPU
ZEND_GET_MODULE(handle_sigxcpu)
#endif

#define DATE_FORMAT_LEN 128
#define DATE_FORMAT "%a %b %d %T %Y"

static int num_called = 0;
void apache_errorlog(const char * format, ...)
{
	char date[DATE_FORMAT_LEN];
	struct tm *ptime;
	time_t t;
	va_list ap;

	va_start(ap ,format);
	t = time(NULL);
	if(!setlocale(LC_TIME, "C")) {
		perror("failed setlocale():");
		return;
	}
	ptime = localtime(&t);
	if(!ptime) {
		perror("failed localtime():");
		return;
	}
	if(!strftime(date, DATE_FORMAT_LEN, DATE_FORMAT, ptime)) {
		perror("failed strftime():");
		return;
	}
	fprintf(stderr, "[%s] [info] ", date);
	vfprintf(stderr, format, ap);
	va_end(ap);
	setlocale(LC_TIME, "");
}

static int (*saved_zend_printf)(const char *format, ...);
static int (*saved_zend_write)(const char *str, uint str_length);

static int printf_stderr(const char *format, ...)
{
	int retval;
	va_list ap;

	va_start(ap ,format);
	retval = vfprintf(stderr, format, ap);
	va_end(ap);
	return retval;
}

static int write_stderr(const char *str, uint str_length)
{
	return fwrite(str, str_length, 1, stderr);
}

static void swap_zend_write_func(void)
{
	static bool swaped = false;

	if(!swaped) {
		saved_zend_printf = zend_printf;
		saved_zend_write  = zend_write;
		zend_printf = printf_stderr;
		zend_write  = write_stderr;
		swaped = true;
	} else {
		zend_printf = saved_zend_printf;
		zend_write  = saved_zend_write;
		swaped = false;
	}
}

static void sigaction_sigxcpu(int signum, siginfo_t *info, void *data)
{
	const char *server_name = getenv("SERVER_NAME");
	const char *remote_host = getenv("REMOTE_HOST");
	
	if(!remote_host)
			remote_host = getenv("REMOTE_ADDR");

	if(handle_sigxcpu_globals.print_backtrace) {
		/* with debug_print_backtrace() */
		apache_errorlog("%d received SIGXCPU(%d) %s %s\n",
						info->si_pid,
						num_called++,
						(server_name ? server_name : "-"),
						(remote_host ? remote_host : "-"));
		
		/* dirty hack */
		swap_zend_write_func();
		zend_eval_string("debug_print_backtrace();",
						 NULL,
						 "debug_print_backtrace();");
		swap_zend_write_func();
	}
	else {
		/* without debug_print_backtrace() */
		apache_errorlog("%d received SIGXCPU(%d) %s at %s:%d %s %s\n",
						info->si_pid,
						num_called++,
						get_active_function_name(TSRMLS_C),
						zend_get_executed_filename(TSRMLS_C),
						zend_get_executed_lineno(TSRMLS_C),
						(server_name ? server_name : "-"),
						(remote_host ? remote_host : "-"));
	}
}

static void setup_sigaction(void)
{
	struct sigaction sig;

	memset(&sig, 0, sizeof(sig));
	sig.sa_sigaction = sigaction_sigxcpu;
	sig.sa_flags = SA_SIGINFO;
	if(0 != sigaction(SIGXCPU, &sig, NULL)) {
		php_error(E_WARNING, "oops, failed set sigaction()");
	}
}

/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION(handle_sigxcpu)
{
	ZEND_INIT_MODULE_GLOBALS(handle_sigxcpu,
							php_handle_sigxcpu_init_globals, NULL);
	REGISTER_INI_ENTRIES();

	/* add your stuff here */
	setup_sigaction();
	
	return SUCCESS;
}
/* }}} */


/* {{{ PHP_MSHUTDOWN_FUNCTION */
PHP_MSHUTDOWN_FUNCTION(handle_sigxcpu)
{
	/* add your stuff here */

	return SUCCESS;
}
/* }}} */


/* {{{ PHP_RINIT_FUNCTION */
PHP_RINIT_FUNCTION(handle_sigxcpu)
{
	/* add your stuff here */

	return SUCCESS;
}
/* }}} */


/* {{{ PHP_RSHUTDOWN_FUNCTION */
PHP_RSHUTDOWN_FUNCTION(handle_sigxcpu)
{
	/* add your stuff here */

	return SUCCESS;
}
/* }}} */


/* {{{ PHP_MINFO_FUNCTION */
PHP_MINFO_FUNCTION(handle_sigxcpu)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "Handle SIGXCPU", "enabled");
	php_info_print_table_row(2, "Version", "0.0.1alpha");
	php_info_print_table_end();
	DISPLAY_INI_ENTRIES();
	
/* 	php_info_print_box_start(0); */
/* 	php_info_print_box_end(); */
	/* add your stuff here */

}
/* }}} */

#endif /* HAVE_HANDLE_SIGXCPU */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
