/**
 * Bit-Level I/O Library
 *
 * This structure provides bit-level reading and writing with multiple encoding modes:
 * 1. Plain bit I/O (r/w): Direct bit reading/writing
 * 2. Pseudo-random bit I/O (rs/ws): XOR bits with PRNG for better distribution
 * 3. Run-length bit I/O (wz/wzc): Compress runs of zeros
 *
 * All modes support "finitely odd" bit streams (ending with final 1, then infinite 0s)
 * which is essential for bijective coding.
 *
 * Return values for read/write functions:
 * - 0 or 1: Normal bit value
 * - -1: Last bit in stream (the final '1')
 * - -2: After end of stream (infinite '0's)
 */

struct bit_byts {
    FILE* f;           // File handle
    int inuse;         // Usage flag (0x69 = uninitialized, 0x01 = reading, 0x02 = writing)

    // Pseudo-random number generator state
    long long bx;      // Modulus for PRNG
    long long ax;      // Multiplier for PRNG
    int dw;            // PRNG state for writing
    int dr;            // PRNG state for reading
    int d1r;           // Last bit read
    int d1w;           // First '1' bit flag for ws mode
    int d2w;           // Zero count before first '1' for ws mode
    int d3w;           // Current zero count for ws mode

    // Bit I/O state
    int zerf;          // Zero flag (detected 0x00 byte)
    int onef;          // One flag (detected 0x80 byte after 0x00)
    int bn;            // Current byte value or next byte
    int bo;            // Previous byte value
    int l;             // Bit mask for current position in byte
    int M;             // Magic byte value (0x80)
    long zc;           // Zero counter

    /**
     * Initialize structure to default state
     */
    void xx() {
        f = NULL;
        inuse = 0x69;  // Uninitialized marker
        M = 0x80;
        l = 0;
        bn = 0;
        bo = 0;
        zerf = 0;
        onef = 0;
        zc = 0;

        // Initialize PRNG state
        dw = 1;
        d1w = 0;
        d2w = 0;
        d3w = 0;
        d1r = 0;
        dr = 1;

        // Linear congruential generator parameters
        bx = 0x7fffffff;  // Modulus (prime)
        ax = 16807;       // Multiplier (primitive root)
    }

    /**
     * Get current usage status
     */
    int status() {
        return inuse;
    }

    /**
     * Check that structure is properly initialized
     */
    void CHK() {
        if (inuse != 0x69) {
            fprintf(stderr, " all read in use bit_byts use error %x \n", inuse);
            abort();
        }
    }

    /**
     * Constructor
     */
    bit_byts() {
        xx();
    }

    /**
     * Open file for bit reading (FOF - Finitely Odd Format assumed)
     */
    void ir(FILE* fr) {
        CHK();
        inuse = 0x01;
        f = fr;
        bn = getc(f);
        if (bn == EOF) {
            fprintf(stderr, " empty file in bit_byts \n");
            abort();
        }
    }

    /**
     * Open file for reading ASCII '0'/'1' characters
     */
    void irc(FILE* fr) {
        CHK();
        inuse = 0x01;
        f = fr;
        bn = getc(f);
        if ((bn != (int)'1') && (bn != (int)'0')) {
            fprintf(stderr, " empty file in bit_byts \n");
            abort();
        }
    }

    /**
     * Open file and read first bit immediately
     */
    int irr(FILE* frr) {
        ir(frr);
        return r();
    }

    /**
     * Read next bit with pseudo-random decoding
     * XORs the bit with PRNG output to reverse pseudo-random encoding
     */
    int rs() {
        if ((d1r = r()) < 0) return d1r;
        dr = (ax * dr) % bx;
        return (1 & dr ^ d1r);
    }

    /**
     * Open file for bit writing (FOF format)
     */
    void iw(FILE* fw) {
        CHK();
        inuse = 0x02;
        f = fw;
    }

    /**
     * Open file and write first bit immediately
     */
    int iww(FILE* fww, int b) {
        iw(fww);
        return w(b);
    }

    /**
     * Write bit with pseudo-random encoding
     *
     * Algorithm:
     * - Buffers zeros until first '1' is seen
     * - XORs actual bits with PRNG output
     * - On end-of-stream (-1 or -2), flushes buffer
     *
     * @param c Bit value (0, 1, -1 for last, -2 for after last)
     * @return Status
     */
    int ws(int c) {
        if (c == 0) {
            d3w++;
            return 0;
        }

        if (c == 1) {
            if (d1w == 0) {
                // First '1' encountered - save zero count
                d1w = 1;
                d2w = d3w;
                d3w = 0;
                return 0;
            }
            // Write buffered zeros with PRNG
            for (; d2w > 0; d2w--) {
                dw = (ax * dw) % bx;
                w(1 & dw);
            }
            d2w = d3w;
            d3w = 0;
            dw = (ax * dw) % bx;
            return w(1 ^ (1 & dw));  // Write '1' XORed with PRNG
        }

        if (c == -2) {
            if (d1w == 0) return w(-1);
            // Flush buffered zeros
            for (; d2w > 0; d2w--) {
                dw = (ax * dw) % bx;
                w(1 & dw);
            }
            d1w = 0;
            return w(-1);
        }

        // c == -1 or other
        if (d1w == 0) {
            // No '1' seen yet - flush zeros
            for (; d3w > 0; d3w--) {
                dw = (ax * dw) % bx;
                w(1 & dw);
            }
            return w(-1);
        }

        // Flush all buffers
        for (; d2w > 0; d2w--) {
            dw = (ax * dw) % bx;
            w(1 & dw);
        }
        dw = (ax * dw) % bx;
        w(1 ^ (1 & dw));

        for (; d3w > 0; d3w--) {
            dw = (ax * dw) % bx;
            w(1 & dw);
        }
        d1w = 0;

        return w(-1);
    }

