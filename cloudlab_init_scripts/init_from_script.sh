#!/bin/bash

# Place this file in /opt in Cloudlab so that the server can startup using this script and call the ./init.sh script in the github repo.

cd ~/.

git clone https://github.com/sandymule/xdrpp-hello-world

cd ~/xdrpp-hello-world/cloudlab_init_scripts/

sudo bash ./init.sh
