#!/usr/bin/env python3
"""
PDF to Markdown conversion using olmocr
Usage: python convert.py <pdf-path>
"""

import sys
import os
import subprocess
from pathlib import Path

def convert_pdf(pdf_path):
    """Convert PDF to Markdown using olmocr."""
    
    if not os.path.exists(pdf_path):
        print(f"Error: File not found: {pdf_path}")
        sys.exit(1)
    
    pdf_path = Path(pdf_path).resolve()
    output_dir = pdf_path.parent / ".olmocr_output"
    
    try:
        # Run olmocr pipeline
        cmd = [
            "python", "-m", "olmocr.pipeline",
            str(output_dir),
            "--markdown",
            "--pdfs", str(pdf_path)
        ]
        
        print(f"Running: {' '.join(cmd)}")
        result = subprocess.run(cmd, capture_output=True, text=True)
        
        if result.returncode != 0:
            print(f"Error: {result.stderr}")
            sys.exit(1)
        
        # Find the generated markdown file
        md_dir = output_dir / "markdown"
        if md_dir.exists():
            md_files = list(md_dir.glob("*.md"))
            if md_files:
                # Copy to same directory as PDF
                output_md = pdf_path.with_suffix('.md')
                import shutil
                shutil.copy(md_files[0], output_md)
                print(f"Converted: {output_md}")
                return str(output_md)
        
        print("Conversion completed. Check .olmocr_output/markdown/")
        return str(md_dir)
        
    except FileNotFoundError:
        print("Error: olmocr not found. Install with: pip install olmocr")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python convert.py <pdf-path>")
        sys.exit(1)
    
    pdf_file = sys.argv[1]
    convert_pdf(pdf_file)
