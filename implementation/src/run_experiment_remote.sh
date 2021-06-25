if [ "$#" -lt 3 ]; then
	echo "usage: ./whatever experiment_name num_threads hostname <name_prefix>";
	exit 1;
fi
set -e

ssh  $3 /bin/bash <<EOF
	cd edce/implementation/src
	source set_env.sh
	pwd
	./run_experiment.sh $1 $2 $4 > /dev/null
EOF

output_name="$4/$1_$2"

if [ -z $4 ]; then
	output_name = $1_$2
else
	mkdir -p experiment_results/$4
fi

echo "copying file to ./experiment_results/${output_name}"

scp $3:~/edce/implementation/src/experiment_results/${output_name} ./experiment_results/${output_name}


