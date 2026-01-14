#!/usr/bin/env python3
"""
Convert PDF manuals into a searchable hierarchical directory of text/markdown files.

Uses pdftotext (poppler-utils) for text extraction and mutool (mupdf) for outline parsing.

Usage:
    python3 pdf_to_text.py <pdf_file> <output_dir> [--dry-run] [--format text|markdown]
"""

import argparse
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


@dataclass
class OutlineEntry:
    """A single entry in the PDF outline/bookmark structure."""
    title: str
    page: int
    level: int
    children: list['OutlineEntry'] = field(default_factory=list)

    def __repr__(self):
        return f"OutlineEntry({self.title!r}, page={self.page}, level={self.level}, children={len(self.children)})"


def parse_outline(pdf_filepath: Path) -> list[OutlineEntry]:
    """
    Parse the PDF outline using mutool.

    Returns a tree of OutlineEntry objects representing the bookmark hierarchy.
    """
    result = subprocess.run(
        ['mutool', 'show', str(pdf_filepath), 'outline'],
        capture_output=True,
        text=True
    )

    if result.returncode != 0:
        print(f"Warning: mutool failed: {result.stderr}", file=sys.stderr)
        return []

    entries = []
    stack = []  # Stack of (level, entry) for building the tree

    # Pattern: (||-) followed by tabs, then "title" then #page=N
    # Level is determined by the number of tabs
    line_pattern = re.compile(r'^([|\-])\t+(.*)$')
    title_pattern = re.compile(r'^"([^"]+)"\s+#page=(\d+)')

    for line in result.stdout.splitlines():
        if not line.strip():
            continue

        # Count leading tabs after the first character to determine level
        match = line_pattern.match(line)
        if not match:
            continue

        prefix = match.group(1)
        rest = match.group(2)

        # Count tabs in original line to determine nesting
        # Format is: [|-]\t\t...rest where each extra \t is a level
        level = 0
        for i, ch in enumerate(line[1:]):  # Skip first char (| or -)
            if ch == '\t':
                level += 1
            else:
                break
        level -= 1  # First tab is just separator, not nesting
        if level < 0:
            level = 0

        # Extract title and page
        title_match = title_pattern.match(rest)
        if not title_match:
            continue

        title = title_match.group(1)
        page = int(title_match.group(2))

        entry = OutlineEntry(title=title, page=page, level=level)

        # Build tree structure
        # Pop entries from stack that are at same or deeper level
        while stack and stack[-1][0] >= level:
            stack.pop()

        if stack:
            # Add as child of parent at shallower level
            parent = stack[-1][1]
            parent.children.append(entry)
        else:
            # Top-level entry
            entries.append(entry)

        # Push this entry onto stack
        stack.append((level, entry))

    return entries


def get_page_count(pdf_filepath: Path) -> int:
    """Get the total page count of a PDF using pdfinfo."""
    result = subprocess.run(
        ['pdfinfo', str(pdf_filepath)],
        capture_output=True,
        text=True
    )
    for line in result.stdout.splitlines():
        if line.startswith('Pages:'):
            return int(line.split(':')[1].strip())
    return 0


def extract_text_range(pdf_filepath: Path, start_page: int, end_page: int) -> str:
    """Extract text from a page range using pdftotext with layout preservation."""
    result = subprocess.run(
        ['pdftotext', '-f', str(start_page), '-l', str(end_page), '-layout', str(pdf_filepath), '-'],
        capture_output=True,
        text=True
    )
    return result.stdout


def sanitize_filename(title: str, index: Optional[int] = None) -> str:
    """
    Convert a title to a safe filename.

    Replaces problematic characters, limits length, and optionally prepends index.
    """
    # Remove or replace problematic characters
    safe = re.sub(r'[<>:"/\\|?*]', '', title)
    safe = re.sub(r'[\s]+', '_', safe)
    safe = re.sub(r'[^\w\-_.]', '', safe)
    safe = safe.strip('_')

    # Limit length
    if len(safe) > 60:
        safe = safe[:60].rstrip('_')

    # Prepend index for sorting
    if index is not None:
        safe = f"{index:02d}_{safe}"

    return safe


def clean_text(text: str) -> str:
    """Clean up extracted text by removing excessive blank lines."""
    # Replace 3+ consecutive newlines with 2
    text = re.sub(r'\n{3,}', '\n\n', text)
    return text.strip()


def calculate_page_ranges(entries: list[OutlineEntry], total_pages: int) -> list[tuple[OutlineEntry, int, int]]:
    """
    Calculate the page range for each entry.

    Returns list of (entry, start_page, end_page) tuples.
    Each entry's range ends just before the next entry starts.
    """
    # Flatten the tree to get all entries in order
    flat = []

    def flatten(entry: OutlineEntry):
        flat.append(entry)
        for child in entry.children:
            flatten(child)

    for entry in entries:
        flatten(entry)

    # Sort by page number (should already be in order, but ensure it)
    flat.sort(key=lambda e: e.page)

    # Calculate ranges
    ranges = []
    for i, entry in enumerate(flat):
        start = entry.page
        if i + 1 < len(flat):
            end = flat[i + 1].page - 1
        else:
            end = total_pages

        # Ensure valid range
        if end < start:
            end = start

        ranges.append((entry, start, end))

    return ranges


def write_entry(
    pdf_filepath: Path,
    entry: OutlineEntry,
    start_page: int,
    end_page: int,
    output_path: Path,
    use_markdown: bool,
    dry_run: bool
) -> None:
    """Write a single outline entry to a file."""
    if dry_run:
        print(f"  Would write: {output_path} (pages {start_page}-{end_page})")
        return

    # Extract text
    text = extract_text_range(pdf_filepath, start_page, end_page)
    text = clean_text(text)

    # Format as markdown if requested
    if use_markdown:
        content = f"# {entry.title}\n\n{text}\n"
    else:
        content = f"{entry.title}\n{'=' * len(entry.title)}\n\n{text}\n"

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding='utf-8')


