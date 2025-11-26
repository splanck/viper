#!/bin/bash
#
# generate_snapshot.sh - Create a tarball snapshot of the Viper source code
#
# Usage: ./scripts/generate_snapshot.sh
#
# Creates a .tar.gz archive of the current HEAD in the parent directory
# with the filename: vipersrc<YYYYMMDD>.tar.gz
#

set -e

# Get the current date in YYYYMMDD format
DATE=$(date +%Y%m%d)

# Output filename
OUTPUT="../vipersrc${DATE}.tar.gz"

# Create the archive
git archive --format=tar.gz --output="${OUTPUT}" HEAD

echo "Created snapshot: ${OUTPUT}"
