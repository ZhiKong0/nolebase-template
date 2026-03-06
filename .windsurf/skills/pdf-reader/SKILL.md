---
name: pdf-reader
description: Extract and analyze text from PDF files. Use when users need to read, summarize, or analyze PDF documents.
argument-hint: <pdf-file-path>
---

# PDF Reader Skill

This skill extracts text content from PDF files and makes it available for analysis.

## When to Use

- User asks to read a PDF file
- User wants to extract text from a PDF
- User needs to analyze PDF content
- User asks to summarize a PDF document

## Steps

1. Extract text from the PDF using the extract script:
   ```
   python .windsurf/skills/pdf-reader/extract.py <pdf-path>
   ```

2. Read the extracted text file

3. Analyze and present the content to the user

## Output Format

Present the PDF content in a structured way:
- Document title (if available)
- Page count
- Key sections
- Summary of main content

## Notes

- Large PDFs may take time to process
- Scanned PDFs may require OCR (handled by PyPDF2)
- Password-protected PDFs cannot be processed
