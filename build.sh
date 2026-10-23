#!/bin/sh

# this doesn't necessarily NEED to exist since it's just a simple make
# however this is used by my odin build script which takes in a shell script for building a dependency
# without this the script would be external to the submodule which would be stupid

make -j $(nproc)