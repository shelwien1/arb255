# Bijective Arithmetic Coding for Byte Streams

## Overview

This document explains the bijective arithmetic coding implementation in `biacode.cpp`, which compresses byte streams using Matt Timmermans' bijective arithmetic coding algorithm. Unlike traditional arithmetic coding, this implementation creates a true one-to-one (bijective) mapping between input byte sequences and output byte sequences without requiring explicit EOF markers or length prefixes.

## Key Differences from arb255.cpp

| Aspect | biacode.cpp | arb255.cpp |
|--------|-------------|------------|
| **Input Domain** | Byte streams (256 symbols) | Bit streams (2 symbols) |
| **Compression** | Bytes → compressed bytes | Bits → bytes |
| **Model** | Adaptive 256-symbol model | 255 binary models (tree) |
| **Arithmetic Coder** | 16-bit precision | 64-bit precision |
| **Use Case** | General file compression | Bit stream packing |

---

## Part 1: Bijective Mapping Between Byte Streams and Finitely-Odd Bit Streams

### Architecture Overview

biacode.cpp achieves bijection through a two-layer architecture:

```
Input bytes → Arithmetic Encoder → Finitely-odd bit stream → FOBitOStream → Output bytes
     ↓                ↓                      ↓                      ↓              ↓
  [b₀...bₙ]    Interval narrowing    ...bits...1000...     XOR encoding    Final file
```

### Layer 1: Arithmetic Coding with Free Ends

The `ArithmeticEncoder` class implements bijective arithmetic coding using the free end technique:

```cpp
class ArithmeticEncoder {
  U32 low, range;        // Current interval [low, low+range)
  U32 nextfreeend;       // Next reserved termination value
  U32 freeendeven;       // Mask for free end evenness
  // ...
};
```

#### How Free Ends Work

**Free End Concept**: A reserved value within the current interval that can serve as a unique termination marker.

**Free End Properties**:
1. Free ends are "odd numbers" in a generalized binary sense
2. They follow a systematic sequence: 0 (most even), then increasingly odd numbers
3. Each symbol encoding reserves one free end
4. At termination, the current free end is output

**Free End Increment Logic**:
```cpp
if (could_have_ended) {
  if (nextfreeend)
    nextfreeend += (freeendeven + 1) << 1;  // Jump to next odd number
  else
    nextfreeend = freeendeven + 1;           // First free end
}
```

**Evenness Mask (`freeendeven`)**:
- Starts as `MASK16 = 0x0FFFF` (all low bits can vary)
- Represents which bit positions must be 0 for a number to be "even"
- As interval narrows, evenness requirements become stricter
- Example sequence with `freeendeven = 0xFF`:
  ```
  nextfreeend = 0     → most even (all specified bits are 0)
  nextfreeend = 0x100 → next (bit 8 can be 1)
  nextfreeend = 0x300 → next (bit 9 can also be 1)
  ```

#### Maintaining Free End Within Interval

After narrowing the interval for each symbol:
```cpp
if (nextfreeend < low)
  nextfreeend = ((low + freeendeven) & ~freeendeven) | (freeendeven + 1);
```

This ensures the free end:
- Always stays within `[low, low+range)`
- Maintains the correct "oddness" (determined by `freeendeven`)
- Represents the smallest valid odd number ≥ low

#### When Interval Becomes Too Small

```cpp
while (nextfreeend - low >= range) {
  freeendeven >>= 1;  // Reduce evenness requirement (accept more numbers)
  nextfreeend = ((low + freeendeven) & ~freeendeven) | (freeendeven + 1);
}
```

When the interval shrinks and can't hold the current free end, the algorithm:
1. Relaxes the evenness requirement by shifting `freeendeven` right
2. Finds a new free end with less strict oddness
3. This ensures a free end always exists within the interval

### Layer 2: Finitely-Odd Bit Stream Encoding

The `FOBitOStream` and `FOBitIStream` classes wrap byte streams to create finitely-odd bit streams:

```cpp
class FOBitOStream : public std::ostream {
  // Wraps output bytes into finitely-odd format
  BytesAsFOBitsOutBuf buffer;
};
```

#### XOR Encoding with 55

**Purpose**: Prevent pathological cases during repeated compression

```cpp
base.put(segfirst ^ 55);  // Output byte XORed with 55
```

Why XOR with 55?
1. **Breaking patterns**: Prevents files of all zeros or all 0xFF from staying that way
2. **Improved distribution**: The value 55 (0x37 = 00110111₂) flips multiple bits
3. **Reversibility**: XOR is its own inverse: `(x ^ 55) ^ 55 = x`
4. **No overhead**: Decoder automatically reverses with the same XOR

