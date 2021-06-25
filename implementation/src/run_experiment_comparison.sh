
if [ "$#" -lt 3 ]; then
	echo "usage: ./whatever experiment_name max_num_threads name_prefix <run local?>";
	exit 1;
fi

REMOTE_HOST="ramseyer@amd022.utah.cloudlab.us" #"geoff@howard.scs.stanford.edu"


ssh -t ${REMOTE_HOST} "/bin/bash  --init-file <(echo \"cd edce/implementation/src;pwd;git pull\")" </dev/tty



expnames=""
for ((i=1;i<=$2;i++));
do
	output_name="$3/$1_$i"

	if [ -z $3 ]; then
		output_name = $1_$i
	else
		mkdir -p $3
	fi

	expnames="${expnames} ${output_name}"
	if [ -z $4 ]; then
	#	echo "donothing"
		./run_experiment_remote.sh $1 $i ${REMOTE_HOST} $3
	#	./run_experiment.sh $1 $i $3
	fi
done
python3 graph_generation.py single_experiment ${expnames}
python3 graph_generation.py threadcount_comparison ${expnames} --threadcount_comparison_filename="$3/thread_comparison"

	