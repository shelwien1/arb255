#!/bin/bash
set -e

echo "Running tests..."

echo "Test 1: arb255 compress arb255.cpp -> 1"
./arb255 c arb255.cpp 1

echo "Test 2: arb255 decompress 1 -> 2"
./arb255 d 1 2

echo "Test 3: biacode compress arb255.cpp -> 3"
./biacode c arb255.cpp 3

echo "Test 4: biacode decompress 3 -> 4"
./biacode d 3 4

echo "Test 5: arb255 compress arb255.cpp -> 5"
./arb255 c arb255.cpp 5

echo "Test 6: arb255 decompress 5 -> 6"
./arb255 d 5 6

echo "Test 7: biacode decompress arb255.cpp -> 7"
./biacode d arb255.cpp 7

echo "Test 8: biacode compress 7 -> 8"
./biacode c 7 8

echo ""
echo "Checking file hashes..."

# Get hash of original file
ORIGINAL_HASH=$(md5sum arb255.cpp | awk '{print $1}')
echo "Original arb255.cpp hash: $ORIGINAL_HASH"

# Check each output file
FAIL=0
for file in 2 4 6 8; do
    if [ ! -f "$file" ]; then
        echo "ERROR: File '$file' does not exist!"
        FAIL=1
    else
        FILE_HASH=$(md5sum "$file" | awk '{print $1}')
        echo "File '$file' hash: $FILE_HASH"
        if [ "$FILE_HASH" != "$ORIGINAL_HASH" ]; then
            echo "ERROR: File '$file' hash does not match original!"
            FAIL=1
        else
            echo "File '$file' hash matches ✓"
        fi
    fi
done

if [ $FAIL -eq 0 ]; then
    echo ""
    echo "All tests passed!"
else
    echo ""
    echo "Some tests failed!"
    exit 1
fi
