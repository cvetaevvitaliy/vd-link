#!/bin/sh
# Kill vd-link-drone if running, ignore if not

killall -9 vd-link-drone || true
