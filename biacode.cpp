//===========================================================================
//  Copyright (C) 1999 Matt Timmermans
//  Free for non-commercial purposes as long as this notice remains intact.
//  For commercial purposes, mail me at matt@timmermans.org, and we'll talk.
//===========================================================================

#include <assert.h>
#include <fstream>
#include <iostream>
#include <istream>
#include <ostream>
#include <sstream>
#include <streambuf>

//===========================================================================
// Type definitions and constants
//===========================================================================

typedef unsigned long U32;
typedef unsigned char BYTE;

static const U32 MAXP1 = 0x08000L;
static const U32 BIT16 = 0x10000L;
static const U32 MASK16 = 0x0FFFFL;

//===========================================================================
// ArithmeticModel - Base class for probability models
//===========================================================================

class ArithmeticModel {
public:
  U32 ProbOne() const { return prob1; }
  virtual void GetSymRange(int symbol, U32 *newlow, U32 *newhigh) const = 0;
  virtual int GetSymbol(U32 p, U32 *newlow, U32 *newhigh) const = 0;

protected:
  U32 prob1;
};

//===========================================================================
// BytesAsFOBitsOutBuf - Output stream buffer for finitely-odd bit streams
//===========================================================================

class BytesAsFOBitsOutBuf : public std::streambuf {
public:
  BytesAsFOBitsOutBuf(std::ostream &bytestream, int bytesperblock = 1);
  ~BytesAsFOBitsOutBuf() { End(); }
  void End();

private:
  std::ostream &base;
  long segsize;
  int blocksize, blockleft;
  char buf[256];
  char segfirst;
  bool reserve0;
  virtual int overflow(int c);
  virtual int sync();
};

//===========================================================================
// BytesAsFOBitsInBuf - Input stream buffer for finitely-odd bit streams
//===========================================================================

class BytesAsFOBitsInBuf : public std::streambuf {
public:
  BytesAsFOBitsInBuf(std::istream &bytestream, int bytesperblock = 1);

private:
  std::istream &base;
  int blocksize, blockleft;
  bool in_done;
  bool reserve0;
  char buf[256];
  virtual int underflow();
};

//===========================================================================
// FOBitOStream and FOBitIStream - Finitely-odd bit stream classes
//===========================================================================

typedef std::ostream stdostream;
typedef std::istream stdistream;

class FOBitOStream : public stdostream {
public:
  FOBitOStream(std::ostream &base, int blocksize = 1) : stdostream(&buffer), buffer(base, blocksize) {}
  void End() { buffer.End(); }

private:
  BytesAsFOBitsOutBuf buffer;
};

class FOBitIStream : public stdistream {
public:
  FOBitIStream(std::istream &base, int blocksize = 1) : stdistream(&buffer), buffer(base, blocksize) {}

private:
  BytesAsFOBitsInBuf buffer;
};

//===========================================================================
// ArithmeticEncoder - Bijective Arithmetic Encoder
//===========================================================================

class ArithmeticEncoder {
public:
  ArithmeticEncoder(std::ostream &outstream);
  void Encode(const ArithmeticModel *model, int symbol, bool could_have_ended);
  void End();

private:
  void ByteWithCarry(U32 byte);
  std::ostream &bytesout;
  U32 low, range;
  int intervalbits;
  U32 freeendeven;
  U32 nextfreeend;
  BYTE carrybyte;
  unsigned long carrybuf;
};

//===========================================================================
// ArithmeticDecoder - Bijective Arithmetic Decoder
//===========================================================================

class ArithmeticDecoder {
public:
  ArithmeticDecoder(std::istream &instream);
  int Decode(const ArithmeticModel *model, bool can_end);

private:
  std::istream &bytesin;
  U32 low, range;
  int intervalbits;
  U32 freeendeven;
  U32 nextfreeend;
  U32 value;
  int valueshift;
  BYTE followbyte;
  long followbuf;
};

//===========================================================================
// SimpleAdaptiveModel - Adaptive probability model
//===========================================================================

