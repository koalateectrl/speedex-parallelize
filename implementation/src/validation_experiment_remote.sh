if [ "$#" -ne 4 ]; then
	echo "usage: ./whatever hostname experiment_name num_threads name_suffix";
	exit 1;
fi

set -ex

output_name="$4/$2_$3"

if [ -z $4 ]; then
	output_name=$2_$3
else
	mkdir -p experiment_results/$4
fi

./run_script_remote.sh "./validation_experiment.sh $2 $3 $4" $1 ${output_name}_production ${output_name}_validation

python3 graph_generation.py production_single ${output_name}_production 
python3 graph_generation.py validation_single ${output_name}_validation


