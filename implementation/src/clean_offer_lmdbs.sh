#!/bin/bash

for ((i=0;i<=$1;i++));
do
	for ((j=0;j<=$1;j++));
	do
		folder="offer_database/"$i"_"$j"/"
		mkdir -p $folder
		rm $folder"data.mdb" 2> /dev/null
		rm $folder"lock.mdb" 2> /dev/null
	done
done

exit 0
