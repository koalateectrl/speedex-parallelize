mkdir experiment_data
mkdir experiment_results

make xdrpy_module

ulimit -n 10240 -S

make synthetic_data_gen

