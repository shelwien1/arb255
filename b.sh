#!/bin/bash
set -e

echo "Building arb255..."
g++ -o arb255 arb255.cpp

echo "Building biacode..."
g++ -o biacode biacode.cpp

echo "Building unarb255..."
g++ -o unarb255 unarb255.cpp

echo "Build completed successfully!"
