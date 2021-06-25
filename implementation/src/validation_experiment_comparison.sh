

if [ "$#" -ne 5 ]; then
	echo "usage: ./whatever hostname experiment_name name_suffix min_threads max_threads";
	exit 1;
fi

mkdir -p experiment_results/$3

notesfile=experiment_results/$3/notes

#vim ${notesfile}

echo "\n" >> ${notesfile}

set -x

lscpu > experiment_results/$3/cpuconfig

git log -n 1 > experiment_results/$3/git_log


#sudo smartctl -a /dev/sda > experiment_results/$3/diskconfig

production_names=""
validation_names=""

if [ $5 -ge 1 ] && [ $4 -le 1 ]; then
	./validation_experiment_remote.sh $1 $2 1 $3
	production_names="${production_names} "$3/$2_1_production
	validation_names="${validation_names} "$3/$2_1_validation
fi

if [ $5 -ge 2 ] && [ $4 -le 2 ]; then
	./validation_experiment_remote.sh $1 $2 2 $3
	production_names="${production_names} "$3/$2_2_production
	validation_names="${validation_names} "$3/$2_2_validation
fi

if [ $5 -ge 4 ] && [ $4 -le 4 ]; then
	./validation_experiment_remote.sh $1 $2 4 $3
	production_names="${production_names} "$3/$2_4_production
	validation_names="${validation_names} "$3/$2_4_validation
fi

if [ $5 -ge 8 ] && [ $4 -le 8 ]; then
	./validation_experiment_remote.sh $1 $2 8 $3
	production_names="${production_names} "$3/$2_8_production
	validation_names="${validation_names} "$3/$2_8_validation
fi

if [ $5 -ge 10 ] && [ $4 -le 10 ]; then
	./validation_experiment_remote.sh $1 $2 10 $3
	production_names="${production_names} "$3/$2_10_production
	validation_names="${validation_names} "$3/$2_10_validation
fi

if [ $5 -ge 16 ] && [ $4 -le 16 ]; then
	./validation_experiment_remote.sh $1 $2 16 $3
	production_names="${production_names} "$3/$2_16_production
	validation_names="${validation_names} "$3/$2_16_validation
fi

if [ $5 -ge 20 ] && [ $4 -le 20 ]; then
	./validation_experiment_remote.sh $1 $2 20 $3
	production_names="${production_names} "$3/$2_20_production
	validation_names="${validation_names} "$3/$2_20_validation
fi

if [ $5 -ge 32 ] && [ $4 -le 32 ]; then
	./validation_experiment_remote.sh $1 $2 32 $3
	production_names="${production_names} "$3/$2_32_production
	validation_names="${validation_names} "$3/$2_32_validation
fi

if [ $5 -ge 40 ] && [ $4 -le 40 ]; then
	./validation_experiment_remote.sh $1 $2 40 $3
	production_names="${production_names} "$3/$2_40_production
	validation_names="${validation_names} "$3/$2_40_validation
fi

if [ $5 -ge 64 ] && [ $4 -le 64 ]; then
	./validation_experiment_remote.sh $1 $2 64 $3
	production_names="${production_names} "$3/$2_64_production
	validation_names="${validation_names} "$3/$2_64_validation
fi

if [ $5 -ge 128 ] && [ $4 -le 128 ]; then
	./validation_experiment_remote.sh $1 $2 128 $3
	production_names="${production_names} "$3/$2_128_production
	validation_names="${validation_names} "$3/$2_128_validation
fi

python3 graph_generation.py production_threadcount_comparison ${production_names} --threadcount_comparison_filename=$3/thread_comparison
python3 graph_generation.py validation_threadcount_comparison ${validation_names} --threadcount_comparison_filename=$3/thread_comparison

