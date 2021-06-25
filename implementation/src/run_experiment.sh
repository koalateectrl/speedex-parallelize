if [ "$#" -lt 2 ]; then
	echo "usage: ./whatever experiment_name num_threads <name_suffix>";
	exit 1;
fi
set -x
set -e

make -j 8 singlenode_experiment

set +e
./clean_persisted_data.sh 20
#mkdir -p account_databases
#mkdir -p header_hash_database
#mkdir -p block_database

#rm account_databases/data.mdb
#rm account_databases/lock.mdb

#rm header_hash_database/data.mdb
#rm header_hash_database/lock.mdb

#rm block_database/*.block || true

#./clean_offer_lmdbs.sh 10
set -e

output_name="$3/$1_$2"

if [ -z $3 ]; then
	output_name=$1_$2
else
	mkdir -p experiment_results/$3
fi

echo "results output to ${output_name}"

./singlenode_experiment experiment_data/$1 experiment_results/${output_name} $2

echo $?


python3 graph_generation.py single_experiment ${output_name}
exit 0

