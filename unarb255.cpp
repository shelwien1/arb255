/**
 * Bijective Arithmetic Decoder for 256 symbols
 * Version 20040723
 *
 * This implements bijective arithmetic decoding, the inverse of arb255.cpp.
 * It decodes a bit stream that was encoded with bijective arithmetic coding.
 *
 * Key Algorithm Components:
 * 1. Arithmetic decoding: Narrows probability intervals to decode symbols
 * 2. Free end detection: Recognizes stream termination marker
 * 3. Adaptive model: Same 256 binary models as encoder
 * 4. Context switching: Follows same binary tree path as encoder
 *
 * The decoder must maintain exact synchronization with encoder:
 * - Same probability model updates
 * - Same context switching logic
 * - Same free end calculations
 */

#include <stdio.h>
#include <stdlib.h>
#include "bit_byts.inc"

/**
 * Two-state frequency model (must match encoder)
 * Tracks frequency of '1' bits vs total for each of 255 contexts
 */
struct bij_2c {
    unsigned long long Fone;  // Frequency of '1' symbol
    unsigned long long Ftot;  // Total frequency (ones + zeros)
};

bij_2c ff[255];  // 255 binary models (256 leaf nodes in binary tree)
int cc;          // Current context (which model to use)

// ==================== ARITHMETIC CODING CONSTANTS ====================

typedef unsigned long long code_value;  // Type of an arithmetic code value

#define Top_value code_value(0XFFFFFFFFFFFFFFFFull)  // Largest code value

// HALF AND QUARTER POINTS IN THE CODE VALUE RANGE
#define Half      code_value((Top_value >> 1) + 1)   // Point after first half
#define First_qtr code_value(Half >> 1)              // Point after first quarter
#define Third_qtr code_value(Half + First_qtr)       // Point after third quarter

/**
 * Free End Management (from Matt Timmermans' approach)
 *
 * There are basically 2 sets of free ends:
 * 1. Free zero available, or if still in interval, the First_qtr point as alternate
 * 2. Around the Half point, systematically going higher until past current high
 *
 * Starting at Half point:
 * - Go higher until past high
 * - If still in interval with Half point, go down until less than low
 * - Then shift to next higher point and repeat
 *
 * This can never fail because:
 * - Interval must expand when it becomes half the space size
 * - Eventually use all free points up
 * - By then, interval becomes half size
 * - Number of points in interval at least doubles
 */

code_value freeend;   // Current free end
code_value fcount;    // Free end counter
int CMOD = 0;         // Code modification flag
int FRX = 0;          // Free end extend flag
int FRXX = 0;         // High free end usage flag

// ==================== BIT I/O ====================

bit_byts in;   // Input bit stream
bit_byts out;  // Output bit stream

#define dasr in.rs()   // Read a bit (with pseudo-random decoding)
#define dasw out.wz    // Write a bit

int ZEND;              // Flag for last one bit in file
code_value VALUE;      // Current decoded value

static code_value low, high;  // Ends of current code region

/**
 * Convert free end value to counter representation
 * This maps the free end value to a sequential count
 */
void fre_2_cnt(void)
{
    code_value f1, f2, f3;
    f3 = freeend;

    for (f1 = Half, f2 = 1, fcount = 1; f3 != 0; f1 >>= 1) {
        if (f3 == f1)
            break;
        fcount <<= 1;
        if (f1 & f3) {
            fcount++;
            f3 -= f1;
        }
    }

    if (f3 == 0)
        fcount = 0;
}

/**
 * Convert counter to free end value
 * Returns the free end value corresponding to a count
 */
code_value cnt_2_fre(void)
{
    code_value f1, f2, f3;

    if (fcount == 0 || fcount > Top_value) {
        freeend = 0;
        return 0;
    }

    f3 = fcount;
    for (f1 = Half, f2 = 1, freeend = Half; f3 > 1; f3 >>= 1) {
        f1 >>= 1;
        freeend >>= 1;
        if (f2 & f3) {
            freeend += Half;
        }
    }

    return f1;
}

/**
 * Increment free end to next available value
 * Must match encoder's free end sequence exactly
 */