**Example**:
```
Original byte: 0x00 → XOR 55 → 0x37 (file)
Original byte: 0xFF → XOR 55 → 0xC8 (file)
Compressed 0x00 becomes non-zero in output!
```

#### Block-Based Encoding

The `BytesAsFOBitsOutBuf` processes bytes in blocks (default size 1):

```cpp
if (!blockleft) {
  if (reserve0)
    reserve0 = !(segfirst & 127);
  else
    reserve0 = !segfirst;
  blockleft = blocksize - 1;
}
```

**Reserve0 Flag**:
- Tracks whether we need to reserve 0x00 as a special marker
- Helps detect the finitely-odd termination pattern
- Similar to the 0x00 0x80 pattern in arb255.cpp but adapted for XOR encoding

#### End-of-Stream Pattern

The termination pattern for FOBitOStream:
```cpp
void End() {
  sync();
  // ...fill remaining block...
  if (reserve0) {
    assert(segfirst != 0);
    if (segfirst != (char)128) {
      // Need another block to properly terminate
      blockleft = blocksize;
      goto TOP;
    }
  } else if (segfirst) {
    blockleft = blocksize;
    goto TOP;
  }
}
```

The stream ends when:
1. All pending data is flushed
2. The reserve0 condition is satisfied (can terminate without ambiguity)
3. The final pattern indicates the last '1' bit in the finitely-odd representation

### Why This Achieves Bijection

**Forward Direction** (Compression):
```
Input bytes [b₀, b₁, ..., bₙ]
    ↓ (deterministic arithmetic encoding)
Free end sequence [f₀, f₁, ..., fₙ]
    ↓ (deterministic free end output)
Finitely-odd bit stream ...bits...1000...
    ↓ (deterministic XOR and blocking)
Output bytes [B₀, B₁, ..., Bₘ]
```

**Reverse Direction** (Decompression):
```
Input bytes [B₀, B₁, ..., Bₘ]
    ↓ (reverse XOR and blocking)
Finitely-odd bit stream ...bits...1000...
    ↓ (free end detection)
Arithmetic interval narrowing
    ↓ (symbol lookup in model)
Output bytes [b₀, b₁, ..., bₙ]
```

**Uniqueness**:
- Each input byte sequence produces a unique free end sequence (arithmetic coding is deterministic)
- Each free end sequence produces a unique finitely-odd bit stream (free end output is deterministic)
- Each finitely-odd bit stream has a unique XOR-encoded byte representation
- **Therefore**: The mapping is one-to-one (bijective)

---

## Part 2: Arithmetic Code Termination and Bijectivity

### Traditional Arithmetic Coding Problem

Standard arithmetic coding represents a message as a number in an interval:

```
Symbol sequence: [s₀, s₁, s₂, ..., sₙ]
        ↓
Interval: [0.abcdef..., 0.abcdeg...)
        ↓
Any number in this interval can represent the message!
```

**Problem**: Infinitely many representations → **not bijective**

### Free End Solution

#### Free End Reservation Strategy

The encoder reserves free ends at each potential stopping point:

```cpp
void Encode(const ArithmeticModel *model, int symbol, bool could_have_ended) {
  U32 newh, newl;

  if (could_have_ended) {
    // Reserve next free end BEFORE encoding symbol
    if (nextfreeend)
      nextfreeend += (freeendeven + 1) << 1;
    else
      nextfreeend = freeendeven + 1;
  }

  // Now encode the symbol
  model->GetSymRange(symbol, &newl, &newh);
  // ...narrow interval...
}
```

**Key insight**: By reserving the free end *before* encoding, we ensure:
1. The free end is not in the symbol's sub-interval
2. If we stop now, the free end uniquely indicates "end here"
3. If we continue, the next symbol's interval won't contain this free end

#### Termination Process

**Encoding** (`End()` method):
```cpp
void End() {
  nextfreeend <<= (24 - intervalbits);  // Align to byte boundary

  while (nextfreeend) {
    ByteWithCarry(nextfreeend >> 16);   // Output top byte
    nextfreeend = (nextfreeend & MASK16) << 8;
  }

  if (carrybuf)
    ByteWithCarry(0);  // Flush carry buffer
}
```

This outputs the reserved free end value as the final bytes of the stream.

**Decoding** (`Decode()` method):
```cpp
int Decode(const ArithmeticModel *model, bool can_end) {
  // Read bytes to fill VALUE
  while (valueshift <= 0) {
    value <<= 8;
    valueshift += 8;
    // ...read byte...
  }

  if (can_end) {
    // Check if VALUE matches the free end
    if ((followbuf < 0) && (((nextfreeend - low) << valueshift) == value))
      return -1;  // EOF detected!

    // Reserve next free end (must match encoder)
    if (nextfreeend)
      nextfreeend += (freeendeven + 1) << 1;
    else
      nextfreeend = freeendeven + 1;
  }

  // Decode next symbol
  // ...
}
```

