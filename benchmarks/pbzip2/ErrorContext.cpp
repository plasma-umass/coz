/* 
 * File:   ErrorContext.cpp
 * Author: Yavor Nikolov
 * 
 * Created on 29 October 2011, 18:14
 */

#include "pbzip2.h"
#include "ErrorContext.h"

#include <cstring>
#include <cerrno>

namespace pbzip2
{

ErrorContext * ErrorContext::_instance = 0;
pthread_mutex_t ErrorContext::_err_ctx_mutex = PTHREAD_MUTEX_INITIALIZER;

ErrorContext * ErrorContext::getInstance()
{
	pthread_mutex_lock( &_err_ctx_mutex );

	if ( ErrorContext::_instance == NULL )
	{
		_instance = new(std::nothrow) ErrorContext;

		if ( _instance == NULL )
		{
			fprintf( stderr, "pbzip2: *ERROR: Can't initialize error context - out of memory!\n" );
		}
	}

	pthread_mutex_unlock( &_err_ctx_mutex );

	return _instance;
}

void ErrorContext::printErrnoMsg( FILE * out, int err )
{
	if ( err != 0 )
	{
		fprintf( out, "pbzip2: *ERROR: system call failed with errno=[%d: %s]!\n",
			err, std::strerror( err ) );
	}
}

void ErrorContext::syncPrintErrnoMsg( FILE * out, int err )
{
	pthread_mutex_lock( &_err_ctx_mutex );
	printErrnoMsg( out, err );
	pthread_mutex_unlock( &_err_ctx_mutex );
}

void ErrorContext::printErrorMessages( FILE * out )
{
	pthread_mutex_lock( &_err_ctx_mutex );
	
	printErrnoMsg( out, _first_kernel_err_no );
	printErrnoMsg( out, _last_kernel_err_no );
	
	pthread_mutex_unlock( &_err_ctx_mutex );
}

void ErrorContext::saveError()
{
	int newerr = errno;
	
	pthread_mutex_lock( &_err_ctx_mutex );
	
	if ( newerr != 0 )
	{
		int & err_ref = ( _first_kernel_err_no == 0 )
						? _first_kernel_err_no
						: _last_kernel_err_no;
		
		err_ref = newerr;
	}

	_last_kernel_err_no = errno;

	pthread_mutex_unlock( &_err_ctx_mutex );
}

void ErrorContext::reset()
{
	pthread_mutex_lock( &_err_ctx_mutex );
	_first_kernel_err_no = _last_kernel_err_no = 0;
	pthread_mutex_unlock( &_err_ctx_mutex );
}

} // namespace pbzip2
