if [ "$#" -ne 3 ]; then
	echo "usage: ./whatever experiment_name num_threads name_suffix";
	exit 1;
fi

assets=20

set -ex 

make singlenode_validator -j 8
make generate_zeroblock -j 8
make xdrpy_module -j 8

EXPERIMENT_DATA_FILE=experiment_data/$1

DEFAULT_AMOUNT=10000000000

#./clean_persisted_data.sh ${assets} keep

#./generate_zeroblock ${EXPERIMENT_DATA_FILE} ${DEFAULT_AMOUNT}

./singlenode_validator ${EXPERIMENT_DATA_FILE} experiment_results/${output_name}_validation $2