class SimpleAdaptiveModel : public ArithmeticModel {
public:
  SimpleAdaptiveModel(int numsymbols);
  ~SimpleAdaptiveModel();
  void Update(int symbol);
  void Reset();
  virtual void GetSymRange(int symbol, U32 *newlow, U32 *newhigh) const;
  virtual int GetSymbol(U32 p, U32 *newlow, U32 *newhigh) const;

private:
  void AddP(int symbol, U32 n);
  void SubP(int symbol, U32 n);
  U32 *probheap;
  int symzeroindex;
  int window[4096], *w0, *w1, *w2, *w3;
};

//===========================================================================
// ArithmeticEncoder Implementation
//===========================================================================

ArithmeticEncoder::ArithmeticEncoder(std::ostream &outstream) : bytesout(outstream) {
  low = 0;
  range = BIT16;
  intervalbits = 16;
  freeendeven = MASK16;
  nextfreeend = 0;
  carrybyte = 0;
  carrybuf = 0;
}

void ArithmeticEncoder::Encode(const ArithmeticModel *model, int symbol, bool could_have_ended) {
  U32 newh, newl;

  if (could_have_ended) {
    if (nextfreeend)
      nextfreeend += (freeendeven + 1) << 1;
    else
      nextfreeend = freeendeven + 1;
  }

  model->GetSymRange(symbol, &newl, &newh);
  newl = newl * range / model->ProbOne();
  newh = newh * range / model->ProbOne();
  range = newh - newl;
  low += newl;

  if (nextfreeend < low)
    nextfreeend = ((low + freeendeven) & ~freeendeven) | (freeendeven + 1);

  if (range <= (BIT16 >> 1)) {
    low += low;
    range += range;
    nextfreeend += nextfreeend;
    freeendeven += freeendeven + 1;

    while (nextfreeend - low >= range) {
      freeendeven >>= 1;
      nextfreeend = ((low + freeendeven) & ~freeendeven) | (freeendeven + 1);
    }

    for (;;) {
      if (++intervalbits == 24) {
        newl = low & ~MASK16;
        low -= newl;
        nextfreeend -= newl;
        freeendeven &= MASK16;
        ByteWithCarry(newl >> 16);
        intervalbits -= 8;
      }

      if (range > (BIT16 >> 1))
        break;

      low += low;
      range += range;
      nextfreeend += nextfreeend;
      freeendeven += freeendeven + 1;
    }
    while (range <= (BIT16 >> 1))
      ;
  } else {
    while (nextfreeend - low >= range) {
      freeendeven >>= 1;
      nextfreeend = ((low + freeendeven) & ~freeendeven) | (freeendeven + 1);
    }
  }
}

void ArithmeticEncoder::End() {
  nextfreeend <<= (24 - intervalbits);

  while (nextfreeend) {
    ByteWithCarry(nextfreeend >> 16);
    nextfreeend = (nextfreeend & MASK16) << 8;
  }

  if (carrybuf)
    ByteWithCarry(0);

  low = 0;
  range = BIT16;
  intervalbits = 16;
  freeendeven = MASK16;
  nextfreeend = 0;
  carrybyte = 0;
  carrybuf = 0;
}

inline void ArithmeticEncoder::ByteWithCarry(U32 outbyte) {
  if (carrybuf) {
    if (outbyte >= 256) {
      bytesout.put((char)(carrybyte + 1));
      while (--carrybuf)
        bytesout.put(0);
      carrybyte = (BYTE)outbyte;
    } else if (outbyte < 255) {
      bytesout.put((char)carrybyte);
      while (--carrybuf)
        bytesout.put((char)255);
      carrybyte = (BYTE)outbyte;
    }
  } else {
    carrybyte = (BYTE)outbyte;
  }
  ++carrybuf;
}

//===========================================================================
// ArithmeticDecoder Implementation
//===========================================================================

ArithmeticDecoder::ArithmeticDecoder(std::istream &instream) : bytesin(instream) {
  low = 0;
  range = BIT16;
  intervalbits = 16;
  freeendeven = MASK16;
  nextfreeend = 0;
  value = 0;
  valueshift = -24;
  followbyte = 0;
  followbuf = 1;
}

