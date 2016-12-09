#!/bin/bash

rm -rf pkg/
mkdir -p pkg/liblonghorn

cp -r src/ pkg/liblonghorn
cp -r include/ pkg/liblonghorn
cp -r README.md pkg/liblonghorn
cp -r LICENSE pkg/liblonghorn
cp -r Makefile pkg/liblonghorn
cp -r scripts/deb pkg/liblonghorn/debian
cd pkg/liblonghorn/
debuild -us -uc
