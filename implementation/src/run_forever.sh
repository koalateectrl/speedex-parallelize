#!/bin/bash

for (( ; ; ))
do
	$1
	if [[ $? == 0 ]]; then
		exit 0
	fi
done