    /**
     * Write bit with run-length encoding of zeros
     *
     * Algorithm:
     * - Counts consecutive zeros in bn
     * - On '1' or end-of-stream, flushes zeros and writes '1'
     *
     * @param c Bit value (0, 1, -1, -2)
     * @return Status
     */
    int wz(int c) {
        if (c == -2) return w(-2);
        if (c == 0) {
            bn++;
            return 0;
        } else {
            // Flush zeros, then write bit
            for (; bn > 0; bn--)
                w(0);
            return w(c);
        }
    }

    /**
     * Write ASCII '0'/'1' character with run-length encoding
     */
    int wzc(int c) {
        if (c == -2) return wc(-2);
        if (c == 0) {
            bn++;
            return 0;
        } else {
            for (; bn > 0; bn--)
                wc(0);
            return wc(c);
        }
    }

    /**
     * Read next ASCII '0' or '1' character
     *
     * Algorithm:
     * - Reads characters from file
     * - Interprets '0' and '1' as bit values
     * - Detects end-of-stream when non-'0'/'1' character is read
     *
     * @return 0, 1, or -1 for end
     */
    int rc() {
        if (f == NULL) return -2;

        if (bn == 2) {
            bo = fgetc(f);
            if (bo == (int)'1') return 1;
            if (bo == (int)'0') return 0;
            xx();
            return -1;
        }

        if (bn == (int)'1') {
            bo = fgetc(f);
            if (bo == (int)'1') return 1;  // String of '1's
            if (bo == (int)'0') {
                bn = 1;
                return 1;
            }
            xx();
            return -1;
        }

        if (bn == (int)'0') {
            bn = 2;
            return 0;
        }

        if (bn == 1) {
            bn = 2;
            return 0;
        }

        return 7;
    }

    /**
     * Read next bit from file
     *
     * Algorithm:
     * - Reads bytes and extracts bits using mask 'l'
     * - Detects finitely-odd end marker (0x00 followed by 0x80)
     * - Returns -1 when final '1' bit is read
     * - Returns -2 for all bits after that (infinite '0's)
     *
     * @return 0, 1, -1 (last bit), or -2 (after end)
     */
    int r() {
        if (f == NULL) return -2;

        if ((l >>= 1) == 0) {
            // Need to read next byte
            l = 0x80;
            bo = bn;
            bn = fgetc(f);

            // Detect finitely-odd end marker
            if (bo == 0)
                zerf = 1;
            else if (bo != M) {
                zerf = 0;
                onef = 0;
            } else if (zerf == 1)
                onef = 1;

            // Check for end-of-stream
            if ((bn == EOF) && (onef + zerf) > 0) {
                onef = 0;
                zerf = 0;
                bn = M;
            }
        }

        // Extract bit from current byte
        if ((bo & l) == 0) return 0;

        bo ^= l;  // Clear the bit
        if ((bn != EOF) || (bo != 0)) return 1;

        // This was the last '1' bit
        xx();
        return -1;
    }

    /**
     * Write ASCII '0' or '1' character
     *
     * @param x 0, 1, -1 (end with '1'), or -2 (end after last '1')
     * @return 0 for success, -1 for sending last, -2 for after last
     */
    int wc(int x) {
        if (f == NULL) return -2;

        if (x == 1) {
            fputc('1', f);
            return 0;
        }

        if (x == 0) {
            fputc('0', f);
            bo = 1;
        }

        if (x == -2) bo = 1;

        if (x == -1) {
            if (bo == 0) fputc('1', f);
            xx();
            return x;
        }
        return 0;
    }

    /**
     * Write a bit to file
     *
     * Algorithm:
     * - Accumulates bits in 'bo' using mask 'l'
     * - Writes complete bytes to file
     * - Handles finitely-odd termination
     *
     * @param x 0, 1, -1 (last bit), or -2 (after last)
     * @return 0 for success, -1/-2 for end states
     */
    int w(int x) {
        if (f == NULL) return -2;

        // Handle end-of-stream markers
        if (x == -1) {
            w(1);    // Write final '1'
            w(-2);   // Close stream
            return -1;
        }

        if (x == -2) {
            // Check for finitely-odd end condition
            if ((bo == M) && ((zerf + onef) == 0))
                fputc(bo, f);
            if ((bo == M) || (bo == 0)) {
                xx();
                return -2;
            }
        }

        // Shift bit mask
        if ((l >>= 1) == 0) l = 0x80;

        // Set bit if x is non-zero
        if (x > 0) bo ^= l;

        // Write byte when mask wraps or on end
        if ((l == 1) || (x < 0)) {
            // Detect finitely-odd markers
            if (bo == 0)
                zerf = 1;
            else if (bo != M) {
                zerf = 0;
                onef = 0;
            } else if (zerf == 1)
                onef = 1;

            if (x < 0) {
                // End of stream
                if (((onef + zerf) == 0) || (bo != M))
                    fputc(bo, f);
                xx();
                return -2;
            } else {
                // Normal byte write
                fputc(bo, f);
                bo = 0;
            }
        }
        return 0;
    }
};