int ArithmeticDecoder::Decode(const ArithmeticModel *model, bool can_end) {
  int ret;
  U32 newh, newl;

  while (valueshift <= 0) {
    value <<= 8;
    valueshift += 8;

    if (!--followbuf) {
      value |= followbyte;

      int cin;
      do {
        cin = bytesin.get();
        if (cin < 0) {
          followbuf = -1;
          break;
        }
        ++followbuf;
        followbyte = (BYTE)cin;
      } while (!followbyte);
    }
  }

  if (can_end) {
    if ((followbuf < 0) && (((nextfreeend - low) << valueshift) == value))
      return -1;

    if (nextfreeend)
      nextfreeend += (freeendeven + 1) << 1;
    else
      nextfreeend = freeendeven + 1;
  }

  newl = ((value >> valueshift) * model->ProbOne() + model->ProbOne() - 1) / range;
  ret = model->GetSymbol(newl, &newl, &newh);

  newl = newl * range / model->ProbOne();
  newh = newh * range / model->ProbOne();

  range = newh - newl;
  value -= (newl << valueshift);
  low += newl;

  if (nextfreeend < low)
    nextfreeend = ((low + freeendeven) & ~freeendeven) | (freeendeven + 1);

  if (range <= (BIT16 >> 1)) {
    low += low;
    range += range;
    nextfreeend += nextfreeend;
    freeendeven += freeendeven + 1;
    --valueshift;

    while (nextfreeend - low >= range) {
      freeendeven >>= 1;
      nextfreeend = ((low + freeendeven) & ~freeendeven) | (freeendeven + 1);
    }

    for (;;) {
      if (++intervalbits == 24) {
        newl = low & ~MASK16;
        low -= newl;
        nextfreeend -= newl;
        freeendeven &= MASK16;
        intervalbits -= 8;
      }

      if (range > (BIT16 >> 1))
        break;

      low += low;
      range += range;
      nextfreeend += nextfreeend;
      freeendeven += freeendeven + 1;
      --valueshift;
    }
    while (range <= (BIT16 >> 1))
      ;
  } else {
    while (nextfreeend - low >= range) {
      freeendeven >>= 1;
      nextfreeend = ((low + freeendeven) & ~freeendeven) | (freeendeven + 1);
    }
  }

  return ret;
}

//===========================================================================
// BytesAsFOBitsOutBuf Implementation
//===========================================================================

BytesAsFOBitsOutBuf::BytesAsFOBitsOutBuf(std::ostream &bytestream, int bytesperblock) : base(bytestream), segsize(0), reserve0(false), segfirst(0) {
  blocksize = (bytesperblock > 0 ? bytesperblock : 1);
  blockleft = 0;
}

int BytesAsFOBitsOutBuf::overflow(int c) {
  char *s, *e;

  for (s = pbase(), e = pptr(); s != e; ++s) {
    if (!segsize) {
      segfirst = *s;
      ++segsize;
    } else if (!*s) {
      ++segsize;
    } else {
      if (!blockleft) {
        if (reserve0)
          reserve0 = !(segfirst & 127);
        else
          reserve0 = !segfirst;
        blockleft = blocksize - 1;
      } else {
        reserve0 = reserve0 && !segfirst;
        --blockleft;
      }

      base.put(segfirst ^ 55);

      for (--segsize; segsize; --segsize) {
        if (!blockleft) {
          reserve0 = true;
          blockleft = blocksize - 1;
        } else {
          --blockleft;
        }
        base.put(55);
      }

      segfirst = *s;
      ++segsize;
    }
  }

  buf[0] = (char)c;
  setp(buf, buf + 256);
  if (c >= 0)
    pbump(1);

  return (c & 255);
}

int BytesAsFOBitsOutBuf::sync() {
  overflow(-1);
  return 0;
}

void BytesAsFOBitsOutBuf::End() {
  sync();

  if (!segsize)
    segfirst = 0;

TOP:
  for (; blockleft; --blockleft) {
    reserve0 = reserve0 && !segfirst;
    base.put(segfirst ^ 55);
    segfirst = 0;
  }

  if (reserve0) {
    assert(segfirst != 0);
    if (segfirst != (char)128) {
      reserve0 = false;
      blockleft = blocksize;
      goto TOP;
    }
  } else if (segfirst) {
    blockleft = blocksize;
    goto TOP;
  }

  segsize = 0;
  reserve0 = false;
  blockleft = 0;
}