def process_entries(
    pdf_filepath: Path,
    entries: list[OutlineEntry],
    page_ranges: dict[int, tuple[int, int]],  # entry.page -> (start, end)
    output_dirpath: Path,
    use_markdown: bool,
    dry_run: bool,
    parent_index: Optional[int] = None
) -> None:
    """
    Recursively process outline entries, creating directories and files.

    - Entries with children become directories
    - Entries without children become files
    """
    ext = '.md' if use_markdown else '.txt'

    for i, entry in enumerate(entries):
        index = i if parent_index is None else i + 1  # Start from 1 for children
        start_page, end_page = page_ranges.get(entry.page, (entry.page, entry.page))

        if entry.children:
            # Create directory for this entry
            dirname = sanitize_filename(entry.title, index)
            entry_dirpath = output_dirpath / dirname

            if dry_run:
                print(f"  Would create directory: {entry_dirpath}")
            else:
                entry_dirpath.mkdir(parents=True, exist_ok=True)

            # Calculate intro range: from this entry's page to first child's page - 1
            first_child_page = entry.children[0].page
            intro_end = first_child_page - 1
            if intro_end >= start_page:
                # There's intro content before the first child
                intro_path = entry_dirpath / f"00_chapter_intro{ext}"
                write_entry(pdf_filepath, entry, start_page, intro_end, intro_path, use_markdown, dry_run)

            # Recursively process children
            process_entries(
                pdf_filepath, entry.children, page_ranges,
                entry_dirpath, use_markdown, dry_run, parent_index=index
            )
        else:
            # Create file for this entry
            filename = sanitize_filename(entry.title, index) + ext
            file_path = output_dirpath / filename
            write_entry(pdf_filepath, entry, start_page, end_page, file_path, use_markdown, dry_run)


def process_pdf(pdf_filepath: Path, output_dirpath: Path, use_markdown: bool, dry_run: bool) -> bool:
    """
    Process a single PDF file into a directory structure.

    Returns True if successful, False otherwise.
    """
    if not pdf_filepath.exists():
        print(f"Error: PDF file not found: {pdf_filepath}", file=sys.stderr)
        return False

    # Get PDF info
    total_pages = get_page_count(pdf_filepath)
    if total_pages == 0:
        print(f"Error: Could not determine page count for {pdf_filepath}", file=sys.stderr)
        return False

    print(f"Processing: {pdf_filepath.name} ({total_pages} pages)")

    # Parse outline
    entries = parse_outline(pdf_filepath)

    if not entries:
        print(f"  No outline found, using fallback extraction")
        return fallback_extraction(pdf_filepath, output_dirpath, use_markdown, dry_run)

    print(f"  Found {len(entries)} top-level outline entries")

    # Check if outline has real structure (not just page numbers)
    sample_titles = [e.title for e in entries[:5]]
    if all(t.startswith('Page ') for t in sample_titles):
        print(f"  Outline appears to be page-based, using fallback extraction")
        return fallback_extraction(pdf_filepath, output_dirpath, use_markdown, dry_run)

    # Calculate page ranges
    ranges = calculate_page_ranges(entries, total_pages)
    page_ranges = {e.page: (start, end) for e, start, end in ranges}

    # Create output directory
    pdf_name = pdf_filepath.stem
    pdf_output_dirpath = output_dirpath / sanitize_filename(pdf_name)

    if dry_run:
        print(f"  Would create directory: {pdf_output_dirpath}")
    else:
        pdf_output_dirpath.mkdir(parents=True, exist_ok=True)

    # Process entries
    process_entries(pdf_filepath, entries, page_ranges, pdf_output_dirpath, use_markdown, dry_run)

    print(f"  {'Would create' if dry_run else 'Created'} output in: {pdf_output_dirpath}")
    return True


def fallback_extraction(pdf_filepath: Path, output_dirpath: Path, use_markdown: bool, dry_run: bool) -> bool:
    """
    Fallback for PDFs without useful outlines.

    Extracts the entire PDF into a single file.
    """
    pdf_name = pdf_filepath.stem
    pdf_output_dirpath = output_dirpath / sanitize_filename(pdf_name)
    ext = '.md' if use_markdown else '.txt'
    output_path = pdf_output_dirpath / f"full_text{ext}"

    if dry_run:
        print(f"  Would create: {output_path} (full document)")
        return True

    # Extract all text
    total_pages = get_page_count(pdf_filepath)
    text = extract_text_range(pdf_filepath, 1, total_pages)
    text = clean_text(text)

    # Format
    if use_markdown:
        content = f"# {pdf_name.replace('_', ' ')}\n\n{text}\n"
    else:
        title = pdf_name.replace('_', ' ')
        content = f"{title}\n{'=' * len(title)}\n\n{text}\n"

    pdf_output_dirpath.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding='utf-8')

    print(f"  Created: {output_path}")
    return True


def main():
    parser = argparse.ArgumentParser(
        description='Convert PDF manuals into searchable text/markdown hierarchy.'
    )
    parser.add_argument('pdf_file', type=Path, help='Input PDF file')
    parser.add_argument('output_dir', type=Path, help='Output directory')
    parser.add_argument('--dry-run', action='store_true',
                        help='Show what would be created without writing files')
    parser.add_argument('--format', choices=['text', 'markdown'], default='markdown',
                        help='Output format (default: markdown)')

    args = parser.parse_args()

    use_markdown = args.format == 'markdown'

    success = process_pdf(args.pdf_file, args.output_dir, use_markdown, args.dry_run)

    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
