//===========================================================================
//  Copyright (C) 1999 Matt Timmermans
//  Free for non-commercial purposes as long as this notice remains intact.
//  For commercial purposes, mail me at matt@timmermans.org, and we'll talk.
//===========================================================================

#include <assert.h>
#include "arithmetic.h"

//===========================================================================
//  ArithmeticEncoder - Bijective Arithmetic Encoder
//
//  Algorithm Overview:
//  Arithmetic coding represents data as a probability interval [low, low+range).
//  As symbols are encoded, the interval narrows based on symbol probabilities.
//
//  Bijective Enhancement:
//  Traditional arithmetic coding requires an explicit EOF marker. This
//  implementation uses "free ends" - special odd numbers reserved during
//  encoding that serve as implicit terminators, creating a bijection
//  (one-to-one mapping) between inputs and outputs.
//
//  Key Invariants:
//  - range > BIT16/2 (keeps precision)
//  - freeendeven marks bit positions that must be 0 in free ends
//  - nextfreeend is always in [low, low+range) and has proper oddness
//===========================================================================

ArithmeticEncoder::ArithmeticEncoder(std::ostream &outstream)
    : bytesout(outstream)
{
    // Initialize interval to full range [0, 65536)
    low = 0;
    range = BIT16;
    intervalbits = 16;

    // Initialize free end tracking
    // freeendeven = MASK16 means all low bits can vary
    // nextfreeend = 0 is the most even number (all low bits zero)
    freeendeven = MASK16;
    nextfreeend = 0;

    // No bytes buffered for carry propagation yet
    carrybyte = 0;
    carrybuf = 0;
}

/**
 * Encode a symbol into the arithmetic coded stream
 *
 * @param model Probability model providing symbol ranges
 * @param symbol Symbol to encode
 * @param could_have_ended True if stream could validly end here
 *
 * Algorithm:
 * 1. Reserve a free end if this is a potential stopping point
 * 2. Narrow the interval based on symbol probability
 * 3. Ensure nextfreeend stays within the interval
 * 4. Output bytes as leading bits become determined
 */
void ArithmeticEncoder::Encode(
    const ArithmeticModel *model,
    int symbol,
    bool could_have_ended)
{
    U32 newh, newl;

    // nextfreeend is guaranteed to be in the current range at this point

    if (could_have_ended) {
        // Reserve the next free end value as a potential terminator
        // This maintains the bijective property by ensuring we can
        // always signal end-of-stream without ambiguity

        if (nextfreeend)
            // Add (freeendeven+1)*2 to get next odd number with same evenness
            nextfreeend += (freeendeven + 1) << 1;
        else
            // From most even number, jump to first odd
            nextfreeend = freeendeven + 1;
    }

    // Get symbol's probability range [newl, newh) from model
    model->GetSymRange(symbol, &newl, &newh);

    // Scale probability range to current interval size
    newl = newl * range / model->ProbOne();
    newh = newh * range / model->ProbOne();

    // Narrow the interval to encode this symbol
    range = newh - newl;
    low += newl;

    // Ensure nextfreeend is at least as large as low
    if (nextfreeend < low)
        // Jump to smallest valid odd number >= low
        nextfreeend = ((low + freeendeven) & ~freeendeven) | (freeendeven + 1);

    // Maintain interval precision by scaling up when range gets small
    if (range <= (BIT16 >> 1)) {
        // Range is too small, scale everything by 2
        low += low;
        range += range;
        nextfreeend += nextfreeend;
        freeendeven += freeendeven + 1;  // Double and add 1

        // Ensure nextfreeend still fits in the interval
        while (nextfreeend - low >= range) {
            // nextfreeend is outside range, need to reduce its "oddness"
            freeendeven >>= 1;
            // Find smallest number with required oddness >= low
            nextfreeend = ((low + freeendeven) & ~freeendeven) | (freeendeven + 1);
        }

        // Continue scaling until range is large enough
        for (;;) {
            if (++intervalbits == 24) {
                // Accumulated 24 bits - need to output a byte

                // Extract top byte from low
                newl = low & ~MASK16;
                low -= newl;
                nextfreeend -= newl;

                // Only one number with this evenness can exist in range
                // If nextfreeend has this evenness, next scaling must reduce it
                freeendeven &= MASK16;

                // Output the byte (may trigger carry propagation)
                ByteWithCarry(newl >> 16);
                intervalbits -= 8;
            }

            if (range > (BIT16 >> 1))
                break;  // Range is now large enough

            // Scale again
            low += low;
            range += range;
            nextfreeend += nextfreeend;
            freeendeven += freeendeven + 1;
        } while (range <= (BIT16 >> 1));
    } else {
        // Range is still large, just ensure nextfreeend fits
        while (nextfreeend - low >= range) {
            freeendeven >>= 1;
            nextfreeend = ((low + freeendeven) & ~freeendeven) | (freeendeven + 1);
        }
    }
}

