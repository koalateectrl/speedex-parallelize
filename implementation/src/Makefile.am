

DATA_X_FILES = xdr/types.x \
	xdr/transaction.x \
	xdr/ledger.x \
	xdr/block.x \
	xdr/database_commitments.x \
	xdr/trie_proof.x \
	xdr/iblt_wire.x \
	xdr/experiments.x \
	xdr/consensus_api.x \
	xdr/cryptocoin_experiment.x \
	xdr/header_summary.x \
	xdr/hello_world_api.x

SERVER_X_FILES = xdr/transaction_submission_api.x xdr/server_control_api.x \
	xdr/state_query_api.x 

SRC_X_FILES = $(DATA_X_FILES) $(SERVER_X_FILES)

EDCE_SRCS = memory_database.cc user_account.cc \
	block_validator.cc  \
	serial_transaction_processor.cc \
	tatonnement_oracle.cc simple_synthetic_data_generator.cc \
	merkle_work_unit.cc merkle_work_unit_manager.cc memory_database_view.cc \
	tatonnement_sim_setup.cc \
	lp_solver.cc \
	transaction_buffer_manager.cc block_builder_manager.cc \
	edce.cc signature_check.cc \
	edce_options.cc proof_utils.cc \
	cleanup.cc lmdb_wrapper.cc \
	account_modification_log.cc block_header_hash_map.cc header_persistence_utils.cc \
	work_unit_state_commitment.cc mempool.cc block_producer.cc \
	edce_node.cc coroutine_throttler.cc rpc/consensus_api.cc \
	consensus_api_server.cc consensus_connection_manager.cc \
	crypto_utils.cc tatonnement_sim_experiment.cc \
	synthetic_data_generator/synthetic_data_gen.cc \
	synthetic_data_generator/synthetic_data_gen_options.cc \
	log_merge_worker.cc file_prealloc_worker.cc \
	signature_check_api_server.cc \
	rpc/hello_world_api.cc hello_world_api_server.cc

TX_GEN_SRCS = tx_generator/account_manager.cc

#EXPERIMENT_SRCS = synthetic_data_generator/synthetic_data_gen_options.cc

#AM_LDFLAGS = -g -rdynamic
AM_CPPFLAGS = $(libcrypto_CFLAGS) $(gsl_CFLAGS) $(xdrpp_CFLAGS) $(libsodium_CFLAGS) $(LIBFYAML_CFLAGS) $(lmdb_CFLAGS)
LDADD = $(libcrypto_LIBS) $(gsl_LIBS) $(xdrpp_LIBS) -ltbb -lglpk $(libsodium_LIBS) $(LIBFYAML_LIBS) $(lmdb_LIBS)

TEST_DIR = tests/
TEST_OUT = test_bin/

TEST_SRCS = test_price_utils.h test_block_processor.h test_account_creation.h \
	test_database_seq_numbers.h test_merkle_trie.h test_merkle_trie_metadata.h \
	test_work_unit.h test_glpk_solver.h test_trie_proofs.h test_iblt.h \
	test_parallel_apply.h test_account_merkle_trie.h

TEST_FILES = $(addprefix $(TEST_DIR), $(TEST_SRCS))

test_runner.cc : $(TEST_FILES) $(SRC_X_FILES:.x=.h)
	cxxtestgen --error-printer -o test_runner.cc $(TEST_FILES)

SRCS = $(EDCE_SRCS) $(TX_GEN_SRCS) #$(EXPERIMENT_SRCS)

$(SRCS:.cc=.o) : $(SRC_X_FILES:.x=.h)

MAIN_CCS = synthetic_data_generator/aberrant_datasets_gen.cc \
	sosp_aberrant_datasets_runner.cc xdr_write_speedtest.cc \
	generate_db_zeroblock.cc singlenode_experiment.cc \
	singlenode_validator.cc edce_producer_experiment.cc \
	edce_validator_experiment.cc sosp_tatonnement_mega_graph.cc \
	sosp_tatonnement_sim.cc signature_check_benchmark.cc \
	performance_test_merkle_trie.cc experiment_controller.cc \
	cvxpy_comparison.cc summarize_headers.cc \
	synthetic_data_generator/cryptocoin_dataset_gen.cc \
	synthetic_data_generator/synthetic_data_gen_from_params.cc \
	tatonnement_sim.cc trie_comparison.cc save_acclog_fast.cc \
	hello_world_controller.cc

$(MAIN_CCS:.cc=.o) : $(SRC_X_FILES:.x=.h)

$(SRC_X_FILES:.x=.h) : $(XDRC)

SUFFIXES = .x .h

.x.h:
	$(XDRC) -hh --hhpy -o $@ $<

%.scaffold : %.x $(XDRC) $(DATA_X_FILES:.x=.h)
	$(XDRC) -serverhh -o $@_h $<
	$(XDRC) -servercc -o $@_cc $<

%.scaffold_async : %.x $(XDRC) $(DATA_X_FILES:.x=.h)
	$(XDRC) -serverhh -a -o $@_h $<
	$(XDRC) -servercc -a -o $@_cc $<

%.xdrpy : %.x $(XDRC) $(DATA_X_FILES)
	$(XDRC) --pxdi $< -o $(basename $@)_includes.pxd
	$(XDRC) --pxd $< -o $(basename $@)_xdr.pxd
	$(XDRC) --pyx $< -o $(basename $@)_xdr.pyx

