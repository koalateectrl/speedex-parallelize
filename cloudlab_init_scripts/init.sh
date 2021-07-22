#!/bin/bash
'
cd /users/samwwong/xdrpp-hello-world/implementation

sudo ./autogen.sh

sudo ./configure

sudo make || true

cd /users/samwwong/xdrpp-hello-world

sudo chmod 777 -R .

cd ./implementation/src

sudo make synthetic_data_gen

sudo make singlenode_experiment

sudo make singlenode_validator

sudo ./synthetic_data_gen default_params.yaml synthetic_data_generator/synthetic_data_params.yaml basic_allvalid

sudo make signature_shard_controller

sudo make signature_check_server_main

cd /users/samwwong/xdrpp-hello-world/

chmod 777 -R .
'