/**
 * Finalize encoding and write the termination marker
 *
 * Algorithm:
 * 1. Output the reserved free end value
 * 2. Flush any buffered bytes
 * 3. Reset state for potential next stream
 */
void ArithmeticEncoder::End()
{
    // Align nextfreeend to byte boundary for output
    nextfreeend <<= (24 - intervalbits);

    // Output the free end value byte by byte
    while (nextfreeend) {
        ByteWithCarry(nextfreeend >> 16);
        nextfreeend = (nextfreeend & MASK16) << 8;
    }

    // Flush any remaining buffered bytes
    if (carrybuf)
        ByteWithCarry(0);

    // Reset encoder state for potential reuse
    low = 0;
    range = BIT16;
    intervalbits = 16;
    freeendeven = MASK16;
    nextfreeend = 0;
    carrybyte = 0;
    carrybuf = 0;
}

/**
 * Output a byte with carry propagation handling
 *
 * Carry Propagation Problem:
 * When low increases, it might overflow into already-output bytes.
 * Example: Output 0xFF, then low increases by 2 -> need to output 0x01 0x01
 *
 * Solution:
 * Buffer bytes in form: [carrybyte] [0xFF]* (carrybuf-1 times)
 * When next byte comes in:
 * - If >= 256: carry occurred, output carrybyte+1 then zeros
 * - If < 255: no carry, output carrybyte then 0xFFs
 * - If == 255: extend buffer, defer decision
 *
 * @param outbyte Byte to output (may be >= 256 indicating carry)
 */
inline void ArithmeticEncoder::ByteWithCarry(U32 outbyte)
{
    if (carrybuf) {
        if (outbyte >= 256) {
            // Carry occurred - propagate it
            bytesout.put((char)(carrybyte + 1));
            while (--carrybuf)
                bytesout.put(0);
            carrybyte = (BYTE)outbyte;
        } else if (outbyte < 255) {
            // No carry - flush buffer
            bytesout.put((char)carrybyte);
            while (--carrybuf)
                bytesout.put((char)255);
            carrybyte = (BYTE)outbyte;
        }
        // If outbyte == 255, just extend buffer (don't increment carrybuf here)
    } else {
        carrybyte = (BYTE)outbyte;
    }
    ++carrybuf;
}

//===========================================================================
//  ArithmeticDecoder - Bijective Arithmetic Decoder
//
//  Algorithm Overview:
//  The decoder maintains the same interval [low, low+range) as the encoder,
//  plus a VALUE that was read from the input stream.
//
//  Decoding Process:
//  1. Determine which symbol's range contains VALUE
//  2. Narrow interval to that symbol's range
//  3. Remove leading bits that are now determined
//  4. Read new bits to maintain VALUE precision
//
//  End Detection:
//  When VALUE equals a reserved free end and no more input is available,
//  we've reached the end of the stream.
//===========================================================================

