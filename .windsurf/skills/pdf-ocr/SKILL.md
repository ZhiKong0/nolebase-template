---
name: pdf-ocr
description: High-quality PDF to Markdown conversion using olmocr. Use for complex PDFs with tables, code, or mixed layouts.
argument-hint: <pdf-file-path>
---

# PDF OCR Skill (olmocr)

This skill converts PDFs to structured Markdown using olmocr for high-quality extraction.

## When to Use

- Complex PDFs with tables
- PDFs containing code blocks
- Academic papers with formulas
- Documents with mixed layouts
- When standard PDF extraction is insufficient

## Prerequisites

Ensure olmocr is installed:
```bash
pip install olmocr
```

## Steps

1. Convert PDF to Markdown using olmocr:
   ```
   python .windsurf/skills/pdf-ocr/convert.py <pdf-path>
   ```

2. Read the generated Markdown file

3. Present the content to user

## Features

- Preserves document structure
- Handles tables and code blocks
- Converts formulas to readable text
- Maintains reading order

## Output

Generates a `.md` file in the same directory as the PDF.
