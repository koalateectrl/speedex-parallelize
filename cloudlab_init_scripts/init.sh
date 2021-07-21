#!/bin/bash

cd ../implementation

sudo ./autogen.sh

sudo ./configure

sudo make || true

cd /users/samwwong/xdrpp-hello-world/

chmod 777 -R .

echo "HELLO WORLD"
