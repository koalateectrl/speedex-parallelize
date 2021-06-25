if [ "$#" -ne 5 ]; then
	echo "usage: ./whatever experiment_name num_threads name_suffix data_gen_yaml num_assets";
	exit 1;
fi


assets=$5

set -ex 

make edce_producer_experiment -j
make generate_zeroblock -j
make xdrpy_module

./clean_persisted_data.sh ${assets}

EXPERIMENT_DATA_FILE=experiment_data/$1

DEFAULT_AMOUNT=10000000000

#make synthetic_data_gen -j

#./synthetic_data_gen default_params.yaml $4 $1

./generate_zeroblock ${EXPERIMENT_DATA_FILE} ${DEFAULT_AMOUNT}


output_folder=$3

mkdir -p experiment_results/$3

lscpu > experiment_results/$3/cpuconfig


num_threads=$2
num_cores=$(( num_threads / 2 ))

echo ${num_threads}
echo ${num_cores}

#cgexec -g cpuset:cpu${num_cores}core 
./edce_producer_experiment ${EXPERIMENT_DATA_FILE} experiment_results/${output_folder} ${num_threads} 

make summarize_headers -j

echo "summary folder is summary_"$3

./summarize_headers databases/header_database/ experiment_results/${output_folder}/results summary_$3