//===========================================================================
// BytesAsFOBitsInBuf Implementation
//===========================================================================

BytesAsFOBitsInBuf::BytesAsFOBitsInBuf(std::istream &bytestream, int bytesperblock) : base(bytestream), blockleft(0), in_done(false), reserve0(false) {
  blocksize = (bytesperblock > 0 ? bytesperblock : 1);
  blockleft = 0;
}

int BytesAsFOBitsInBuf::underflow() {
  char *s, *e;
  int inbyte;

  for (s = buf, e = buf + 256; (s != e); ++s) {
    if (in_done) {
      inbyte = 0;
    } else {
      inbyte = base.get();
      if (inbyte < 0) {
        in_done = true;
        inbyte = 0;
      } else {
        inbyte ^= 55;
      }
    }

    if (blockleft) {
      reserve0 = reserve0 && !inbyte;
      *s = (char)inbyte;
      --blockleft;
    } else if (in_done) {
      if (reserve0) {
        *s = (char)128;
        reserve0 = false;
      } else {
        break;
      }
    } else {
      if (reserve0)
        reserve0 = !(inbyte & 127);
      else
        reserve0 = !inbyte;
      blockleft = blocksize - 1;
      *s = (char)inbyte;
    }
  }

  if (s > buf) {
    setg(buf, buf, s);
    return (unsigned char)buf[0];
  } else {
    setg(0, 0, 0);
    return -1;
  }
}

//===========================================================================
// SimpleAdaptiveModel Implementation
//===========================================================================

SimpleAdaptiveModel::SimpleAdaptiveModel(int numsymbols) {
  int i;

  for (symzeroindex = 1; symzeroindex < numsymbols; symzeroindex += symzeroindex)
    ;

  probheap = new U32[symzeroindex << 1];
  for (i = symzeroindex << 1; i--;)
    probheap[i] = 0;
  prob1 = 0;

  for (i = numsymbols; i--;)
    AddP(i, 1);

  for (i = 4096; i--;)
    window[i] = -1;

  w0 = window;
  w1 = w0 + 1024;
  w2 = w0 + 2048;
  w3 = w0 + 3072;
}

SimpleAdaptiveModel::~SimpleAdaptiveModel() { delete[] probheap; }

void SimpleAdaptiveModel::Update(int symbol) {
  w1 = ((w1 == window) ? w1 + 4095 : w1 - 1);
  if (*w1 >= 0)
    SubP(*w1, 2);

  w2 = ((w2 == window) ? w2 + 4095 : w2 - 1);
  if (*w2 >= 0)
    SubP(*w2, 1);

  w3 = ((w3 == window) ? w3 + 4095 : w3 - 1);
  if (*w3 >= 0)
    SubP(*w3, 1);

  w0 = ((w0 == window) ? w0 + 4095 : w0 - 1);
  if (*w0 >= 0)
    SubP(*w0, 2);

  *w0 = symbol;
  AddP(symbol, 6);
}

void SimpleAdaptiveModel::Reset() {
  int *w, *lim;
  lim = window + 4095;

  for (w = w0; w != w1; w = (w == lim ? window : w + 1)) {
    if (*w < 0)
      goto DONE;
    SubP(*w, 6);
    *w = -1;
  }

  for (w = w1; w != w2; w = (w == lim ? window : w + 1)) {
    if (*w < 0)
      goto DONE;
    SubP(*w, 4);
    *w = -1;
  }

  for (w = w2; w != w3; w = (w == lim ? window : w + 1)) {
    if (*w < 0)
      goto DONE;
    SubP(*w, 3);
    *w = -1;
  }

  for (w = w3; w != w0; w = (w == lim ? window : w + 1)) {
    if (*w < 0)
      goto DONE;
    SubP(*w, 2);
    *w = -1;
  }

DONE:
  return;
}

void SimpleAdaptiveModel::GetSymRange(int symbol, U32 *newlow, U32 *newhigh) const {
  int i, bit = symzeroindex;
  U32 low = 0;

  for (i = 1; i < symzeroindex;) {
    bit >>= 1;
    i += i;

    if (symbol & bit) {
      low += probheap[i++];
    }
  }

  *newlow = low;
  *newhigh = low + probheap[i];
}

