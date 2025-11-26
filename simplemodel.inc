//===========================================================================
//  Copyright (C) 1999 Matt Timmermans
//  Free for non-commercial purposes as long as this notice remains intact.
//  For commercial purposes, mail me at matt@timmermans.org, and we'll talk.
//===========================================================================

#include "simplemodel.h"

//===========================================================================
//  SimpleAdaptiveModel - Adaptive Probability Model for Arithmetic Coding
//
//  This is a demonstration adaptive model that learns symbol probabilities
//  from the data stream as it's being encoded/decoded.
//
//  Data Structures:
//  1. probheap: Binary heap storing cumulative symbol probabilities
//     - Allows O(log n) lookup and updates
//     - probheap[1] = total probability (ProbOne)
//     - probheap[symzeroindex + i] = probability of symbol i
//
//  2. window: Sliding window of last 4096 symbols
//     - Used to "forget" old statistics and adapt to changing data
//     - Window is divided into 4 regions with different decay rates
//     - More recent symbols have higher weight
//
//  Algorithm:
//  - When a symbol is encoded/decoded, its probability increases
//  - Old symbols (leaving the window) have their probability decreased
//  - This creates an adaptive model that tracks local statistics
//===========================================================================

/**
 * Constructor: Initialize the adaptive model
 *
 * @param numsymbols Number of symbols in alphabet
 *
 * Algorithm:
 * 1. Round numsymbols up to power of 2 for efficient heap indexing
 * 2. Initialize probability heap with equal probabilities
 * 3. Set up sliding window structure
 */
SimpleAdaptiveModel::SimpleAdaptiveModel(int numsymbols)
{
    int i;

    // Round numsymbols to next power of 2 for heap structure
    // This allows efficient binary tree traversal: parent = i/2, children = 2*i, 2*i+1
    for (symzeroindex = 1; symzeroindex < numsymbols; symzeroindex += symzeroindex);

    // Allocate probability heap
    // Size is 2 * symzeroindex: symzeroindex internal nodes + symzeroindex leaf nodes
    probheap = new U32[symzeroindex << 1];
    for (i = symzeroindex << 1; i--;)
        probheap[i] = 0;
    prob1 = 0;

    // Initialize all symbols with probability 1
    for (i = numsymbols; i--;)
        AddP(i, 1);

    // Initialize sliding window (circular buffer)
    // Window divided into 4 regions of 1024 symbols each
    for (i = 4096; i--;)
        window[i] = -1;  // -1 means "no symbol"

    // Set up pointers to 4 window regions
    // These move backwards through the circular buffer
    w0 = window;       // Most recent 1024 symbols (highest weight: 6)
    w1 = w0 + 1024;    // Next 1024 (weight: 4 for leaving, 2 for entering from w2)
    w2 = w0 + 2048;    // Next 1024 (weight: 3 for leaving, 1 for entering from w3)
    w3 = w0 + 3072;    // Oldest 1024 (weight: 2 for leaving window entirely)
};

SimpleAdaptiveModel::~SimpleAdaptiveModel()
{
    delete[] probheap;
}

/**
 * Update model after encoding/decoding a symbol
 *
 * @param symbol Symbol that was just processed
 *
 * Algorithm:
 * 1. Move window pointers backward (circular)
 * 2. Remove probability weight from symbols leaving each region
 * 3. Add new symbol to most recent region with high weight
 *
 * Weight Distribution:
 * - Symbol enters w0: add 6
 * - Symbol leaves w0 to w1: subtract 2
 * - Symbol leaves w1 to w2: subtract 1
 * - Symbol leaves w2 to w3: subtract 1
 * - Symbol leaves w3: subtract 2
 * Net: Recent symbols weighted 6, decay to 0 over 4096 steps
 */
void SimpleAdaptiveModel::Update(int symbol)
{
    // Move w1 pointer backward (circular)
    w1 = ((w1 == window) ? w1 + 4095 : w1 - 1);
    if (*w1 >= 0)  // Symbol leaving region 1
        SubP(*w1, 2);

    // Move w2 pointer backward (circular)
    w2 = ((w2 == window) ? w2 + 4095 : w2 - 1);
    if (*w2 >= 0)  // Symbol leaving region 2
        SubP(*w2, 1);

    // Move w3 pointer backward (circular)
    w3 = ((w3 == window) ? w3 + 4095 : w3 - 1);
    if (*w3 >= 0)  // Symbol leaving region 3
        SubP(*w3, 1);

    // Move w0 pointer backward (circular)
    w0 = ((w0 == window) ? w0 + 4095 : w0 - 1);
    if (*w0 >= 0)  // Symbol leaving region 0
        SubP(*w0, 2);

    // Add new symbol to most recent position
    *w0 = symbol;
    AddP(symbol, 6);  // New symbols get weight 6
}

