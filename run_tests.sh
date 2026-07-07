#!/bin/bash
export QT_QPA_PLATFORM=offscreen
cd tests/build
make -j$(nproc)
./test_remix_connector
