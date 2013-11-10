/* 
 * File:   BZ2StreamScanner.h
 * Author: Yavor Nikolov
 *
 * Created on March 6, 2010, 10:07 PM
 */

#ifndef _BZ2STREAMSCANNER_H
#define	_BZ2STREAMSCANNER_H

#include "pbzip2.h"
#include <vector>
#include <string>

using namespace std;

namespace pbzip2
{

class BZ2StreamScanner
{
public:
	typedef unsigned char CharType;

	static const size_t DEFAULT_IN_BUFF_CAPACITY = 1024 * 1024; // 1M
	static const size_t DEFAULT_OUT_BUFF_LIMIT = 1024 * 1024;

	enum BZ2SScannerErrorFlag
	{
		ERR_MEM_ALLOC_INBUFF = 1,
		ERR_MEM_ALLOC_OUTBUFF = 1 << 1,
		ERR_IO_READ = 1 << 2,
		ERR_IO_INSUFFICIENT_BUFF_CAPACITY = 1 << 3,
		ERR_INVALID_STATE = 1 << 4,
		ERR_INVALID_FILE_FORMAT = 1 << 5
	};

	BZ2StreamScanner( int hInFile, size_t inBuffCapacity = DEFAULT_IN_BUFF_CAPACITY );
	int init( int hInFile, size_t inBuffCapacity = DEFAULT_IN_BUFF_CAPACITY );
	
	virtual ~BZ2StreamScanner();

	outBuff * getNextStream();

	size_t getInBuffSize() const { return ( _inBuffEnd - _inBuff ); }
	size_t getInBuffCapacity() const { return _inBuffCapacity; }
	const basic_string<CharType> & getHeader() const { return _bz2Header; }
	size_t getHeaderSize() const { return _bz2Header.size(); }
	int getErrState() const { return _errState; }
	bool failed() { return ( _errState != 0 ); }

	/** true if header has been found since last initialization */
	bool isBz2HeaderFound() const { return _bz2HeaderFound; }

	/** status of last/current search only */
	bool getSearchStatus() const { return _searchStatus; }
	
	// end of file
	bool eof() const { return _eof; }

	/** true if out buffer is full enough to produce output block */
	bool isOutBuffFullEnough() const { return _outBuff.bufSize >= getOutBuffCapacityLimit(); }

	/**
	 * dispose memory resources
	 */
	virtual void dispose();

	void printCurrentState();
	
private:
	/* disable copy c-tor */
	BZ2StreamScanner( const BZ2StreamScanner& orig ) {}

	void initOutBuff( char * buf = NULL, size_t bufSize = 0, size_t bufCapacity = 0 );
	int appendOutBuffData( CharType * end );
	int appendOutBuffData() { return appendOutBuffData( getInBuffSearchPtr() ); }
	int appendOutBuffDataUpToLimit();
	int ensureOutBuffCapacity( size_t newSize );
	int readData();

	CharType * getInBuffEnd() { return _inBuffEnd; }
	CharType * getInBuffBegin() { return _inBuff; }
	CharType * getInBuffCurrent() { return _inBuffCurrent; }
	CharType * getInBuffSearchPtr() { return _inBuffSearchPtr; }
	char * getOutBuffEnd() { return _outBuff.buf + _outBuff.bufSize; }
	size_t getUnsearchedCount() const { return _inBuffEnd - _inBuffSearchPtr; }

	/**
	 * Search next bz2 header. Read more data from file if needed.
	 *
	 * @return pointer to header is returned if found;
	 *         getInBuffEnd() - if not found; NULL - on error.
	 */
	CharType * searchNextHeader();

	/**
	 * Search next bz2 header just in currently available input buffer.
	 * (Doesn't read more data from file).
	 *
	 * @return pointer to header or getInBuffEnd() if such is not found.
	 */
	CharType * searchNextHeaderInBuff();

	/**
	 * Prepare for next read from file into input buffer.
	 * Consumes remaining input data buffer and moves header tail to beginning.
	 * 
	 */
	int rewindInBuff();

	/**
	 * Locate BZh header prefix in buffer. In case of first search - just check
	 * the beginning of buffer and signal error if it doesn't match to headers.
	 *
	 * @return pointer to BZh header prefix if located. getInBuffEnd() if not.
	 *         failure() and getErrState() will indicate error if such occurred.
	 */
	CharType * locateHeaderPrefixInBuff();

	size_t getOutBuffCapacityLimit() const { return _outBuffCapacityLimit; }

	int _hInFile; // input file descriptor
	bool _eof;

	basic_string<CharType> _bz2Header;
	basic_string<CharType> _bz2HeaderZero;
	bool _bz2HeaderFound;
	bool _searchStatus;

	CharType * _inBuff;
	CharType * _inBuffEnd; // end of data read from file
	CharType * _inBuffCurrent;
	CharType * _inBuffSearchPtr;

	size_t _inBuffCapacity; // allocated memory capacity for in buffer

	outBuff _outBuff;
	size_t _outBuffCapacity;
	size_t _outBuffCapacityHint; // keep max used capacity
	size_t _outBuffCapacityLimit;

	unsigned int _errState; // 0 - ok; otherwise error
	int _outSequenceNumber; // output block sequence number in bz2 stream (>0 if segmented)
	int _streamNumber;
};

}

#endif	/* _BZ2STREAMSCANNER_H */