**EOF Detection**:
- `followbuf < 0`: No more input bytes available
- `((nextfreeend - low) << valueshift) == value`: VALUE equals the reserved free end
- When both conditions are true → we've reached the exact termination point

#### Interval Narrowing with Range Preservation

```cpp
model->GetSymRange(symbol, &newl, &newh);
newl = newl * range / model->ProbOne();  // Scale to current range
newh = newh * range / model->ProbOne();
range = newh - newl;
low += newl;
```

Each symbol narrows the interval proportionally to its probability:
- High probability symbols → large sub-intervals
- Low probability symbols → small sub-intervals
- Free end must fit in the remaining interval

### Why This Achieves Bijectivity

**Uniqueness Proof**:

1. **Forward Uniqueness**:
   ```
   Each input sequence S has:
   - Unique symbol-by-symbol interval narrowing (deterministic model)
   - Unique free end sequence (deterministic increment)
   - Unique final interval containing unique free end
   - Unique output bytes (deterministic encoding of free end)

   Therefore: S₁ ≠ S₂ ⟹ Output(S₁) ≠ Output(S₂)
   ```

2. **Reverse Uniqueness**:
   ```
   Each valid output O has:
   - Unique finitely-odd bit stream (XOR reversal)
   - Unique VALUE at each step (deterministic read)
   - Unique free end detection point (mathematical equality)
   - Unique symbol sequence (deterministic model lookup)

   Therefore: O₁ ≠ O₂ ⟹ Decode(O₁) ≠ Decode(O₂)
   ```

3. **Perfect Round-Trip**:
   ```
   Encode(Decode(O)) = O  for all valid O
   Decode(Encode(S)) = S  for all S

   Therefore: Bijection established!
   ```

### Carry Propagation

The `ByteWithCarry()` method handles arithmetic carries:

```cpp
void ByteWithCarry(U32 outbyte) {
  if (carrybuf) {
    if (outbyte >= 256) {
      // Carry occurred!
      bytesout.put((char)(carrybyte + 1));
      while (--carrybuf)
        bytesout.put(0);  // Propagate carry through 0xFF bytes
      carrybyte = (BYTE)outbyte;
    } else if (outbyte < 255) {
      // No carry possible
      bytesout.put((char)carrybyte);
      while (--carrybuf)
        bytesout.put((char)255);  // Output buffered 0xFF bytes
      carrybyte = (BYTE)outbyte;
    }
    // If outbyte == 255, keep buffering
  }
  ++carrybuf;
}
```

**Why carries matter for bijection**:
- Arithmetic coding can produce values that "overflow" into the next byte
- Example: `0xFF + 0x02 = 0x101` (carry into next byte)
- Buffering ensures carries propagate correctly
- Deterministic carry handling preserves bijection

---

## Part 3: EOF Treatment and File Integrity

### Three-Level EOF Handling

#### Level 1: Physical File EOF

```cpp
cin = bytesin.get();
if (cin < 0) {
  followbuf = -1;  // Mark physical EOF
  break;
}
```

The `followbuf = -1` flag indicates no more bytes available from the underlying file.

#### Level 2: Finitely-Odd Stream EOF

The `BytesAsFOBitsInBuf` class detects the finitely-odd termination pattern:

```cpp
if (in_done) {
  if (reserve0) {
    *s = (char)128;  // Final byte in finitely-odd format
    reserve0 = false;
  } else {
    break;  // Truly done - infinite zeros follow
  }
}
```

After physical EOF:
1. If `reserve0` is true: output one final 0x80 byte (the final '1' bit)
2. Otherwise: stop producing bytes (infinite zeros)

#### Level 3: Arithmetic Coding EOF

```cpp
if (can_end) {
  if ((followbuf < 0) && (((nextfreeend - low) << valueshift) == value))
    return -1;  // Arithmetic coding EOF
}
```

The arithmetic decoder returns -1 when:
- No more input available (`followbuf < 0`)
- Current VALUE matches the reserved free end
- This indicates the encoder finished and output its final free end

### Why Appending 0x00 Breaks Decoding

Let's trace through a concrete example:

#### Original File Structure

```
Physical bytes: [compressed data...] [free end bytes] [EOF]
                                     ↑
                              Free end output by encoder

After XOR and FOBit processing:
Logical bytes: [arithmetic coded symbols] + free end marker
                                            ↑
                                    Detected by VALUE == nextfreeend
```

#### Modified File (0x00 Appended)