PYX_FILES = $(DATA_X_FILES:.x=_xdr.pyx)
PXD_FILES = $(DATA_X_FILES:.x=_xdr.pxd)
PXD_INCLUDE_FILES = $(DATA_X_FILES:.x=_includes.pxd)

%_xdr.pyx : %.x $(XDRC) $(DATA_X_FILES)
	$(XDRC) --pyx $< -o $(basename $@).pyx

%_xdr.pxd : %.x $(XDRC) $(DATA_X_FILES)
	$(XDRC) --pxd $< -o $(basename $@).pxd

%_includes.pxd : %.x $(XDRC) $(DATA_X_FILES)
	$(XDRC) --pxdi $< -o $(basename $@).pxd

xdrpy_module: $(XDRC) $(PYX_FILES) $(PXD_FILES) $(PXD_INCLUDE_FILES) $(DATA_X_FILES:.x=.h)
	python3 setup.py build_ext --inplace -j 4 --pyx_files="$(PYX_FILES)"

bin_PROGRAMS = database_speed_test test \
	perftest_work_unit tatonnement_sim gsl_solver \
	cvxpy_comparison \
	tx_gen gen_block integrated_tx_processing_benchmark \
	synthetic_data_gen singlenode_experiment generate_zeroblock \
	singlenode_validator xdr_write_speedtest db_hash_test \
	edce_producer_experiment performance_test_merkle_trie \
	sosp_tatonnement_sim edce_validator_experiment \
	sig_benchmark sosp_mega_graph \
	aberrant_data_gen aberrant_data_runner \
	experiment_controller \
	cryptocoin_data_gen \
	summarize_headers \
	trie_comparison \
	save_acclog_fast \
	hello_world_controller

all-local: xdrpy_module

test_SOURCES = $(EDCE_SRCS) test_runner.cc
database_speed_test_SOURCES = $(EDCE_SRCS) database_speed_test.cc
# random_sim_SOURCES = $(EDCE_SRCS) simple_simulator.cc
perftest_work_unit_SOURCES = $(EDCE_SRCS) performance_test_work_unit.cc
tatonnement_sim_SOURCES = $(EDCE_SRCS) tatonnement_sim.cc
gsl_solver_SOURCES = $(EDCE_SRCS) unmapped_convex_solver.cc
cvxpy_comparison_SOURCES = $(EDCE_SRCS) cvxpy_comparison.cc
# e2e_sim_SOURCES = $(EDCE_SRCS) e2e_sim_main.cc
# singlenode_edce_SOURCES = $(EDCE_SRCS) singlenode_edce.cc
integrated_tx_processing_benchmark_SOURCES = $(EDCE_SRCS) integrated_tx_processing_benchmark.cc
xdr_write_speedtest_SOURCES = $(EDCE_SRCS) xdr_write_speedtest.cc


synthetic_data_gen_SOURCES = $(SRCS) synthetic_data_generator/synthetic_data_gen_from_params.cc
singlenode_experiment_SOURCES = $(SRCS) singlenode_experiment.cc
generate_zeroblock_SOURCES = $(SRCS) generate_db_zeroblock.cc
singlenode_validator_SOURCES = $(SRCS) singlenode_validator.cc
edce_producer_experiment_SOURCES = $(SRCS) edce_producer_experiment.cc
edce_validator_experiment_SOURCES = $(SRCS) edce_validator_experiment.cc

tx_gen_SOURCES = $(SRCS) tx_generator/tx_gen_main.cc
gen_block_SOURCES = $(SRCS) tx_generator/generate_block.cc

db_hash_test_SOURCES = $(SRCS) db_hash_test.cc
performance_test_merkle_trie_SOURCES = $(SRCS) performance_test_merkle_trie.cc
sosp_tatonnement_sim_SOURCES = $(SRCS) sosp_tatonnement_sim.cc

#convex_oracle_test_SOURCES = $(SRCS) convex_oracle_test.cc

sig_benchmark_SOURCES = $(SRCS) signature_check_benchmark.cc

sosp_mega_graph_SOURCES = $(SRCS) sosp_tatonnement_mega_graph.cc

aberrant_data_gen_SOURCES = $(SRCS) synthetic_data_generator/aberrant_datasets_gen.cc
aberrant_data_runner_SOURCES = $(SRCS) sosp_aberrant_datasets_runner.cc
cryptocoin_data_gen_SOURCES = $(SRCS) synthetic_data_generator/cryptocoin_dataset_gen.cc
experiment_controller_SOURCES = $(SRCS) experiment_controller.cc
summarize_headers_SOURCES = $(SRCS) summarize_headers.cc
trie_comparison_SOURCES = $(SRCS) trie_comparison.cc
save_acclog_fast_SOURCES = $(SRCS) save_acclog_fast.cc

hello_world_controller_SOURCES = $(SRCS) hello_world_controller.cc

CLEANFILES = $(SRC_X_FILES:.x=.h) $(SERVER_X_FILES:.x=.scaffold_h) $(SERVER_X_FILES:.x=.scaffold_cc) \
	 $(SERVER_X_FILES:.x=.scaffold_h_async) $(SERVER_X_FILES:.x=.scaffold_cc_async)