void inc_fre(void)
{
    code_value freeetemp;
    code_value f1;

    // Convert current free end to counter, increment, convert back
    fre_2_cnt();
    fcount++;
    freeetemp = cnt_2_fre();

    // Check if we've exhausted available free ends
    if (freeend == 0) {
        FRX = 1;
        FRXX = 1;
        freeend = low;
        return;
    }

    // If free end is still in valid range, we're done
    if (low <= freeend && freeend <= high) {
        return;
    }

    // Check for overflow
    if (fcount > (Top_value - 1)) {
        FRX = 1;
        FRXX = 1;
        freeend = low;
        return;
    }

    // If free end is too high, shift it down to fit
    if (freeend > high) {
        freeetemp >>= 1;
        for (; freeetemp > high;) {
            freeetemp >>= 1;
        }

        if (freeetemp == 0) {
            FRX = 1;
            FRXX = 1;
            freeend = low;
            return;
        } else if (low <= freeetemp && freeetemp <= high) {
            freeend = freeetemp;
            return;
        }
    }

    // Search for valid free end within interval
    f1 = Top_value >> 1;
    f1 = f1 + freeetemp;
    f1 -= Half;
    freeend = 0;

    for (;; f1 >>= 1, freeetemp >>= 1) {
        freeend = ((low + f1) & ~f1) | freeetemp;

        if (freeetemp == 0) {
            FRX = 1;
            return;
        }

        if (low <= freeend && freeend <= high)
            break;
    }
}

/**
 * Input a single bit from the stream
 * Returns: 0 or 1 for normal bits, -1 for last bit, -2 thereafter
 */
inline int input_bit(void)
{
    int t;
    t = dasr;

    if (t < 0) {
        if (t == -1)
            t = 1;
        else
            t = 0;
        ZEND = 1;  // Mark end of input
    }

    return t;
}

void start_decoding(void);
int decode_symbol(bij_2c);

int main(int argc, char *argv[])
{
    int ticker = 0;
    int ch;

    fprintf(stderr, "Bijective Arithmetic 2 state uncoding version 20040723\n");
    fprintf(stderr, "Arithmetic of 256 Symbols decoding on ");

    // Open input and output files
    FILE* f_inp = fopen(argv[1], "rb");
    if (f_inp == 0) return 1;

    FILE* g_out = fopen(argv[2], "wb");
    if (g_out == 0) return 2;

    in.ir(f_inp);
    out.iw(g_out);

    // Initialize all 255 binary frequency models
    // Must match encoder initialization exactly
    for (cc = 255; cc-- > 0;) {
        ff[cc].Fone = 1;
        ff[cc].Ftot = 2;
    }

    // Initialize decoder state
    cc = 0;
    low = 0;
    high = Top_value;
    start_decoding();

    // Main decoding loop - reconstruct original bit stream
    for (;;) {
        // Progress indicator
        if ((ticker++ % 65536) == 0)
            putc('.', stderr);

        // Decode next bit using current context model
        ch = decode_symbol(ff[cc]);
        dasw(ch);

        if (ch == -1)
            break;  // End of stream detected

        // Update frequency model (must match encoder)
        if (ch == 1)
            ff[cc].Fone++;
        ff[cc].Ftot++;

        // Update context for next bit (must match encoder)
        if (ch == 0) {
            cc = 2 * cc + 1;  // Go left in tree (0 child)
        } else {
            cc = 2 * cc + 2;  // Go right in tree (1 child)
        }

        // Wrap context if we've gone past 255
        if (cc >= 255)
            cc = 0;
    }

    // Display end-of-stream marker for verification
    fprintf(stderr, "\n EOS = ");
    fcount = Half;

    if (freeend == 0)
        fprintf(stderr, " { NULL } ");

    // Show the free end value that was detected
    for (; freeend != 0; fcount >>= 1) {
        ch = (fcount & freeend) != 0 ? 1 : 0;
        if (ch == 1) {
            fprintf(stderr, "1");
            freeend -= fcount;
        } else {
            fprintf(stderr, "0");
        }
    }

    fprintf(stderr, " SUCCESSFUL \n");
    if (FRXX == 1)
        fprintf(stderr, "BUT USED HIGH FREEENDS");

    return 0;
}

/**
 * Initialize the decoder by reading initial bits
 *
 * Algorithm:
 * 1. Start with VALUE = 1
 * 2. Read bits until VALUE >= Half (reach the valid range)
 * 3. Subtract Half and read one more bit
 * 4. Now VALUE is positioned correctly within [low, high]
 */
void start_decoding(void)
{
    VALUE = 1;
    freeend = Half;
    fcount = 1;
    ZEND = 0;

    // Read initial bits to fill VALUE
    for (; VALUE < Half;) {
        VALUE = 2 * VALUE + input_bit();
    }

    VALUE -= Half;
    VALUE = 2 * VALUE + input_bit();
}

/**
 * Decode the next symbol (0 or 1)
 *
 * Algorithm:
 * 1. Check for end-of-stream (VALUE == freeend)
 * 2. Split interval [low, high] based on symbol probabilities
 * 3. Determine which portion VALUE falls into
 * 4. That determines the decoded symbol
 * 5. Narrow interval to that portion
 * 6. Update free end (must match encoder)
 * 7. Remove bits as interval narrows
 *
 * Returns: 0 or 1 for decoded symbol, -1 for end-of-stream
 */