```
Physical bytes: [compressed data...] [free end bytes] 0x00 [EOF]
                                                       ↑
                                                   APPENDED

After XOR: 0x00 ^ 55 = 0x37 (becomes 55 in logical stream!)
```

**What happens during decoding**:

1. **Original Decoding Path**:
   ```
   Read bytes → XOR reverse → Fill VALUE
   VALUE accumulates: ...final value...
   Check: VALUE == nextfreeend? YES → return -1
   Decoding stops at correct position
   ```

2. **Modified File Decoding Path**:
   ```
   Read bytes → XOR reverse → Fill VALUE
   VALUE accumulates: ...final value...
   Check: VALUE == nextfreeend? NO! (extra byte changed VALUE)
   Continue decoding...
   Read appended byte: 0x00 ^ 55 = 0x37
   VALUE now includes this extra data
   Decoder tries to find symbol for this VALUE
   Either:
     - Decodes garbage symbol
     - Fails with error (VALUE out of range)
     - Stops at wrong position
   ```

#### Mathematical Reason

The bijection relies on exact byte sequences:

```
F: InputBytes → OutputBytes
G: OutputBytes → InputBytes

For bijection: G(F(x)) = x and F(G(y)) = y

Original: y₀ = [b₀, b₁, ..., bₙ]
Modified: y₁ = [b₀, b₁, ..., bₙ, 0x00]

Since y₀ ≠ y₁:
  G(y₁) ≠ G(y₀) = x  (different input to G produces different output)

Therefore: Appending breaks the bijection!
```

#### Why the Decoder Fails

**Specific failure modes**:

1. **VALUE Mismatch**:
   ```cpp
   if ((followbuf < 0) && (((nextfreeend - low) << valueshift) == value))
   ```
   The appended byte changes VALUE, so the equality fails.
   The decoder doesn't recognize the EOF marker.

2. **Extra Symbol Decoding**:
   ```cpp
   newl = ((value >> valueshift) * model->ProbOne() + ...) / range;
   ret = model->GetSymbol(newl, &newl, &newhigh);
   ```
   The decoder tries to interpret the appended byte as part of the data.
   This produces a garbage symbol or an error.

3. **Model State Corruption**:
   ```cpp
   model->Update(symbol);  // Updates adaptive model
   ```
   If a garbage symbol is decoded, the model's probability distribution changes.
   All subsequent decoding is based on wrong probabilities.
   Even if we later detect EOF, the output is already corrupted.

### The XOR-55 Effect

The XOR encoding adds another layer:

```
Appending 0x00 to physical file:
  Physical: ...data... [EOF] → ...data... 0x00 [EOF]

After XOR reverse during read:
  Logical: ...data... [finitely-odd end] → ...data... [finitely-odd end] 0x37
                      ↑                                 ↑
                  (was here)                    (now extra byte!)
```

The appended 0x00 becomes 0x37 (0x00 ^ 55) in the logical stream:
- Not a zero byte anymore!
- Looks like valid compressed data
- Decoder has no way to know it's invalid
- Tries to process it as a real byte

### Block Size Effect

If `blocksize > 1`, the effect cascades:

```cpp
blockleft = blocksize - 1;
```

Appending a byte might:
1. Complete an incomplete block (changes block boundary detection)
2. Start a new block (changes reserve0 state)
3. Shift the entire finitely-odd pattern (changes EOF detection)

All of these break the bijection by changing the byte-to-bit-stream mapping.

---

## Summary

The bijective arithmetic coding in `biacode.cpp` achieves true bijectivity through:

1. **Two-Layer Architecture**:
   - **Arithmetic layer**: Free end management ensures unique termination values
   - **Finitely-odd layer**: XOR-encoded byte streams map to unique bit streams

2. **Free End Mechanism**:
   - Systematic reservation of termination values
   - `freeendeven` mask for controlling "oddness"
   - Deterministic increment at each potential stopping point
   - Guaranteed existence within narrowing intervals

3. **Integrated EOF Handling**:
   - Physical EOF (no more bytes)
   - Finitely-odd EOF (final '1' bit pattern)
   - Arithmetic EOF (VALUE matches free end)
   - All three must align for correct decoding

4. **XOR-55 Encoding**:
   - Prevents pathological compression patterns
   - Fully reversible (XOR is self-inverse)
   - Adds no overhead
   - Maintains bijection (deterministic transformation)

5. **Fragility to Modification**:
   - Appending even a single byte changes the XOR-decoded stream
   - Changes VALUE during arithmetic decoding
   - Breaks free end matching
   - Corrupts model state
   - **Result**: Different output, bijection broken

This creates a mathematically rigorous bijection where every input byte sequence has exactly one valid compressed representation, and every valid compressed file decodes to exactly one original byte sequence, with no ambiguity and no external markers needed.
