if [ "$#" -ne 7 ]; then
	echo "usage: ./whatever experiment_name num_threads name_suffix experiment_yaml parent_hostname self_hostname num_assets";
	exit 1;
fi


assets=$7

set -ex 

make edce_validator_experiment -j
make generate_zeroblock -j
make xdrpy_module

make synthetic_data_gen -j

./clean_persisted_data.sh ${assets}

EXPERIMENT_DATA_FILE=experiment_data/$1

#make synthetic_data_gen -j

#./synthetic_data_gen default_params.yaml $4 $1 justparams

DEFAULT_AMOUNT=10000000000

./generate_zeroblock ${EXPERIMENT_DATA_FILE} ${DEFAULT_AMOUNT}


output_folder=$3

mkdir -p experiment_results/$3

lscpu > experiment_results/$3/cpuconfig

num_threads=$2

num_cores=$(( num_threads / 2 ))

echo ${num_threads}
echo ${num_cores}

#cgexec -g cpuset:cpu${num_cores}core 
./edce_validator_experiment ${EXPERIMENT_DATA_FILE} experiment_results/${output_folder} $5 $6 ${num_threads}