int SimpleAdaptiveModel::GetSymbol(U32 p, U32 *newlow, U32 *newhigh) const {
  int i;
  U32 low = 0;

  for (i = 1; i < symzeroindex;) {
    i += i;

    if ((p - low) >= probheap[i]) {
      low += probheap[i++];
    }
  }

  *newlow = low;
  *newhigh = low + probheap[i];
  return (i - symzeroindex);
}

void SimpleAdaptiveModel::AddP(int sym, U32 n) {
  for (sym += symzeroindex; sym; sym >>= 1)
    probheap[sym] += n;

  prob1 = probheap[1];
}

void SimpleAdaptiveModel::SubP(int sym, U32 n) {
  for (sym += symzeroindex; sym; sym >>= 1)
    probheap[sym] -= n;

  prob1 = probheap[1];
}

static char *_callname;

using namespace std;

/**
 * Display usage information and return error code
 */
int usage() {
  char *s;
  // Find the base filename (strip path)
  for (s = _callname; *s; ++s)
    ;
  for (; (s != _callname) && (s[-1] != '\\') && (s[-1] != ':') && (s[-1] != '/'); --s)
    ;

  cerr << endl << "Bijective arithmetic encoder V1.2" << endl << "Copyright (C) 1999, Matt Timmermans" << endl << endl;
  cerr << "USAGE: " << s << " c|d <infile> <outfile>" << endl << endl;
  cerr << "  c:  compress" << endl;
  cerr << "  d:  decompress" << endl << endl;
  return 100;
}

static int Test();

int main(int argc, char **argv) {
  char *s;
  bool decomp = false;
  int blocksize = 1;

  // Parse program name
  if (argc) {
    _callname = *argv++;
    --argc;
  } else {
    _callname = "biacode";
  }

  // Require exactly 3 arguments: mode, input file, output file
  if (argc != 3)
    return usage();

  // Parse compression mode
  s = argv[0];
  if (*s == 'c' || *s == 'C') {
    decomp = false;
  } else if (*s == 'd' || *s == 'D') {
    decomp = true;
  } else {
    return usage();
  }

  // Open input and output files
  {
    ifstream infile(argv[1], ios::in | ios::binary);
    if (infile.fail()) {
      cerr << "Could not read file \"" << argv[1] << endl;
      return 10;
    }

    ofstream outfile(argv[2], ios::out | ios::binary);
    if (outfile.fail()) {
      cerr << "Could not write file \"" << argv[2] << endl;
      return 10;
    }

    // Initialize adaptive model for 256 symbols (bytes)
    SimpleAdaptiveModel model(256);
    int sym;

    if (decomp) {
      // DECOMPRESSION MODE
      // Algorithm: Bijective arithmetic decoding
      // - Reads a finitely-odd bit stream (stream ending with final 1, then infinite 0s)
      // - Uses arithmetic decoder to map bit stream back to symbol probabilities
      // - Adaptive model updates probabilities after each symbol
      // - Decoding stops when special end-of-stream marker is encountered

      FOBitIStream inbits(infile, blocksize);
      ArithmeticDecoder decoder(inbits);

      for (;;) {
        // Decode next symbol using current probability model
        // The 'true' parameter indicates this could be end-of-stream
        sym = decoder.Decode(&model, true);
        if (sym < 0)
          break; // End of stream

        outfile.put((char)(sym));

        // Update model with decoded symbol for adaptive compression
        model.Update(sym);
      }
    } else {
      // COMPRESSION MODE
      // Algorithm: Bijective arithmetic encoding
      // - Maps input byte stream to a finitely-odd bit stream
      // - Uses arithmetic encoder to narrow probability intervals
      // - Each symbol narrows the interval based on its probability
      // - Adaptive model updates probabilities to match input statistics
      // - Bijection ensures unique reversible encoding (no ambiguity)

      FOBitOStream outbits(outfile, blocksize);
      ArithmeticEncoder encoder(outbits);

      for (;;) {
        sym = infile.get();
        if (sym < 0)
          break; // End of input

        // Encode symbol into the probability interval
        // The 'true' parameter reserves a "free end" for potential stream termination
        encoder.Encode(&model, sym, true);

        // Update model with encoded symbol for adaptive compression
        model.Update(sym);
      }

      // Finalize encoding by writing the "free end" terminator
      encoder.End();
      outbits.End();
    }

    outfile.close();
    infile.close();
  }

  return 0;
}

