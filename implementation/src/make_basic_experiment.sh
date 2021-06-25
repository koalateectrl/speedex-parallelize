set -ex

make synthetic_data_gen -j

./make_directories.sh

./synthetic_data_gen synthetic_data_generator/synthetic_data_params.yaml basic_experiment_$1
