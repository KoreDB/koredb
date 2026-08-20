#!/bin/bash

rm -rf cpp/docs cpp/headers c/docs c/koredb.h
python3 collect_files.py
mv headers ./cpp/
cp ../../src/include/c_api/koredb.h ./c/
cd cpp && doxygen Doxyfile
cd ..
cd c && doxygen Doxyfile
rm -rf cpp/headers c/koredb.h
