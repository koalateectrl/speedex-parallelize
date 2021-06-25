if [ "$#" -ne 3 ]; then
	echo "usage: ./whatever experiment_name num_threads name_suffix";
	exit 1;
fi

assets=10

set -ex 

make singlenode_experiment -j 8
make singlenode_validator -j 8
make generate_zeroblock -j 8
make xdrpy_module -j 8

./clean_persisted_data.sh ${assets}

EXPERIMENT_DATA_FILE=experiment_data/$1

DEFAULT_AMOUNT=10000000000

./generate_zeroblock ${EXPERIMENT_DATA_FILE} ${DEFAULT_AMOUNT}

output_name="$3/$1_$2"

if [ -z $3 ]; then
	output_name=$1_$2
else
	mkdir -p experiment_results/$3
fi

echo "processing results output to ${output_name}"

./singlenode_experiment ${EXPERIMENT_DATA_FILE} experiment_results/${output_name}_production $2

./clean_persisted_data.sh ${assets} keep

./generate_zeroblock  ${EXPERIMENT_DATA_FILE} ${DEFAULT_AMOUNT}

./singlenode_validator ${EXPERIMENT_DATA_FILE} experiment_results/${output_name}_validation $2

python3 graph_generation.py production_single ${output_name}_production 
python3 graph_generation.py validation_single ${output_name}_validation



