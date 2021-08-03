#!/bin/bash

cd /users/samwwong/speedex-parallelize/implementation

sudo ./autogen.sh

sudo ./configure

sudo make || true

cd /users/samwwong/speedex-parallelize

sudo chmod 777 -R .

cd ./implementation/src

sudo make synthetic_data_gen

sudo make singlenode_experiment

sudo make singlenode_validator

sudo ./synthetic_data_gen default_params.yaml synthetic_data_generator/synthetic_data_params.yaml basic_allvalid

sudo make signature_shard_controller

sudo make signature_check_server_main

cd /users/samwwong/speedex-parallelize/

chmod 777 -R .
