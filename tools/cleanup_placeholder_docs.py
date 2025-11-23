#!/usr/bin/env python3
"""
Remove placeholder Doxygen comments from source files.

This script removes auto-generated placeholder comments that contain:
- "Parameter description needed."
- "Return value description needed."
- "Implements ... functionality."
"""

import os
import re
import sys
from pathlib import Path

def contains_placeholder(text):
    """Check if text contains any placeholder patterns."""
    placeholders = [
        "Parameter description needed.",
        "Return value description needed.",
        r"Implements .+ functionality\."
    ]

    for pattern in placeholders:
        if re.search(pattern, text):
            return True
    return False

def remove_placeholder_comments(content):
    """Remove Doxygen comment blocks containing placeholder text."""
    lines = content.split('\n')
    result = []
    i = 0
    removed_count = 0

    while i < len(lines):
        line = lines[i]

        # Check for single-line /// comment with placeholder
        if line.strip().startswith('///'):
            if contains_placeholder(line):
                removed_count += 1
                i += 1
                continue

        # Check for /** ... */ style comment blocks
        elif '/**' in line:
            # Find the end of the comment block
            block_start = i
            block_lines = [line]
            i += 1

            # Collect the entire comment block
            while i < len(lines):
                block_lines.append(lines[i])
                if '*/' in lines[i]:
                    break
                i += 1

            # Check if block contains placeholders
            block_text = '\n'.join(block_lines)
            if contains_placeholder(block_text):
                removed_count += 1
                i += 1
                continue
            else:
                # Keep the block
                result.extend(block_lines)
                i += 1
                continue

        # Check for multi-line // comments with placeholders
        elif line.strip().startswith('//') and not line.strip().startswith('///'):
            # Check if this is part of a multi-line // comment block
            comment_block = [line]
            j = i + 1
            while j < len(lines) and lines[j].strip().startswith('//'):
                comment_block.append(lines[j])
                j += 1

            # Check if any line in the block contains placeholders
            block_text = '\n'.join(comment_block)
            if contains_placeholder(block_text):
                removed_count += len(comment_block)
                i = j
                continue

        result.append(line)
        i += 1

    return '\n'.join(result), removed_count

def process_file(filepath):
    """Process a single file and remove placeholder comments."""
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            original_content = f.read()
    except Exception as e:
        print(f"Error reading {filepath}: {e}")
        return False, 0

    cleaned_content, removed_count = remove_placeholder_comments(original_content)

    if removed_count > 0:
        try:
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(cleaned_content)
            return True, removed_count
        except Exception as e:
            print(f"Error writing {filepath}: {e}")
            return False, 0

    return False, 0

def main():
    """Main entry point."""
    if len(sys.argv) > 1:
        src_dir = Path(sys.argv[1])
    else:
        # Default to src/ directory relative to script location
        script_dir = Path(__file__).parent
        src_dir = script_dir.parent / 'src'

    if not src_dir.exists():
        print(f"Error: Directory {src_dir} does not exist")
        sys.exit(1)

    print(f"Processing files in: {src_dir}")
    print("-" * 60)

    modified_files = []
    total_removed = 0

    # Process all .cpp and .hpp files
    for ext in ['*.cpp', '*.hpp', '*.h']:
        for filepath in src_dir.rglob(ext):
            modified, removed = process_file(filepath)
            if modified:
                modified_files.append(str(filepath.relative_to(src_dir.parent)))
                total_removed += removed
                print(f"Modified: {filepath.relative_to(src_dir.parent)} (removed {removed} comment blocks)")

    print("-" * 60)
    print(f"Summary: Modified {len(modified_files)} files")
    print(f"Total comment blocks removed: {total_removed}")

    if modified_files:
        print("\nModified files:")
        for f in sorted(modified_files):
            print(f"  - {f}")

    return 0

if __name__ == "__main__":
    sys.exit(main())