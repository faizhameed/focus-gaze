#!/usr/bin/env python3
"""Blocklist is no longer compiled into C++.

Runtime file:  $FOCUSGAZE_DATA_DIR/blocklist.txt
               (or ~/Library/Application Support/focusGaze/blocklist.txt)

If missing, the app auto-creates a small social seed (never overwrites an existing file).

Template to copy manually for a larger list:
  cp resources/sample_blocklist.txt "$FOCUSGAZE_DATA_DIR/blocklist.txt"
"""
print(__doc__)
