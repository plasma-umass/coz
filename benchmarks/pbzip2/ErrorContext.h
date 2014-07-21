/* 
 * File:   ErrorContext.h
 * Author: Yavor Nikolov
 *
 * Created on 29 October 2011, 18:14
 */

#ifndef ERRORCONTEXT_H
#define	ERRORCONTEXT_H

#include "pbzip2.h"

namespace pbzip2
{

class ErrorContext
{
private:
	static ErrorContext * _instance;
	static pthread_mutex_t _err_ctx_mutex;	
	
	int _first_kernel_err_no;
	int _last_kernel_err_no;

private:
	ErrorContext():
		_first_kernel_err_no( 0 ),
		_last_kernel_err_no( 0 )
	{
	}
	
	ErrorContext( ErrorContext const & s );
	void operator=( ErrorContext const & s );
public:
	static ErrorContext * getInstance();
	void printErrorMessages( FILE * out = stderr );
	void saveError();
	void reset();
	
	static void printErrnoMsg( FILE * out, int err );
	static void syncPrintErrnoMsg( FILE * out, int err );
};

} // namespace pbzip2

#endif	/* ERRORCONTEXT_H */

