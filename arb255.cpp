/**
 * Bijective Arithmetic Encoder for 256 symbols
 * Version 20040723
 *
 * This implements bijective arithmetic coding using a 2-state adaptive model.
 * Bijective coding creates a one-to-one mapping between input and output,
 * eliminating the need for explicit end-of-file markers.
 *
 * Key Algorithm Components:
 * 1. Arithmetic coding: Encodes symbols by narrowing probability intervals
 * 2. Free end management: Maintains bijection by tracking available "free ends"
 * 3. Adaptive model: 256 binary models that adapt based on bit context
 * 4. Context switching: Uses previous bits to select current model (binary tree)
 */

#include <stdio.h>
#include <stdlib.h>

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
  FILE *f;   // File handle
  int inuse; // Usage flag (0x69 = uninitialized, 0x01 = reading, 0x02 = writing)

  // Pseudo-random number generator state
  long long bx; // Modulus for PRNG
  long long ax; // Multiplier for PRNG
  int dw;       // PRNG state for writing
  int dr;       // PRNG state for reading
  int d1r;      // Last bit read
  int d1w;      // First '1' bit flag for ws mode
  int d2w;      // Zero count before first '1' for ws mode
  int d3w;      // Current zero count for ws mode

  // Bit I/O state
  int zerf; // Zero flag (detected 0x00 byte)
  int onef; // One flag (detected 0x80 byte after 0x00)
  int bn;   // Current byte value or next byte
  int bo;   // Previous byte value
  int l;    // Bit mask for current position in byte
  int M;    // Magic byte value (0x80)
  long zc;  // Zero counter

  /**
   * Initialize structure to default state
   */
  void xx() {
    f = NULL;
    inuse = 0x69; // Uninitialized marker
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
    bx = 0x7fffffff; // Modulus (prime)
    ax = 16807;      // Multiplier (primitive root)
  }

  /**
   * Get current usage status
   */
  int status() { return inuse; }

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
  bit_byts() { xx(); }

  /**
   * Open file for bit reading (FOF - Finitely Odd Format assumed)
   */
  void ir(FILE *fr) {
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
  void irc(FILE *fr) {
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
  int irr(FILE *frr) {
    ir(frr);
    return r();
  }

  /**
   * Read next bit with pseudo-random decoding
   * XORs the bit with PRNG output to reverse pseudo-random encoding
   */
  int rs() {
    if ((d1r = r()) < 0)
      return d1r;
    dr = (ax * dr) % bx;
    return (1 & dr ^ d1r);
  }

  /**
   * Open file for bit writing (FOF format)
   */
  void iw(FILE *fw) {
    CHK();
    inuse = 0x02;
    f = fw;
  }

  /**
   * Open file and write first bit immediately
   */
  int iww(FILE *fww, int b) {
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
      return w(1 ^ (1 & dw)); // Write '1' XORed with PRNG
    }

    if (c == -2) {
      if (d1w == 0)
        return w(-1);
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
    if (c == -2)
      return w(-2);
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
    if (c == -2)
      return wc(-2);
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
    if (f == NULL)
      return -2;

    if (bn == 2) {
      bo = fgetc(f);
      if (bo == (int)'1')
        return 1;
      if (bo == (int)'0')
        return 0;
      xx();
      return -1;
    }

    if (bn == (int)'1') {
      bo = fgetc(f);
      if (bo == (int)'1')
        return 1; // String of '1's
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
    if (f == NULL)
      return -2;

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
    if ((bo & l) == 0)
      return 0;

    bo ^= l; // Clear the bit
    if ((bn != EOF) || (bo != 0))
      return 1;

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
    if (f == NULL)
      return -2;

    if (x == 1) {
      fputc('1', f);
      return 0;
    }

    if (x == 0) {
      fputc('0', f);
      bo = 1;
    }

    if (x == -2)
      bo = 1;

    if (x == -1) {
      if (bo == 0)
        fputc('1', f);
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
    if (f == NULL)
      return -2;

    // Handle end-of-stream markers
    if (x == -1) {
      w(1);  // Write final '1'
      w(-2); // Close stream
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
    if ((l >>= 1) == 0)
      l = 0x80;

    // Set bit if x is non-zero
    if (x > 0)
      bo ^= l;

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

/**
 * Two-state frequency model
 * Tracks frequency of '1' bits vs total for each of 255 contexts
 */
struct bij_2c {
  unsigned long long Fone; // Frequency of '1' symbol
  unsigned long long Ftot; // Total frequency (ones + zeros)
};

bij_2c ff[255]; // 255 binary models (256 leaf nodes in binary tree)
int cc;         // Current context (which model to use)

// ==================== ARITHMETIC CODING CONSTANTS ====================

#define Code_value_bits 64             // Number of bits in a code value
typedef unsigned long long code_value; // Type of an arithmetic code value

#define Top_value code_value(0XFFFFFFFFFFFFFFFFull) // Largest code value
#define Half code_value((Top_value >> 1) + 1)       // Point after first half
#define First_qtr code_value(Half >> 1)             // Point after first quarter
#define Third_qtr code_value(Half + First_qtr)      // Point after third quarter

void encode_symbol(int, bij_2c);

// ==================== FREE END MANAGEMENT ====================
// Free ends enable bijective coding by maintaining unused code points
// that can serve as stream terminators

code_value freeend; // Current free end value
code_value fcount;  // Counter for free end calculation
int CMOD = 0;       // Code modification flag
int FRX = 0;        // Free end extend flag
int FRXX = 0;       // High free end usage flag

// ==================== BIT I/O ====================

bit_byts out; // Output bit stream
bit_byts in;  // Input bit stream

#define dasr in.r() // Read a bit
#define dasw out.ws // Write a bit (with pseudo-random encoding)

int symbol;
int ZATE;
code_value Nz, No; // Number of future zeros or ones

// ==================== ENCODER STATE ====================

code_value low, high;      // Ends of the current code region
code_value bits_to_follow; // Number of opposite bits to output after next bit

/**
 * Convert free end value to counter representation
 * This maps the free end value to a sequential count
 */
void fre_2_cnt(void) {
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
code_value cnt_2_fre(void) {
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
 *
 * Algorithm: Find the next odd number (in binary representation) that
 * falls within the current [low, high] interval. This ensures we always
 * have a termination point available for bijective coding.
 */
void inc_fre(void) {
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
 * Output a bit plus any pending opposite bits
 * This handles bit output with "bits to follow" for staying in middle region
 */
void bit_plus_follow(int bit) {
  for (dasw(bit); bits_to_follow > 0; bits_to_follow--)
    dasw(1 ^ bit);
}

void usage(const char *progname) {
  fprintf(stderr, "\nBijective Arithmetic 2 state coding version 20040723\n");
  fprintf(stderr, "USAGE: %s c|d <infile> <outfile>\n\n", progname);
  fprintf(stderr, "  c:  compress (bits to bytes)\n");
  fprintf(stderr, "  d:  decompress (bytes to bits)\n\n");
}

void encode_file(FILE *f_inp, FILE *g_out);
void decode_file(FILE *f_inp, FILE *g_out);

int main(int argc, char *argv[]) {
  if (argc != 4) {
    usage(argv[0]);
    return 1;
  }

  char mode = argv[1][0];
  if (mode != 'c' && mode != 'C' && mode != 'd' && mode != 'D') {
    usage(argv[0]);
    return 1;
  }

  // Open input and output files
  FILE *f_inp = fopen(argv[2], "rb");
  if (f_inp == 0) {
    fprintf(stderr, "Could not open input file: %s\n", argv[2]);
    return 1;
  }

  FILE *g_out = fopen(argv[3], "wb");
  if (g_out == 0) {
    fprintf(stderr, "Could not open output file: %s\n", argv[3]);
    fclose(f_inp);
    return 2;
  }

  if (mode == 'c' || mode == 'C') {
    fprintf(stderr, "Bijective Arithmetic 2 state coding version 20040723\n");
    fprintf(stderr, "Arithmetic of 256 symbols coding on ");
    encode_file(f_inp, g_out);
  } else {
    fprintf(stderr, "Bijective Arithmetic 2 state uncoding version 20040723\n");
    fprintf(stderr, "Arithmetic of 256 Symbols decoding on ");
    decode_file(f_inp, g_out);
  }

  fclose(f_inp);
  fclose(g_out);

  return 0;
}

void encode_file(FILE *f_inp, FILE *g_out) {
  int ch;
  int ticker = 0;

  in.ir(f_inp);
  out.iw(g_out);

  // Initialize all 255 binary frequency models
  // Each starts with equal probability (1:1 ratio)
  for (cc = 255; cc-- > 0;) {
    ff[cc].Fone = 1;
    ff[cc].Ftot = 2;
  }

  // Initialize encoder state
  cc = 0;             // Start with context 0
  high = Top_value;   // Maximum value
  low = 0;            // Minimum value
  freeend = Half;     // First free end at midpoint
  fcount = 1;         // Free end counter
  bits_to_follow = 0; // No bits pending

  // Main encoding loop - process each input byte as 8 bits
  for (;;) {
    ch = in.r();

    // Progress indicator
    if ((ticker++ % 65536) == 0)
      putc('.', stderr);

    if (ch < 0)
      break; // End of input

    // Encode the bit (0 or 1) using current context model
    encode_symbol(ch, ff[cc]);

    // Update frequency model
    if (ch == 1)
      ff[cc].Fone++;
    ff[cc].Ftot++;

    // Update context for next bit
    // This creates a binary tree where the path taken depends on bits seen
    if (ch == 0) {
      cc = 2 * cc + 1; // Go left in tree (0 child)
    } else {
      cc = 2 * cc + 2; // Go right in tree (1 child)
    }

    // Wrap context if we've gone past 255 (reached leaf level)
    if (cc >= 255)
      cc = 0;
  }

  // Finalize encoding by writing the free end marker
  fprintf(stderr, "\n EOS = ");
  fcount = Half;

  if (freeend == 0)
    fprintf(stderr, " { NULL } ");

  // Output the free end value as a binary sequence
  for (; freeend != 0; fcount >>= 1) {
    ch = (fcount & freeend) != 0 ? 1 : 0;
    bit_plus_follow(ch);

    if (ch == 1) {
      fprintf(stderr, "1");
      freeend -= fcount;
    } else {
      fprintf(stderr, "0");
    }
  }

  bit_plus_follow(0); // Final bit
  dasw(-2);           // Close bit stream

  fprintf(stderr, " SUCCESSFUL \n");
  if (FRXX == 1)
    fprintf(stderr, "BUT USED HIGH FREEENDS");
}

/**
 * Encode a single symbol (0 or 1) using adaptive binary model
 *
 * Algorithm:
 * 1. Split current interval [low, high] based on symbol probabilities
 * 2. Determine which symbol is LPS (Less Probable Symbol)
 * 3. Assign interval portions: LPS gets smaller portion
 * 4. Update free end to maintain bijective property
 * 5. Output bits when interval can be distinguished
 *
 * The bijective property is maintained by:
 * - Tracking free ends (unused code points)
 * - Ensuring interval always contains at least one free end
 * - Positioning LPS to minimize free end values
 */
void encode_symbol(int symbol, bij_2c ff) {
  code_value c, a, b; // Interval calculation variables
  code_value Fzero;   // Frequency of zero symbol
  int LPS;            // Less Probable Symbol (0 or 1)

  // Sanity check: ensure interval and free end are valid
  if (high < low || freeend > high || freeend < low) {
    fprintf(stderr, " STOP 1 impossible exit ");
    exit(0);
  }

  // Calculate interval size and split based on probabilities
  c = high - low;      // Current interval size
  a = c / ff.Ftot;     // Base portion per frequency unit
  b = c - a * ff.Ftot; // Remainder

  Fzero = ff.Ftot - ff.Fone; // Frequency of '0' symbol

  // Determine LPS and calculate its interval size
  if (Fzero > ff.Fone) {
    // '1' is less probable
    LPS = 1;
    a = a * ff.Fone + (b * ff.Fone) / ff.Ftot;
  } else {
    // '0' is less probable
    LPS = 0;
    a = a * Fzero + (b * Fzero) / ff.Ftot;
  }

  // Ensure minimum interval size
  if ((low + a) > (high - a))
    a--;

  // Assign interval based on symbol and position preference
  // Strategy: Place LPS to minimize free end growth
  if (low >= First_qtr && (high - a) <= Third_qtr && (high - a) >= Half) {
    // LPS at top of interval (helps when in middle region)
    if (symbol == LPS)
      low = high - a;
    else
      high = (high - a) - 1;
  } else if (symbol == LPS) {
    // LPS at bottom of interval (normal case)
    high = low + a;
  } else {
    // MPS (More Probable Symbol) gets remainder
    low = low + a + 1;
  }

  /**
   * Free End Management
   *
   * The free end must always stay within [low, high] to maintain bijection.
   * When interval changes, we adjust free end accordingly:
   * - If FRX flag set: free end needs special handling
   * - Otherwise: increment to next valid odd number in interval
   */
  if (FRX != 0) {
    // Free end outside interval - adjust it
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

  // Verify free end is still valid
  if ((freeend > high || freeend < low)) {
    fprintf(stderr, "\n NOWAY ");
    exit(0);
  }

  /**
   * Bit Output Loop
   *
   * Output bits as the interval narrows, using three cases:
   * 1. Interval in lower half [0, Half): output 0
   * 2. Interval in upper half [Half, Top): output 1
   * 3. Interval in middle [First_qtr, Third_qtr): defer with bits_to_follow
   */
  for (;;) {
    if (high < Half) {
      // Entire interval in lower half - output 0
      CMOD = 0;
      bit_plus_follow(0);
    } else if (low >= Half) {
      // Entire interval in upper half - output 1
      CMOD = 0;
      bit_plus_follow(1);
      low -= Half;
      high -= Half;
      freeend -= Half;
    } else if (low >= First_qtr && high < Third_qtr) {
      // Interval straddles middle - defer decision
      CMOD = 1;
      bits_to_follow += 1;
      freeend -= First_qtr;
      low -= First_qtr;
      high -= First_qtr;
    } else {
      break; // Can't output yet
    }

    // Scale up interval by 2x
    low = 2 * low;
    high = 2 * high + 1;
    freeend = 2 * freeend + FRX;
    FRX = 0;
  }

  return;
}

// ==================== DECODER FUNCTIONS ====================

int ZEND;         // Flag for last one bit in file
code_value VALUE; // Current decoded value

/**
 * Input a single bit from the stream
 * Returns: 0 or 1 for normal bits, -1 for last bit, -2 thereafter
 */
inline int input_bit(void) {
  int t;
  t = in.rs();

  if (t < 0) {
    if (t == -1)
      t = 1;
    else
      t = 0;
    ZEND = 1; // Mark end of input
  }

  return t;
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
void start_decoding(void) {
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

int decode_symbol(bij_2c ff) {
  code_value c, a, b;         // Interval calculation variables
  code_value Fzero;           // Frequency of zero symbol
  code_value oldlow, oldhigh; // For validation
  int LPS;                    // Less Probable Symbol (0 or 1)
  static int EXX = 0;         // Error counter
  int symbol = 0;             // Decoded symbol

  oldlow = low;
  oldhigh = high;

  // Sanity check: ensure interval and free end are valid
  if (high < low || freeend > high || freeend < low) {
    fprintf(stderr, " STOP 1 impossible exit ");
    exit(0);
  }

  // Check for end-of-stream: VALUE matches free end
  if (ZEND == 1 && VALUE == freeend && FRX == 0)
    return -1; // EXIT DONE

  // Additional end-of-stream validation
  if (ZEND == 1 && FRX == 0 && ((VALUE == 0 && CMOD == 0) || (VALUE == Half && CMOD == 1))) {
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
    fprintf(stderr, " not possible high = %16.16llx VALUE = %16.16llx low = %16.16llx ", high, VALUE, low);
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
      break; // Can't remove bits yet
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

void decode_file(FILE *f_inp, FILE *g_out) {
  int ticker = 0;
  int ch;

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
    out.wz(ch);

    if (ch == -1)
      break; // End of stream detected

    // Update frequency model (must match encoder)
    if (ch == 1)
      ff[cc].Fone++;
    ff[cc].Ftot++;

    // Update context for next bit (must match encoder)
    if (ch == 0) {
      cc = 2 * cc + 1; // Go left in tree (0 child)
    } else {
      cc = 2 * cc + 2; // Go right in tree (1 child)
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
}
