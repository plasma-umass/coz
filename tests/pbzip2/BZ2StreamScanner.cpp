/* 
 * File:   BZ2StreamScanner.cpp
 * Author: Yavor Nikolov
 * 
 * Created on March 6, 2010, 10:07 PM
 */

#include "pbzip2.h"
#include "BZ2StreamScanner.h"

#include <algorithm>
#include <new>
#include <exception>
#include <limits>

#include <cstring>
#include <cstdio>

using namespace std;

namespace pbzip2
{

const size_t BZ2StreamScanner::DEFAULT_OUT_BUFF_LIMIT;

BZ2StreamScanner::BZ2StreamScanner( int hInFile, size_t inBuffCapacity ):
	_inBuff( NULL ),
	_inBuffCapacity( 0 )
{
	_outBuff.buf = NULL;
	_outBuff.bufSize = 0;
	
	init( hInFile, inBuffCapacity );
}

/**
 * Initialize - position to beginning of input file and prepare for searching.
 *
 * @return 0 - on success; -1 on error.
 */
int BZ2StreamScanner::init( int hInFile, size_t inBuffCapacity )
{
	dispose();

	CharType bz2header[] = "BZh91AY&SY";
	// zero-terminated string
	CharType bz2ZeroHeader[] =
		{ 'B', 'Z', 'h', '9', 0x17, 0x72, 0x45, 0x38, 0x50, 0x90, 0 };

	_hInFile = hInFile;
	_eof = false;
	_bz2Header = bz2header;
	_bz2HeaderZero = bz2ZeroHeader;
	_bz2HeaderFound = false;
	_inBuffCapacity = 0;
	_errState = 0;
	_searchStatus = false;
	_outBuffCapacityHint = 0;
	_outBuffCapacityLimit = DEFAULT_OUT_BUFF_LIMIT;
	_outSequenceNumber = 0;
	_streamNumber = 0;

	// Prevent too small buffer
	if ( inBuffCapacity < 2 * _bz2Header.size() )
	{
		inBuffCapacity = 2 * _bz2Header.size();
	}

	// allocate memory to read in file
	_inBuff = new(std::nothrow) CharType[inBuffCapacity];

	if ( _inBuff == NULL )
	{
		_errState |= ERR_MEM_ALLOC_INBUFF;
		_inBuffEnd = NULL;
		handle_error( EF_EXIT, -1,
				"pbzip2: *ERROR: Could not allocate memory (FileData)!  Aborting...\n" );

		return -1;
	}

	_inBuffCapacity = inBuffCapacity;

	_inBuffCurrent = _inBuffSearchPtr = _inBuffEnd = _inBuff;

	return 0;
}

/**
 * dispose memory resources
 */
void BZ2StreamScanner::dispose()
{
	disposeMemory( _outBuff.buf );
	_outBuff.bufSize = 0;

	disposeMemory( _inBuff );
	_inBuffCapacity = 0;
	
	// close( _hInFile );
}

BZ2StreamScanner::~BZ2StreamScanner()
{
	dispose();
}

/**
 * Verify if there is enough space in output buffer. If not - then allocate.
 */
int BZ2StreamScanner::ensureOutBuffCapacity( size_t newSize )
{
	#ifdef PBZIP_DEBUG
	fprintf( stderr, " start ensureOutBuffCapacity/newSize=%u: [", newSize );
	printCurrentState();
	fprintf( stderr, "\n" );
	#endif

	if ( newSize <= _outBuffCapacity )
	{
		return 0; // enough capacity already
	}

	if ( newSize > _outBuffCapacityHint )
	{
		_outBuffCapacityHint = ( 11 * newSize ) / 10;
		
		if ( ( newSize <= getOutBuffCapacityLimit() ) &&
			( _outBuffCapacityHint > getOutBuffCapacityLimit() ) )
		{
			_outBuffCapacityHint = getOutBuffCapacityLimit();
		}
	}

	char * newBuff = new(std::nothrow) char[_outBuffCapacityHint];
	if ( newBuff == NULL )
	{
		handle_error( EF_EXIT, -1,
			"pbzip2: *ERROR: Could not allocate memory (ensureOutBuffCapacity/%u)!"
			"Aborting...\n", _outBuffCapacityHint );

		_errState |= ERR_MEM_ALLOC_OUTBUFF;
		return -1;
	}

	if ( _outBuff.buf != NULL )
	{
		memcpy( newBuff, _outBuff.buf, _outBuff.bufSize );
		delete [] _outBuff.buf;
	}

	initOutBuff( newBuff, _outBuff.bufSize, _outBuffCapacityHint );

	#ifdef PBZIP_DEBUG
	fprintf( stderr, " end ensureOutBuffCapacity/newSize=%u: [", newSize );
	printCurrentState();
	fprintf( stderr, "\n" );
	#endif

	return 0;
}

/**
 * Depending on wether we have already found bz2 header or not - either append
 * data to output buffer or discard it.
 * On append [current, end) is appended to output buffer. Output buffer is
 * extended if there is not enough existing space available in it.
 *
 * @return the number of bytes appended to output buff or skipped. -1 on error.
 */
int BZ2StreamScanner::appendOutBuffData(CharType * end)
{
	int additionSize = end - getInBuffCurrent();

	#ifdef PBZIP_DEBUG
	fprintf( stderr, " start appendOutBuffData/%d: [", additionSize );
	printCurrentState();
	fprintf( stderr, "\n" );
	#endif

	if ( additionSize <= 0 )
	{
		return 0;
	}

	if ( isBz2HeaderFound() )
	{	
		size_t newSize = _outBuff.bufSize + additionSize;

		if ( ensureOutBuffCapacity( newSize ) != 0 )
		{
			return - 1; // failure encountered
		}

		memcpy( getOutBuffEnd(), getInBuffCurrent(), additionSize );
		_outBuff.bufSize += additionSize;
	}

	// slide current position
	_inBuffCurrent = end;

	#ifdef PBZIP_DEBUG
	fprintf( stderr, " end appendOutBuffData/%d: [", additionSize );
	printCurrentState();
	fprintf( stderr, "\n" );
	#endif

	return additionSize;
}

/**
 * Append available data from [current, search pos) to output buffer but
 * just up to fill current out buffer capacity
 */
int BZ2StreamScanner::appendOutBuffDataUpToLimit()
{
	size_t maxCapacity = std::max( getOutBuffCapacityLimit(), _outBuffCapacity );
	int maxAddition = maxCapacity - _outBuff.bufSize;
	if (maxAddition <= 0 )
	{
		return 0;
	}

	CharType * end1;
	if ( eof() )
	{
		end1 = getInBuffEnd();
	}
	else
	{
		// subtract header size to keep the tail (since start of next header may be in it)
		end1 = std::min( getInBuffSearchPtr(), getInBuffEnd() - ( getHeaderSize() - 1 ) );
	}
	CharType * end2 = getInBuffCurrent() + maxAddition;
	CharType * end = std::min( end1, end2 );

	return appendOutBuffData( end );
}

/**
 * Copy end section of input buffer to beginning just in case the BZIP2 header
 * is located between two buffer boundaries. Copy the other remaining
 * data into output buffer.
 */
int BZ2StreamScanner::rewindInBuff()
{	
	// temporarily mark tail beginning (not real header position)
	_inBuffSearchPtr = getInBuffEnd() - ( _bz2Header.size() - 1 );
	int ret = appendOutBuffData();
	if ( failed() || ( ret < 0 ) )
	{
		return -1;
	}
	else if ( ret == 0 )
	{
		// search pos <= current
		_inBuffSearchPtr = getInBuffCurrent();
	}

	int tailSize = getInBuffEnd() - getInBuffSearchPtr();

	#ifdef PBZIP_DEBUG
	fprintf( stderr, " rewindInBuff: tail len: %d; app ret=%d [", tailSize, ret );
	printCurrentState();
	fprintf( stderr, "\n" );
	#endif

	// copy tail of input buffer to start and cut the rest
	std::copy( getInBuffSearchPtr(), getInBuffEnd(), getInBuffBegin() );
	_inBuffEnd = getInBuffBegin() + tailSize;
	_inBuffCurrent = getInBuffBegin();
	_inBuffSearchPtr = getInBuffBegin();

	#ifdef PBZIP_DEBUG
	fprintf( stderr, " end rewindInBuff: tail len: %d; app ret=%d [", tailSize, ret );
	printCurrentState();
	fprintf( stderr, "\n" );
	#endif

	return 0;
}

/**
 * Load data from file to input buffer. Read untill buffer is full or end of
 * file is reached or error is encountered.
 *
 * Enough additional capacity should be ensured otherwise may return 0 before
 * eof.
 *
 * @return Returns number of read bytes on success; 0 - on end of file; < 0 on error
 */
int BZ2StreamScanner::readData()
{
	rewindInBuff();
	if ( failed() )
	{
		return -1;
	}

	if ( getInBuffSize() >= getInBuffCapacity() )
	{
		handle_error( EF_EXIT, -1,
			"pbzip2: *ERROR: BZ2StreamScanner::readData not enough buffer free space!"
			" inBuffSize=%u, _inBuffCapacity=%u! Aborting...\n",
					 getInBuffSize(), getInBuffCapacity() );

		_errState |= ERR_IO_INSUFFICIENT_BUFF_CAPACITY;
		return -1;
	}

	int bytesRead = do_read( _hInFile, getInBuffEnd(),
						 getInBuffCapacity() - getInBuffSize() );

	#ifdef PBZIP_DEBUG
	fprintf( stderr, " readData: %d bytes read\n", bytesRead );
	#endif

	if ( bytesRead > 0 )
	{
		_inBuffEnd += bytesRead;
	}
	else if ( bytesRead < 0 )
	{
		handle_error( EF_EXIT, -1,
			"pbzip2: *ERROR: Could not read from input file [err=%d]! Aborting...\n", bytesRead );
		
		_errState |= ERR_IO_READ;
		return bytesRead;
	}
	else // ( bytesRead == 0 )
	{
		_eof = true;
	}

	return bytesRead;
}

/**
 * Locate BZh header prefix in buffer. In case of first search - just check
 * the beginning of buffer and signal error if it doesn't match to headers.
 *
 * @return pointer to BZh header prefix if located. getInBuffEnd() if not.
 *         failure() and getErrState() will indicate error if such occurred.
 */
BZ2StreamScanner::CharType * BZ2StreamScanner::locateHeaderPrefixInBuff()
{
	size_t prefixLen = 3;

	#ifdef PBZIP_DEBUG
	fprintf( stderr, " start locateHeaderPrefixInBuff; " );
	printCurrentState();
	fprintf( stderr, "\n" );
	#endif

	// first search
	if ( !isBz2HeaderFound() )
	{
		if ( ( getInBuffSearchPtr() != getInBuffBegin() ) ||
			( getInBuffSize() < _bz2Header.size() ) )
		{
			_errState |= ERR_INVALID_FILE_FORMAT;
			_inBuffSearchPtr = getInBuffEnd();
		}
		else if ( _bz2Header.compare( 0, prefixLen, getInBuffSearchPtr(), prefixLen ) == 0 )
		{
			// header prefix found
		}
		else
		{
			_errState |= ERR_INVALID_FILE_FORMAT;
			_inBuffSearchPtr = getInBuffEnd();
		}
	}
	else
	{
		_inBuffSearchPtr = std::search( getInBuffSearchPtr(), getInBuffEnd(),
				_bz2Header.begin(), _bz2Header.begin() + prefixLen );
	}

	#ifdef PBZIP_DEBUG
	if ( getInBuffSearchPtr() != getInBuffEnd() )
	{
		fprintf( stderr, " end locateHeaderPrefixInBuff - header prefix found; " );
	}
	else
	{
		fprintf( stderr, " end locateHeaderPrefixInBuff - header prefix not found; " );
	}
	printCurrentState();
	fprintf( stderr, "\n" );
	#endif
	
	return getInBuffSearchPtr();
}


/**
 * Search next bz2 header just in currently available input buffer.
 * (Doesn't read more data from file).
 *
 * @return pointer to header or getInBuffEnd() if such is not found.
 */
BZ2StreamScanner::CharType * BZ2StreamScanner::searchNextHeaderInBuff()
{
	#ifdef PBZIP_DEBUG
	fprintf( stderr, "  start searchNextHeaderInBuff; " );
	printCurrentState();
	fprintf( stderr, "\n" );
	#endif

	_searchStatus = false;
	size_t prefixLen = 3;
	size_t hsp = prefixLen + 1; // header selection position

	locateHeaderPrefixInBuff();
	while ( !failed() && ( getUnsearchedCount() >= getHeaderSize() ) )
	{
		// _inBuffSearchPtr += prefixLen;
		basic_string<CharType> * pHdr = NULL;

		if ( getInBuffSearchPtr()[hsp] == _bz2Header[hsp] )
		{
			pHdr = &_bz2Header;
			#ifdef PBZIP_DEBUG
			fprintf( stderr, "   searchNextHeaderInBuff - kind of NON-ZERO header\n" );
			#endif
		}
		else if ( getInBuffSearchPtr()[hsp] == _bz2HeaderZero[hsp] )
		{
			pHdr = &_bz2HeaderZero;
			#ifdef PBZIP_DEBUG
			fprintf( stderr, "   searchNextHeaderInBuff - kind of ZERO header\n" );
			#endif
		}

		if ( pHdr != NULL )
		{
			CharType bwtSizeChar = getInBuffSearchPtr()[prefixLen];
			if ( ( bwtSizeChar >= '1' ) && ( bwtSizeChar <= '9' ) )
			{
				(*pHdr)[prefixLen] = bwtSizeChar;

				// compare the remaining part of magic header
				int cmpres = pHdr->compare( hsp, pHdr->size() - hsp,
						getInBuffSearchPtr() + hsp, pHdr->size() - hsp );

				#ifdef PBZIP_DEBUG
				fprintf( stderr, "   searchNextHeaderInBuff:cmpres=%d\n", cmpres );
				#endif
				if ( cmpres == 0 )
				{
					_searchStatus = true;
					#ifdef PBZIP_DEBUG
					fprintf( stderr, " end searchNextHeaderInBuff - found; " );
					printCurrentState();
					fprintf( stderr, "\n" );
					#endif
					return _inBuffSearchPtr;
				}
			}
		}

		if ( !isBz2HeaderFound() )
		{
			// not finding header on first search means failure
			_errState |= ERR_INVALID_FILE_FORMAT;
			break;
		}
		else
		{
			_inBuffSearchPtr += prefixLen;
			locateHeaderPrefixInBuff();
		}
	}

	// no header has been found if we're here
	_inBuffSearchPtr = getInBuffEnd();

	#ifdef PBZIP_DEBUG
	fprintf( stderr, " end searchNextHeaderInBuff; " );
	printCurrentState();
	fprintf( stderr, "\n" );
	#endif
	
	return _inBuffSearchPtr;
}


void BZ2StreamScanner::printCurrentState()
{
	fprintf( stderr, "current=%d, search pos=%d, end pos=%d; s-c=%d"
			"; out buf size=%d; out buf capacity=%d; header found=%d; search status=%d",
			getInBuffCurrent() - getInBuffBegin(),
			getInBuffSearchPtr() - getInBuffBegin(),
			getInBuffEnd() - getInBuffBegin(),
			getInBuffSearchPtr() - getInBuffCurrent(),
			(int)_outBuff.bufSize,
			(int)_outBuffCapacity,
			(int)isBz2HeaderFound(),
			(int)getSearchStatus() );
}

/**
 * Search next bz2 header. Read more data from file if needed.
 *
 * @return pointer to header is returned if found;
 *         getInBuffEnd() - if not found (or error).
 *         One should check failure() or _errorState for error details.
 */
BZ2StreamScanner::CharType * BZ2StreamScanner::searchNextHeader()
{
	#ifdef PBZIP_DEBUG
	fprintf( stderr, " start searchNextHeader %u/%u... : ",
		 getInBuffSearchPtr() - getInBuffBegin(), getInBuffSize() );
	printCurrentState();
	fprintf( stderr, "\n" );
	#endif

	if ( getUnsearchedCount() > 0 )
	{
		searchNextHeaderInBuff();
	}
	
	while ( !getSearchStatus() && !eof() && !failed() && !isOutBuffFullEnough() )
	{
		readData();
		
		if ( failed() )
		{
			return getInBuffEnd();
		}
		
		searchNextHeaderInBuff();
	}

	if ( getSearchStatus() )
	{
		_bz2HeaderFound = true;

		#ifdef PBZIP_DEBUG
		fprintf( stderr, " header found; " );
		printCurrentState();
		fprintf( stderr, "\n" );
		#endif
	}

	if ( failed() )
	{
		return _inBuffSearchPtr = getInBuffEnd();
	}

	#ifdef PBZIP_DEBUG
	fprintf( stderr, " end searchNextHeader %u/%u... NOT FOUND: ",
		 getInBuffSearchPtr() - getInBuffBegin(), getInBuffSize() );
	printCurrentState();
	fprintf( stderr, "\n" );
	#endif

	return _inBuffSearchPtr;
}

/**
 * Get next BZ2 stream from the input.
 *
 * @return output buffer initialized with bz2 stream. failure() should be checked
 *         after calling this method - true would mean failure(). If failure() is false:
 *  - outBuff.bufSize == 0 indicates end of file;
 */
outBuff * BZ2StreamScanner::getNextStream()
{
	initOutBuff();

	#ifdef PBZIP_DEBUG
	static OFF_T blockNum = 0;
	#endif

	outBuff * res = new(std::nothrow) outBuff;
	if ( res == NULL )
	{
		handle_error( EF_EXIT, -1,
			"pbzip2: *ERROR: Could not allocate memory (getNextStream/%u)!"
			"Aborting...\n", (unsigned) sizeof( outBuff ) );

		_errState |= ERR_MEM_ALLOC_OUTBUFF;
		return res;
	}

	res->buf = NULL;
	res->bufSize = std::numeric_limits<unsigned int>::max();

	// first search
	if ( !failed() && !isBz2HeaderFound() )
	{
		#ifdef PBZIP_DEBUG
		blockNum = 0;
		fprintf( stderr, " First search start\n" );
		#endif

		_searchStatus = false;
		searchNextHeader();
	}

	if ( failed() )
	{
		return res;
	}

	if ( ( getInBuffCurrent() == getInBuffEnd() ) && eof() )
	{
		// end of file
		#ifdef PBZIP_DEBUG
		fprintf( stderr, " End of file\n" );
		#endif

		res->bufSize = 0;
		return res;
	}
	
	if ( ( getInBuffCurrent() == getInBuffSearchPtr() ) ||
		( !getSearchStatus() && !eof() ) )
	{
		// search for next header
		// Slide a bit to skip current header in order to search for next one.
		_inBuffSearchPtr = std::min( getInBuffSearchPtr() + _bz2Header.size(),
								 getInBuffEnd() );
		_searchStatus = false;

		#ifdef PBZIP_DEBUG
		fprintf( stderr, " getNextStream - Searching subsequent header... " );
		printCurrentState();
		fprintf( stderr, "\n" );
		#endif
		
		searchNextHeader();
	}

	if ( failed() )
	{
		return res;
	}

	appendOutBuffDataUpToLimit();

	if ( failed() )
	{
		return res;
	}

	if ( _outSequenceNumber > 0 )
	{
		// continuing an unterminated sequence
		++_outSequenceNumber;
	}
	else if ( getInBuffCurrent() != getInBuffSearchPtr() )
	{
		// start of long multi-part stream
		_outSequenceNumber = 1;
	}
	
	_outBuff.sequenceNumber = _outSequenceNumber;
	_outBuff.inSize = _outBuff.bufSize;
	_outBuff.blockNumber = _streamNumber;

	if ( getInBuffCurrent() == getInBuffSearchPtr() )
	{
		// we're at end of stream (either single or multi-segment one)
		_outBuff.isLastInSequence = true;
		_outSequenceNumber = 0;
		++_streamNumber;
	}
	else
	{
		_outBuff.isLastInSequence = false;
	}


	#ifdef PBZIP_DEBUG
	OFF_T startBlock = blockNum;
	blockNum += _outBuff.bufSize;

	fprintf( stderr, " end getNextStream/blockRange=[%"PRIu64", %"PRIu64"), stream no=%d; seq=%d: [",
		 startBlock, blockNum, _outBuff.blockNumber, _outBuff.sequenceNumber );
	printCurrentState();
	fprintf( stderr, "\n" );
	#endif

	*res = _outBuff;
	// clean-up pointers to returned data.
	initOutBuff();

	return res;
}

void BZ2StreamScanner::initOutBuff( char * buf, size_t bufSize, size_t bufCapacity )
{
	_outBuff.buf = buf;
	_outBuff.bufSize = bufSize;
	_outBuffCapacity = bufCapacity;
	_outBuff.inSize = 0;
}

} // namespace pbzip2

