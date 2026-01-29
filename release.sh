#!/bin/bash
set -e

cd "$(dirname "$0")"

if [ -z "$1" ]; then
  echo "Usage: ./release.sh <version>"
  echo "Example: ./release.sh v0.1.0"
  exit 1
fi

VERSION=$1

if [[ ! $VERSION =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Error: Version must be in format v0.0.0"
  exit 1
fi

echo "Building squirreldb C SDK ${VERSION}..."

make clean
make all

echo "Running tests..."
make test

TARBALL="squirreldb-c-${VERSION}.tar.gz"

echo "Creating release tarball..."
tar -czvf "${TARBALL}" \
  include/ \
  src/ \
  examples/ \
  Makefile \
  LICENSE \
  README.md

echo "Creating git tag..."
git tag "${VERSION}"
git push origin "${VERSION}"

echo ""
echo "Release ${VERSION} ready!"
echo ""
echo "Upload ${TARBALL} to GitHub release:"
echo "  gh release create ${VERSION} ${TARBALL} --title \"${VERSION}\" --notes \"SquirrelDB C SDK ${VERSION}\""