ArithmeticDecoder::ArithmeticDecoder(std::istream &instream)
    : bytesin(instream)
{
    // Initialize interval
    low = 0;
    range = BIT16;
    intervalbits = 16;

    // Initialize free end tracking (must match encoder)
    freeendeven = MASK16;
    nextfreeend = 0;

    // Initialize input value reading
    value = 0;
    // Start at -24 instead of -16 because we preload a zero into followbuf
    valueshift = -24;
    followbyte = 0;
    followbuf = 1;
}

/**
 * Decode the next symbol from the stream
 *
 * @param model Probability model (must match encoder's)
 * @param can_end True if stream can validly end here
 * @return Decoded symbol, or -1 for end-of-stream
 *
 * Algorithm:
 * 1. Read input bytes as needed to maintain VALUE precision
 * 2. Check for end-of-stream if this is a valid stopping point
 * 3. Find which symbol's probability range contains VALUE
 * 4. Narrow interval to that symbol's range
 * 5. Remove determined leading bits
 */
int ArithmeticDecoder::Decode(
    const ArithmeticModel *model,
    bool can_end)
{
    int ret;
    U32 newh, newl;

    // Read input bytes until we have enough precision in VALUE
    while (valueshift <= 0) {
        // Shift VALUE left by 8 bits
        value <<= 8;
        valueshift += 8;

        if (!--followbuf) {
            // Consumed all follow bytes, add the follow byte to VALUE
            value |= followbyte;

            // Refill followbuf with next run of zeros or EOF
            int cin;
            do {
                cin = bytesin.get();
                if (cin < 0) {
                    // End of input
                    followbuf = -1;
                    break;
                }
                ++followbuf;
                followbyte = (BYTE)cin;
            } while (!followbyte);  // Count consecutive zeros
        }
    }

    // Check for end-of-stream
    if (can_end) {
        // If no more input and VALUE equals reserved free end, we're done
        if ((followbuf < 0) && (((nextfreeend - low) << valueshift) == value))
            return -1;  // EOF

        // Reserve the next free end (must match encoder)
        if (nextfreeend)
            nextfreeend += (freeendeven + 1) << 1;
        else
            nextfreeend = freeendeven + 1;
    }

    // Decode: find which symbol's range contains VALUE
    // Calculate scaled position within probability space
    newl = ((value >> valueshift) * model->ProbOne() + model->ProbOne() - 1) / range;
    ret = model->GetSymbol(newl, &newl, &newh);

    // Scale symbol range to current interval
    newl = newl * range / model->ProbOne();
    newh = newh * range / model->ProbOne();

    // Narrow interval to decoded symbol's range
    range = newh - newl;
    value -= (newl << valueshift);
    low += newl;

    // Ensure nextfreeend >= low (must match encoder)
    if (nextfreeend < low)
        nextfreeend = ((low + freeendeven) & ~freeendeven) | (freeendeven + 1);

    // Maintain precision by scaling interval
    if (range <= (BIT16 >> 1)) {
        // Range is too small, scale by 2
        low += low;
        range += range;
        nextfreeend += nextfreeend;
        freeendeven += freeendeven + 1;
        --valueshift;  // VALUE will be scaled up when we read next byte

        // Ensure nextfreeend fits in range
        while (nextfreeend - low >= range) {
            freeendeven >>= 1;
            nextfreeend = ((low + freeendeven) & ~freeendeven) | (freeendeven + 1);
        }

        // Continue scaling until range is large enough
        for (;;) {
            if (++intervalbits == 24) {
                // Drop top byte from interval
                newl = low & ~MASK16;
                low -= newl;
                nextfreeend -= newl;
                freeendeven &= MASK16;
                intervalbits -= 8;
            }

            if (range > (BIT16 >> 1))
                break;  // Range is now large enough

            // Scale again
            low += low;
            range += range;
            nextfreeend += nextfreeend;
            freeendeven += freeendeven + 1;
            --valueshift;
        } while (range <= (BIT16 >> 1));
    } else {
        // Range is still large, just ensure nextfreeend fits
        while (nextfreeend - low >= range) {
            freeendeven >>= 1;
            nextfreeend = ((low + freeendeven) & ~freeendeven) | (freeendeven + 1);
        }
    }

    return ret;
}