/**
 * Helper function for test failure
 */
static bool testfail() { return false; }

/**
 * Self-test function
 * Tests the bijective property by:
 * 1. Compressing data -> decompressing -> comparing with original
 * 2. Decompressing compressed data -> recompressing -> comparing with compressed
 * This verifies the encoding is truly bijective (one-to-one mapping)
 */
static int Test() {
  SimpleAdaptiveModel model(256);
  int bytelen, i, inpos, outpos, sym;
  char in[10], mid[200], out[200];
  bool ok = true;

  // Test files of increasing length (0 to 4 bytes)
  for (bytelen = 0; ok && bytelen < 5; bytelen++) {
    cout << "Testing " << bytelen << " byte files...";

    // Initialize input to all zeros
    for (i = 0; i < bytelen; ++i)
      in[i] = 0;

    // Iterate through all possible files of this length
    for (;;) {
      // TEST 1: Compress then decompress
      {
        ostringstream outstr;
        {
          // Compress the input
          FOBitOStream outbits(outstr);
          ArithmeticEncoder encoder(outbits);
          model.Reset();

          for (inpos = 0; inpos < bytelen; ++inpos) {
            sym = (unsigned char)in[inpos];
            encoder.Encode(&model, sym, true);
            model.Update(sym);
          }
          encoder.End();
          outbits.End();
        }

        // Copy compressed data to mid buffer
        string compressed = outstr.str();
        for (i = 0; i < compressed.length() && i < 200; ++i)
          mid[i] = compressed[i];

        istringstream instr(compressed);
        {
          // Decompress and verify
          FOBitIStream inbits(instr);
          ArithmeticDecoder decoder(inbits);
          model.Reset();
          outpos = 0;

          for (;;) {
            sym = decoder.Decode(&model, true);
            if (sym < 0)
              break;

            // Verify decompressed data matches input
            if ((outpos == inpos) || (((char)sym) != in[outpos])) {
              ok = testfail();
              goto DONELEN;
            }
            model.Update(sym);
            ++outpos;
          }

          // Verify we decoded the correct number of bytes
          if (inpos != outpos) {
            ok = testfail();
            goto DONELEN;
          }
        }
      }

      // TEST 2: Decompress then recompress (verify bijection)
      {
        string indata(in, bytelen);
        istringstream instr(indata);
        {
          // Decompress the "compressed" raw data
          FOBitIStream inbits(instr);
          ArithmeticDecoder decoder(inbits);
          model.Reset();
          outpos = 0;

          for (;;) {
            sym = decoder.Decode(&model, true);
            if (sym < 0)
              break;
            mid[outpos++] = (char)sym;
            model.Update(sym);
          }
        }

        ostringstream outstr;
        {
          // Recompress
          FOBitOStream outbits(outstr);
          ArithmeticEncoder encoder(outbits);
          model.Reset();

          for (inpos = 0; inpos < outpos; ++inpos) {
            sym = (unsigned char)mid[inpos];
            encoder.Encode(&model, sym, true);
            model.Update(sym);
          }
          encoder.End();
          outbits.End();
        }

        // Verify recompressed data matches original
        string recompressed = outstr.str();
        if (recompressed.length() != bytelen) {
          ok = testfail();
          goto DONELEN;
        }

        for (i = 0; i < bytelen; ++i) {
          if (in[i] != recompressed[i]) {
            ok = testfail();
            goto DONELEN;
          }
        }
      }

      // Generate next test input (count through all possible byte sequences)
      i = 0;
      for (;;) {
        if (i == bytelen)
          goto DONELEN;
        if (++(in[i]))
          break;
        ++i;
      }
    }

  DONELEN:
    cout << (ok ? "OK" : "FAIL!") << endl;
  }

  return ok;
}
