# Read Pointers

Each AI keeps its own read pointer file here.

Pointer contract:

- one file per AI or AI-task pointer key
- tracks only the last consumed stream entry
- must not be shared across unrelated tasks blindly
