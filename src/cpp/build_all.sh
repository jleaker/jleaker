#!/bin/sh

rm -rf {bin,obj}
make JDK=/usr/intel/pkgs/java/1.6.0.31-64/ CC_ARCH_FLAGS=-m64
make JDK=/usr/intel/pkgs/java/1.6.0.31/ CC_ARCH_FLAGS=-m32
