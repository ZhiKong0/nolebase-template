#!/usr/bin/env python3
"""
PDF Text Extraction Script for Windsurf Skill
Usage: python extract.py <pdf-path>
"""

import sys
import os
from pathlib import Path

def extract_pdf_text(pdf_path):
    """Extract text from PDF file."""
    try:
        from pypdf import PdfReader
    except ImportError:
        print("Error: pypdf not installed. Run: pip install pypdf")
        sys.exit(1)
    
    if not os.path.exists(pdf_path):
        print(f"Error: File not found: {pdf_path}")
        sys.exit(1)
    
    try:
        reader = PdfReader(pdf_path)
        text = []
        
        for i, page in enumerate(reader.pages, 1):
            page_text = page.extract_text()
            if page_text.strip():
                text.append(f"--- Page {i} ---\n{page_text}\n")
        
        full_text = "\n".join(text)
        
        # Save to temp file
        output_path = Path(pdf_path).with_suffix('.extracted.txt')
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(full_text)
        
        print(f"Extracted {len(reader.pages)} pages")
        print(f"Output saved to: {output_path}")
        return str(output_path)
        
    except Exception as e:
        print(f"Error extracting PDF: {e}")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python extract.py <pdf-path>")
        sys.exit(1)
    
    pdf_file = sys.argv[1]
    extract_pdf_text(pdf_file)
