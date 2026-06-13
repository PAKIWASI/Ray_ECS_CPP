#!/bin/bash


# init xwayland just to run raylib


DISPLAY=:10

Xwayland $DISPLAY -ac &
XWAYLAND_PID=$!

trap "kill $XWAYLAND_PID 2>/dev/null" EXIT INT TERM

# Wait for display to be ready
for i in $(seq 1 20); do
    xdpyinfo -display $DISPLAY &>/dev/null && break
    sleep 0.1
done

DISPLAY=$DISPLAY ./build/main "$@"

exit $?
