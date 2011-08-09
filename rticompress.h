// Event message compression for RTI events
// Since NS messages are about 1700 bytes, most of which are zero
// this code compresses out the zeros for events being sent over the net.
// George F. Riley, Georgia Tech, Spring 2000

#ifndef __RTICOMPRESS_H__
#define __RTICOMPRESS_H__

int Compress(unsigned long* pTarget, unsigned long* pSource, int Count);
int Uncompress(unsigned long* pTarget, unsigned long* pSource, int Count);

#endif