/**
 * Reset model to initial state
 *
 * Used in testing to reset between test cases.
 * Removes all symbols from window and adjusts probabilities accordingly.
 */
void SimpleAdaptiveModel::Reset()
{
    int *w, *lim;
    lim = window + 4095;

    // Walk through window regions and remove all symbols
    // Each region has different weight that needs to be subtracted

    // Region 0 (w0): weight 6
    for (w = w0; w != w1; w = (w == lim ? window : w + 1)) {
        if (*w < 0)
            goto DONE;
        SubP(*w, 6);
        *w = -1;
    }

    // Region 1 (w1): weight 4
    for (w = w1; w != w2; w = (w == lim ? window : w + 1)) {
        if (*w < 0)
            goto DONE;
        SubP(*w, 4);
        *w = -1;
    }

    // Region 2 (w2): weight 3
    for (w = w2; w != w3; w = (w == lim ? window : w + 1)) {
        if (*w < 0)
            goto DONE;
        SubP(*w, 3);
        *w = -1;
    }

    // Region 3 (w3): weight 2
    for (w = w3; w != w0; w = (w == lim ? window : w + 1)) {
        if (*w < 0)
            goto DONE;
        SubP(*w, 2);
        *w = -1;
    }

DONE:
    return;
}

/**
 * Get cumulative probability range for a symbol
 *
 * @param symbol Symbol to query
 * @param newlow Output: cumulative probability of all symbols < symbol
 * @param newhigh Output: cumulative probability of all symbols <= symbol
 *
 * Algorithm:
 * Traverse down the probability heap from root to leaf.
 * At each node, decide whether to go left or right based on symbol bits.
 * Accumulate probabilities from left subtrees as we go.
 *
 * Complexity: O(log numsymbols)
 */
void SimpleAdaptiveModel::GetSymRange(int symbol, U32 *newlow, U32 *newhigh) const
{
    int i, bit = symzeroindex;
    U32 low = 0;

    // Traverse heap from root (index 1) to leaf
    for (i = 1; i < symzeroindex;) {
        bit >>= 1;
        i += i;  // Go to left child (2*i)

        if (symbol & bit) {
            // Symbol bit is 1, go right instead
            low += probheap[i++];  // Add left subtree probability, move to right child
        }
    }

    // i is now the leaf index for this symbol
    *newlow = low;
    *newhigh = low + probheap[i];
}

/**
 * Find symbol corresponding to a cumulative probability value
 *
 * @param p Cumulative probability value
 * @param newlow Output: cumulative probability range start
 * @param newhigh Output: cumulative probability range end
 * @return Symbol whose range contains p
 *
 * Algorithm:
 * Traverse down the probability heap.
 * At each node, check if p falls in left or right subtree.
 * This is the inverse of GetSymRange.
 *
 * Complexity: O(log numsymbols)
 */
int SimpleAdaptiveModel::GetSymbol(U32 p, U32 *newlow, U32 *newhigh) const
{
    int i;
    U32 low = 0;

    // Traverse heap from root (index 1) to leaf
    for (i = 1; i < symzeroindex;) {
        i += i;  // Go to left child (2*i)

        if ((p - low) >= probheap[i]) {
            // p is in right subtree
            low += probheap[i++];  // Add left probability, move to right child
        }
    }

    // i is now the leaf index for the symbol
    *newlow = low;
    *newhigh = low + probheap[i];
    return (i - symzeroindex);  // Convert leaf index to symbol
}

/**
 * Add probability weight to a symbol
 *
 * @param sym Symbol to modify
 * @param n Probability weight to add
 *
 * Algorithm:
 * Update the heap from leaf to root.
 * Each ancestor node accumulates probabilities of its subtree.
 * This maintains the heap invariant after a leaf update.
 */
void SimpleAdaptiveModel::AddP(int sym, U32 n)
{
    // Start at leaf, propagate up to root
    for (sym += symzeroindex; sym; sym >>= 1)
        probheap[sym] += n;

    // Update cached total probability
    prob1 = probheap[1];
}

/**
 * Subtract probability weight from a symbol
 *
 * @param sym Symbol to modify
 * @param n Probability weight to subtract
 *
 * Algorithm:
 * Same as AddP but subtracts instead of adds.
 * Updates heap from leaf to root.
 */
void SimpleAdaptiveModel::SubP(int sym, U32 n)
{
    // Start at leaf, propagate up to root
    for (sym += symzeroindex; sym; sym >>= 1)
        probheap[sym] -= n;

    // Update cached total probability
    prob1 = probheap[1];
}
