set -ex

make synthetic_data_gen -j
make xdrpy_module
make sosp_tatonnement_sim -j



mkdir -p pricecomp_measurements

if [ -z $1 ]; then
	#data generation
	./synthetic_data_gen sosp_price_comp_sim_10.yaml synthetic_data_generator/sosp_tatonnement_sim_params_10.yaml pricecomp_10
	./synthetic_data_gen sosp_price_comp_sim_20.yaml synthetic_data_generator/sosp_tatonnement_sim_params_20.yaml pricecomp_20
	./synthetic_data_gen sosp_price_comp_sim_50.yaml synthetic_data_generator/sosp_tatonnement_sim_params_50.yaml pricecomp_50

fi

if [ -z $2 ]; then

	./sosp_tatonnement_sim experiment_data/pricecomp_10 pricecomp_measurements/tatonnement_10
	./sosp_tatonnement_sim experiment_data/pricecomp_20 pricecomp_measurements/tatonnement_20
	./sosp_tatonnement_sim experiment_data/pricecomp_50 pricecomp_measurements/tatonnement_50
fi

python3 sosp_cvxpy.py experiment_data/pricecomp_10 pricecomp_measurements/cvxpy_10
python3 sosp_cvxpy.py experiment_data/pricecomp_20 pricecomp_measurements/cvxpy_20
python3 sosp_cvxpy.py experiment_data/pricecomp_50 pricecomp_measurements/cvxpy_50


