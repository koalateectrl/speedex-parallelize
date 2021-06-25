if [ "$#" -ne 3 ]; then
	echo "usage: ./whatever experiment_name data_gen_yaml params_yaml";
	exit 1;
fi

make synthetic_data_gen -j

./synthetic_data_gen $3 $2 $1