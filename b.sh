#!/bin/bash
set -e

echo "Building arb255..."
g++ -o arb255 arb255.cpp

echo "Building biacode..."
g++ -o biacode biacode.cpp

echo "Build completed successfully!"
