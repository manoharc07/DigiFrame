#!/usr/bin/env python3
"""
Sanity suite for DigiFrame.

    python tests/run.py                 # everything
    python tests/run.py --only espn     # ESPN contract only (no device needed)
    python tests/run.py --only update   # GitHub update contract (no device needed)
    python tests/run.py --only parsers  # firmware parsers, via /api/devtest
    python tests/run.py --host 192.168.1.50 -v

See tests/README.md for what each group covers and when to run it.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import harness            # noqa: E402
import test_espn          # noqa: E402,F401  (registers via @test)
import test_device        # noqa: E402,F401
import test_update        # noqa: E402,F401

if __name__ == "__main__":
    sys.exit(harness.main())
