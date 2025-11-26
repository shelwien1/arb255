//===========================================================================
//  Copyright (C) 1999 Matt Timmermans
//  Free for non-commercial purposes as long as this notice remains intact.
//  For commercial purposes, mail me at matt@timmermans.org, and we'll talk.
//===========================================================================

//===========================================================================
//  Finitely Odd Bit Stream I/O
//
//  These classes implement a bijection between byte streams and "finitely odd"
//  bit streams. A finitely odd bit stream is:
//  - Infinite length conceptually
//  - Has a final '1' bit at some finite position
//  - Followed by infinite '0' bits
//
//  This property is essential for bijective arithmetic coding because:
//  1. It allows representing end-of-stream without explicit markers
//  2. Every finite bit sequence maps to exactly one finitely-odd stream
//  3. Every finitely-odd stream maps back to exactly one finite byte sequence
//
//  NOTE: When we write files out, we XOR each byte with 55. This is theoretically
//  unnecessary, but it helps when you're playing with repeated decompression -- it
//  tends to stop files from getting incredibly large very quickly.
//===========================================================================

#include "foio.h"
#include <assert.h>

//===========================================================================
//  BytesAsFOBitsOutBuf: Output stream buffer for finitely-odd bit streams
//
//  Algorithm:
//  - Groups bytes into blocks
//  - Reserves special byte values to signal end-of-stream
//  - Tracks segments of consecutive zeros
//  - Uses reserve0 flag to maintain finitely-odd property
//===========================================================================

BytesAsFOBitsOutBuf::BytesAsFOBitsOutBuf(std::ostream &bytestream, int bytesperblock)
    : base(bytestream), segsize(0), reserve0(false), segfirst(0)
{
    blocksize = (bytesperblock > 0 ? bytesperblock : 1);
    blockleft = 0;
}

/**
 * Handle buffer overflow - write buffered bytes to underlying stream
 *
 * Algorithm:
 * 1. Process each byte in buffer
 * 2. Track segments of consecutive zeros
 * 3. Write non-zero bytes with XOR encoding
 * 4. Manage block boundaries and reserve0 flag
 *
 * @param c Overflow character (or -1 for flush)
 * @return Character value
 */
int BytesAsFOBitsOutBuf::overflow(int c)
{
    char *s, *e;

    // Process all bytes currently in buffer
    for (s = pbase(), e = pptr(); s != e; ++s) {
        if (!segsize) {
            // Start of new segment
            segfirst = *s;
            ++segsize;
        } else if (!*s) {
            // Extend current segment of zeros
            ++segsize;
        } else {
            // Non-zero byte - flush segment and write it

            if (!blockleft) {
                // Starting a new block
                // Determine if we need to reserve 0x00 or 0x80
                if (reserve0)
                    reserve0 = !(segfirst & 127);  // Reserve 0x00 if segfirst has high bit
                else
                    reserve0 = !segfirst;  // Reserve 0x00 if segfirst is zero
                blockleft = blocksize - 1;
            } else {
                // Continuing block
                reserve0 = reserve0 && !segfirst;
                --blockleft;
            }

            // Write the first byte of segment (XOR 55)
            base.put(segfirst ^ 55);

            // Write zeros in segment
            for (--segsize; segsize; --segsize) {
                if (!blockleft) {
                    reserve0 = true;
                    blockleft = blocksize - 1;
                } else {
                    --blockleft;
                }
                base.put(55);  // XOR of 0 with 55
            }

            // Start new segment with current byte
            segfirst = *s;
            ++segsize;
        }
    }

    // Reset buffer and store overflow character
    buf[0] = (char)c;
    setp(buf, buf + 256);
    if (c >= 0)
        pbump(1);

    return (c & 255);
}

/**
 * Sync buffer to underlying stream
 */
int BytesAsFOBitsOutBuf::sync()
{
    overflow(-1);
    return 0;
}

/**
 * Finalize the finitely-odd bit stream
 *
 * Algorithm:
 * 1. Flush any buffered data
 * 2. Write remaining bytes to fill current block
 * 3. Ensure stream ends in finitely-odd configuration
 *    (final '1' bit followed by infinite '0's)
 */
void BytesAsFOBitsOutBuf::End()
{
    sync();

    if (!segsize)
        segfirst = 0;

TOP:
    // Fill remaining block with segfirst
    for (; blockleft; --blockleft) {
        reserve0 = reserve0 && !segfirst;
        base.put(segfirst ^ 55);
        segfirst = 0;
    }

    // Check if we need another block to properly terminate
    if (reserve0) {
        assert(segfirst != 0);
        if (segfirst != (char)128) {
            // Can't end here - need another block
            reserve0 = false;
            blockleft = blocksize;
            goto TOP;
        }
    } else if (segfirst) {
        // Can't end here - need another block
        blockleft = blocksize;
        goto TOP;
    }

    // Stream properly terminated
    segsize = 0;
    reserve0 = false;
    blockleft = 0;
}

//===========================================================================
//  BytesAsFOBitsInBuf: Input stream buffer for finitely-odd bit streams
//
//  Algorithm:
//  - Reads blocks from underlying byte stream
//  - Decodes XOR encoding (byte ^ 55)
//  - Detects end-of-stream markers
//  - Generates infinite trailing zeros after final '1' bit
//===========================================================================

BytesAsFOBitsInBuf::BytesAsFOBitsInBuf(std::istream &bytestream, int bytesperblock)
    : base(bytestream), blockleft(0), in_done(false), reserve0(false)
{
    blocksize = (bytesperblock > 0 ? bytesperblock : 1);
    blockleft = 0;
}

/**
 * Read more data into buffer when buffer is empty
 *
 * Algorithm:
 * 1. Read bytes from underlying stream
 * 2. Decode XOR encoding
 * 3. Handle block boundaries
 * 4. Detect end-of-stream
 * 5. Generate trailing zeros after EOF
 *
 * @return First character of new buffer, or -1 for EOF
 */
int BytesAsFOBitsInBuf::underflow()
{
    char *s, *e;
    int inbyte;

    // Fill buffer with decoded bytes
    for (s = buf, e = buf + 256; (s != e); ++s) {
        if (in_done) {
            // Already read all input - generate trailing zeros
            inbyte = 0;
        } else {
            // Read next byte from underlying stream
            inbyte = base.get();
            if (inbyte < 0) {
                in_done = true;
                inbyte = 0;
            } else {
                inbyte ^= 55;  // Decode XOR
            }
        }

        if (blockleft) {
            // Continue current block
            reserve0 = reserve0 && !inbyte;
            *s = (char)inbyte;
            --blockleft;
        } else if (in_done) {
            // We've read the last block - handle end-of-stream
            if (reserve0) {
                // End with 0x80 then stop
                *s = (char)128;
                reserve0 = false;
            } else {
                // Properly terminated - generate infinite zeros
                break;
            }
        } else {
            // Start of new block
            if (reserve0)
                reserve0 = !(inbyte & 127);
            else
                reserve0 = !inbyte;
            blockleft = blocksize - 1;
            *s = (char)inbyte;
        }
    }

    if (s > buf) {
        // We have data in buffer
        setg(buf, buf, s);
        return (unsigned char)buf[0];
    } else {
        // No more data - EOF
        setg(0, 0, 0);
        return -1;
    }
}
