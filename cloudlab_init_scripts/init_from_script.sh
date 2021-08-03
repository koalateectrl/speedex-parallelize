#!/bin/bash

# Place this file in /opt in Cloudlab so that the server can startup using this script and call the ./init.sh script in the github repo.

cd /users/samwwong

git clone https://github.com/sandymule/speedex-parallelize

cd /users/samwwong/speedex-parallelize/cloudlab_init_scripts/

sudo bash ./init.sh
