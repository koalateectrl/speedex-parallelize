

host0="ramseyer@pc04.cloudlab.umass.edu"
host1="ramseyer@pc02.cloudlab.umass.edu"
host2="ramseyer@pc05.cloudlab.umass.edu"
host3="ramseyer@pc03.cloudlab.umass.edu"

experiment_name="giant"
data_gen_yaml="synthetic_data_generator/synthetic_data_params_giant_mempool.yaml"
name_suffix="giant_firsttry"
num_assets="20"
params_yaml="default_params_"${num_assets}".yaml"



workdir="~/xfs/edce/implementation/src"


make_session () {
	tmux new-session -d -s experiment bash
	tmux new-window  bash
	tmux new-window  bash
	tmux new-window  bash
	tmux new-window  bash
	tmux send -t experiment:0.0 "ssh "${host0} C-m
	tmux send -t experiment:1.0 "ssh "${host0} C-m
	tmux send -t experiment:2.0 "ssh "${host1} C-m
	tmux send -t experiment:3.0 "ssh "${host2} C-m
	tmux send -t experiment:4.0 "ssh "${host3} C-m
}


initialize () {
	init_cmd="cd ~/xfs; /opt/init.sh; cd edce/implementation; ./autogen.sh && ./configure && make -j"
	tmux send -t experiment:1.0 "${init_cmd}" C-m
	tmux send -t experiment:2.0 "${init_cmd}" C-m
	tmux send -t experiment:3.0 "${init_cmd}" C-m
	tmux send -t experiment:4.0 "${init_cmd}" C-m
}

repull () {
	repull_cmd="cd ~/xfs/edce/implementation; git pull; make -j"
	tmux send -t experiment:1.0 "${repull_cmd}" C-m
	tmux send -t experiment:2.0 "${repull_cmd}" C-m
	tmux send -t experiment:3.0 "${repull_cmd}" C-m
	tmux send -t experiment:4.0 "${repull_cmd}" C-m
}

gen_synthetic_data () {
	tmux send -t experiment:1.0 "cd "${workdir}"; ./gen_producer_data.sh "${experiment_name}" "${data_gen_yaml}" "${params_yaml} C-m
	tmux send -t experiment:2.0 "cd "${workdir}"; ./gen_validator_data.sh "${experiment_name}" "${data_gen_yaml}" "${params_yaml} C-m
	tmux send -t experiment:3.0 "cd "${workdir}"; ./gen_validator_data.sh "${experiment_name}" "${data_gen_yaml}" "${params_yaml} C-m
	tmux send -t experiment:4.0 "cd "${workdir}"; ./gen_validator_data.sh "${experiment_name}" "${data_gen_yaml}" "${params_yaml} C-m
}

regen_params () {
	regen_cmd="cd "${workdir}"; ./gen_validator_data.sh "${experiment_name}" "${data_gen_yaml}" "${params_yaml}
	tmux send -t experiment:1.0 "cd "${workdir}"; ./gen_validator_data.sh "${experiment_name}" "${data_gen_yaml}" "${params_yaml} C-m
	tmux send -t experiment:2.0 "cd "${workdir}"; ./gen_validator_data.sh "${experiment_name}" "${data_gen_yaml}" "${params_yaml} C-m
	tmux send -t experiment:3.0 "cd "${workdir}"; ./gen_validator_data.sh "${experiment_name}" "${data_gen_yaml}" "${params_yaml} C-m
	tmux send -t experiment:4.0 "cd "${workdir}"; ./gen_validator_data.sh "${experiment_name}" "${data_gen_yaml}" "${params_yaml} C-m

}

keyboard_interrupt () {
	tmux send -t experiment:1.0 C-c
	tmux send -t experiment:2.0 C-c
	tmux send -t experiment:3.0 C-c
	tmux send -t experiment:4.0 C-c
}

send_netstat() {
	tmux send -t experiment:1.0 "netstat | grep 90" C-m
	tmux send -t experiment:2.0 "netstat | grep 90" C-m
	tmux send -t experiment:3.0 "netstat | grep 90" C-m
	tmux send -t experiment:4.0 "netstat | grep 90" C-m
}

start_nodes () {
	if [ "$#" -ne 1 ]; then
		echo "usage: start_nodes num_threads"
		return 1
	fi

	num_threads=$1

	#log_cmd=" 2>&1 | tee log"
	#log_cmd=""

	tmux send -t experiment:1.0 "cd "${workdir}"; ./run_producer_node.sh  "${experiment_name}" "${num_threads}" "${name_suffix}" "${data_gen_yaml}" "${num_assets} C-m
	tmux send -t experiment:2.0 "cd "${workdir}"; ./run_validator_node.sh "${experiment_name}" "${num_threads}" "${name_suffix}" "${data_gen_yaml}" 10.10.1.1 10.10.1.2 "${num_assets} C-m 
	tmux send -t experiment:3.0 "cd "${workdir}"; ./run_validator_node.sh "${experiment_name}" "${num_threads}" "${name_suffix}" "${data_gen_yaml}" 10.10.1.2 10.10.1.3 "${num_assets} C-m
	tmux send -t experiment:4.0 "cd "${workdir}"; ./run_validator_node.sh "${experiment_name}" "${num_threads}" "${name_suffix}" "${data_gen_yaml}" 10.10.1.3 10.10.1.4 "${num_assets} C-m

	return 0
}

run_experiment () {
	output_name=$1
	echo "output going to "${output_name}
	tmux send -t experiment:0.0 "cd "${workdir}"; make experiment_controller -j; ./experiment_controller "${output_name} C-m  #python3 manage_experiment.py "${output_name} C-m
}




#tmux send -t experiment:1.0 "cd edce/implementation/src; ./cloudlab_remote_init.sh $1 './run_producer_node.sh "${experiment_name}" 16 "${name_suffix}" "${data_gen_yaml}"'" C-m
#tmux send -t experiment:2.0 "cd edce/implementation/src; ./cloudlab_remote_init.sh $2 './run_validator_node.sh "${experiment_name}" 16 "${name_suffix}" 10.10.1.1 10.10.1.2'" C-m
#tmux send -t experiment:3.0 "cd edce/implementation/src; ./cloudlab_remote_init.sh $3 './run_validator_node.sh "${experiment_name}" 16 "${name_suffix}" 10.10.1.2 10.10.1.3'" C-m
#tmux send -t experiment:4.0 "cd edce/implementation/src; ./cloudlab_remote_init.sh $4 './run_validator_node.sh "${experiment_name}" 16 "${name_suffix}" 10.10.1.3 10.10.1.4'" C-m
#tmux -2 attach-session -d -t experiment