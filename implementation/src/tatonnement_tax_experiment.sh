set -ex

make synthetic_data_gen -j

make sosp_tatonnement_sim -j

#./synthetic_data_gen sosp_price_comp_sim_20.yaml synthetic_data_generator/sosp_tatonnement_sim_params_noseq_20.yaml tax_20assets

FOLDER=junk
DATA_DIR=experiment_data/tax_20assets

mkdir -p ${FOLDER}

git log -1 > ${FOLDER}/git_log


TAX=25
#./sosp_tatonnement_sim ${DATA_DIR} ${FOLDER}/${TAX}_5_results ${TAX} 5
#./sosp_tatonnement_sim ${DATA_DIR} ${FOLDER}/${TAX}_7_results ${TAX} 7
#./sosp_tatonnement_sim ${DATA_DIR} ${FOLDER}/${TAX}_10_results ${TAX} 10
#./sosp_tatonnement_sim ${DATA_DIR} ${FOLDER}/${TAX}_12_results ${TAX} 12
#./sosp_tatonnement_sim ${DATA_DIR} ${FOLDER}/${TAX}_15_results ${TAX} 15
./sosp_tatonnement_sim ${DATA_DIR} ${FOLDER}/${TAX}_17_results ${TAX} 17
./sosp_tatonnement_sim ${DATA_DIR} ${FOLDER}/${TAX}_20_results ${TAX} 20

TAX=27
#./sosp_tatonnement_sim ${DATA_DIR} ${FOLDER}/${TAX}_5_results ${TAX} 5
#./sosp_tatonnement_sim ${DATA_DIR} ${FOLDER}/${TAX}_7_results ${TAX} 7
#./sosp_tatonnement_sim ${DATA_DIR} ${FOLDER}/${TAX}_10_results ${TAX} 10
#./sosp_tatonnement_sim ${DATA_DIR} ${FOLDER}/${TAX}_12_results ${TAX} 12
#./sosp_tatonnement_sim ${DATA_DIR} ${FOLDER}/${TAX}_15_results ${TAX} 15
./sosp_tatonnement_sim ${DATA_DIR} ${FOLDER}/${TAX}_17_results ${TAX} 17
./sosp_tatonnement_sim ${DATA_DIR} ${FOLDER}/${TAX}_20_results ${TAX} 20