code_value BBB = 100;

int decode_symbol(bij_2c ff)
{
    code_value c, a, b;           // Interval calculation variables
    code_value Fzero;             // Frequency of zero symbol
    code_value oldlow, oldhigh;   // For validation
    int LPS;                      // Less Probable Symbol (0 or 1)
    static int EXX = 0;           // Error counter
    int symbol = 0;               // Decoded symbol

    oldlow = low;
    oldhigh = high;

    // Sanity check: ensure interval and free end are valid
    if (high < low || freeend > high || freeend < low) {
        fprintf(stderr, " STOP 1 impossible exit ");
        exit(0);
    }

    // Check for end-of-stream: VALUE matches free end
    if (ZEND == 1 && VALUE == freeend && FRX == 0)
        return -1;  // EXIT DONE

    // Additional end-of-stream validation
    if (ZEND == 1 && FRX == 0 && ((VALUE == 0 && CMOD == 0) ||
                                   (VALUE == Half && CMOD == 1))) {
        fprintf(stderr, " STOP past end ");
        EXX++;
        if (EXX > 5) {
            exit(0);
        }
    }

    // Calculate interval size and split (must match encoder)
    c = high - low;
    a = c / ff.Ftot;
    b = c - a * ff.Ftot;

    Fzero = ff.Ftot - ff.Fone;

    // Determine LPS and calculate its interval size (must match encoder)
    if (Fzero > ff.Fone) {
        LPS = 1;
        a = a * ff.Fone + (b * ff.Fone) / ff.Ftot;
    } else {
        LPS = 0;
        a = a * Fzero + (b * Fzero) / ff.Ftot;
    }

    // Ensure minimum interval size
    if ((low + a) > (high - a))
        a--;

    // Determine which symbol was encoded based on VALUE position
    // This must perfectly mirror the encoder's interval assignment
    if (low >= First_qtr && (high - a) <= Third_qtr && (high - a) >= Half) {
        // LPS at top of interval case
        if (VALUE >= (high - a)) {
            symbol = LPS;
            low = high - a;
        } else {
            symbol = 1 - LPS;
            high = (high - a) - 1;
        }
    } else {
        // LPS at bottom of interval (normal case)
        if (VALUE <= (low + a)) {
            symbol = LPS;
            high = low + a;
        } else {
            symbol = 1 - LPS;
            low = low + a + 1;
        }
    }

    /**
     * Free End Management (must match encoder exactly)
     *
     * The decoder must track free ends identically to encoder
     * to detect the end-of-stream marker correctly.
     */
    if (FRX != 0) {
        fprintf(stderr, "\n HERE AT LAST ");
        if (low > freeend)
            freeend = low;
        else if (freeend < high)
            freeend += 1;
        else {
            fprintf(stderr, "\n NO FREE END SO FATAL ERROR ");
            fprintf(stderr, "\n THIS SHOULD NOT HAPPEN ");
            exit(0);
        }
    } else if (freeend == Top_value) {
        freeend = low;
        FRX = 1;
    } else if (CMOD == 0 || (freeend | Half) != Half) {
        inc_fre();
    } else if (freeend == 0 || low != 0) {
        freeend = Half;
        inc_fre();
    } else {
        freeend = 0;
    }

    // Validation: interval must remain valid
    if (high < low || low < oldlow || high > oldhigh) {
        fprintf(stderr, " STOP 2 impossible exit ");
        exit(0);
    }

    // Validation: VALUE must stay within interval
    if (VALUE > high || VALUE < low) {
        fprintf(stderr, " not possible high = %16.16llx VALUE = %16.16llx low = %16.16llx ",
                high, VALUE, low);
        exit(0);
    }

    /**
     * Bit Removal Loop
     *
     * As the interval narrows, remove leading bits that are now determined.
     * Must mirror encoder's bit output logic exactly.
     */
    for (;;) {
        if (high < Half) {
            // Entire interval in lower half
            CMOD = 0;
            // No adjustment needed for VALUE
        } else if (low >= Half) {
            // Entire interval in upper half
            CMOD = 0;
            VALUE -= Half;
            freeend -= Half;
            low -= Half;
            high -= Half;
        } else if (low >= First_qtr && high < Third_qtr) {
            // Interval in middle - subtract offset
            CMOD = 1;
            VALUE -= First_qtr;
            freeend -= First_qtr;
            low -= First_qtr;
            high -= First_qtr;
        } else {
            break;  // Can't remove bits yet
        }

        // Scale up interval and read next bit
        low = 2 * low;
        high = 2 * high + 1;
        VALUE = 2 * VALUE + input_bit();
        freeend = 2 * freeend + FRX;
        FRX = 0;
    }

    return symbol;
}
