#!/usr/bin/env python3
"""Exercise a successfully built staged product browser on X11."""

import sys

import upstream_smoke


if __name__ == "__main__":
    sys.exit(upstream_smoke.main(product_build=True))
