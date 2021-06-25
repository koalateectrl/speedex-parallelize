#!/bin/bash

#usage : ./whatever <num assets> <if zero clear blocks/headers>
cd databases

mkdir -p account_database
mkdir -p header_hash_database
mkdir -p tx_block_database
mkdir -p header_database

rm account_database/data.mdb
rm account_database/lock.mdb

rm header_hash_database/data.mdb
rm header_hash_database/lock.mdb

if [ -z $2 ]; then
	rm tx_block_database/*.block || true
	rm header_database/*.header || true
fi

mkdir -p offer_database

../clean_offer_lmdbs.sh $1