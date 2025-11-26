//===========================================================================
//  Copyright (C) 1999 Matt Timmermans
//  Free for non-commercial purposes as long as this notice remains intact.
//  For commercial purposes, mail me at matt@timmermans.org, and we'll talk.
//===========================================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include "arithmetic.inc"
#include "simplemodel.inc"
#include "foio.inc"

static char *_callname;

using namespace std;

/**
 * Display usage information and return error code
 */
int usage()
{
    char *s;
    // Find the base filename (strip path)
    for (s = _callname; *s; ++s);
    for (; (s != _callname) && (s[-1] != '\\') && (s[-1] != ':') && (s[-1] != '/'); --s);

    cerr << endl << "Bijective arithmetic encoder V1.2" << endl
         << "Copyright (C) 1999, Matt Timmermans" << endl << endl;
    cerr << "USAGE: " << s << " [-d] [-b <blocksize>] <infile> <outfile>" << endl << endl;
    cerr << "  -d:  decompress (default is compress)" << endl;
    cerr << "  -b n: compressed blocksize to n bytes (default is 1)" << endl << endl;
    return 100;
}

static int Test();

int main(int argc, char **argv)
{
    char *s;
    bool decomp = false;
    bool test = false;
    int blocksize = 1;

    // Parse program name
    if (argc) {
        _callname = *argv++;
        --argc;
    } else {
        _callname = "biacode";
    }

    // Parse command-line options
    while (argc && (s = *argv) && (*s++ == '-')) {
        if (!*s)
            return usage();
        --argc;
        ++argv;

        while (*s) {
            switch (*s++) {
            case 'd':
            case 'D':
                decomp = true;
                break;

            case 'T':
                test = true;
                break;

            case 'b':
            case 'B':
                if (!*s) {
                    if (!(argc && (s = *argv)))
                        return usage();
                    --argc;
                    ++argv;
                }
                blocksize = atoi(s);
                for (; *s; s++);
                if (blocksize < 1)
                    return usage();
                break;

            default:
                return usage();
            }
        }
    }

    // Run self-test if requested
    if (test) {
        if (argc)
            return usage();
        return Test();
    }

    // Require exactly two file arguments
    if (argc != 2)
        return usage();

    // Open input and output files
    {
        ifstream infile(argv[0], ios::in | ios::binary);
        if (infile.fail()) {
            cerr << "Could not read file \"" << argv[0] << endl;
            return 10;
        }

        ofstream outfile(argv[1], ios::out | ios::binary);
        if (outfile.fail()) {
            cerr << "Could not write file \"" << argv[1] << endl;
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
                    break;  // End of stream

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
                    break;  // End of input

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
static bool testfail()
{
    return false;
}

/**
 * Self-test function
 * Tests the bijective property by:
 * 1. Compressing data -> decompressing -> comparing with original
 * 2. Decompressing compressed data -> recompressing -> comparing with compressed
 * This verifies the encoding is truly bijective (one-to-one mapping)
 */
static int Test()
{
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
