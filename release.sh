#!/bin/bash
set -e

cd "$(dirname "$0")"

VERSION="0.1.0"

echo "Releasing squirreldb-sdk v${VERSION}..."

echo "Building..."
make clean
make

echo "Running tests..."
make test

echo "Creating distribution archive..."
DIST_DIR="squirreldb-sdk-${VERSION}"
mkdir -p "$DIST_DIR/include" "$DIST_DIR/src"
cp include/*.h "$DIST_DIR/include/"
cp -r include/squirreldb "$DIST_DIR/include/" 2>/dev/null || true
cp src/*.c "$DIST_DIR/src/"
cp Makefile README.md LICENSE "$DIST_DIR/" 2>/dev/null || true
tar -czf "squirreldb-sdk-${VERSION}.tar.gz" "$DIST_DIR"
rm -rf "$DIST_DIR"

echo "Released squirreldb-sdk@${VERSION}"
echo ""
echo "Distribution archive: squirreldb-sdk-${VERSION}.tar.gz"
echo ""
echo "Users can install with:"
echo "  tar -xzf squirreldb-sdk-${VERSION}.tar.gz"
echo "  cd squirreldb-sdk-${VERSION}"
echo "  make && sudo make install"
