from experiments_xdr import *

import numpy as np

import matplotlib
matplotlib.use("agg")
import matplotlib.pyplot as plt

import operator

import os

def load_production_results(filename):
	try:
		x = ExperimentResults.new()
		x.load_from_file(filename)
		return x
	except:
		print ("failed to load \"" + str(filename) + "\"")
		raise

def load_validation_results(filename):
	x = ExperimentValidationResults.new();
	x.load_from_file(filename)
	return x

def params_txtbox(params):
	string = '\n'.join(
			[r'$\mathrm{num~assets}$=%d' %(params.num_assets,),
			r'$\mathrm{tax~rate}$=%d' %(params.tax_rate,),
			r'$\mathrm{smooth~mult}$=%d' % (params.smooth_mult,),
			r'$\mathrm{num~threads}$=%d' % (params.num_threads,),
			r'$\mathrm{num~accounts}$=%d' % (params.num_accounts,)]
		)
	plt.text(0.5, 0, string, fontsize=12, transform=plt.gcf().transFigure)
	#plt.subplots_adjust(right=0.3) 


def attr_func(name):
	def method(x):
		return operator.attrgetter(name)(x)
	return method

def plot_measurement_over_rounds(func, experiment, name):
	data = []
	for i in range(0, experiment.block_results.size()):
		data.append(func(experiment.block_results[i]))
	plt.plot(data, label = name)

def plot_ratio_over_rounds(func, exp1, exp2, name):
	data = []
	for i in range(0, exp1.block_results.size()):
		data.append(func(exp1.block_results[i]) / func(exp2.block_results[i]))
	plt.plot(data, label = name)

def plot_ratio_smoothed(func, exp1, exp2, name):
	ratio = 1.0
	data = []
	for i in range(0, exp1.block_results.size()):
		latest = func(exp1.block_results[i]) / func(exp2.block_results[i])
		ratio = (ratio + latest) / 2
		data.append(ratio)
	plt.plot(data, label = (name + "_smoothed"))

def plot_baseline_ratio(length):
	data = []
	for i in range(0, length):
		data.append(1.0)
	plt.plot(data, label = "baseline")

def plot_block_measurements_over_rounds(experiment):
	plot_measurement_over_rounds(
		attr_func("block_creation_measurements.block_building_time"), 
		experiment, 
		"block_building_time")
	plot_measurement_over_rounds(
		attr_func("block_creation_measurements.initial_account_db_commit_time"), 
		experiment, 
		"initial_account_db_commit_time")
	plot_measurement_over_rounds(
		attr_func("block_creation_measurements.initial_offer_db_commit_time"), 
		experiment, 
		"initial_offer_db_commit_time")
	plot_measurement_over_rounds(
		attr_func("block_creation_measurements.tatonnement_time"), 
		experiment, 
		"tatonnement_time")
	plot_measurement_over_rounds(
		attr_func("block_creation_measurements.lp_time"), 
		experiment, 
		"lp_time")
	plot_measurement_over_rounds(
		attr_func("block_creation_measurements.clearing_check_time"), 
		experiment, 
		"clearing_check_time")
	plot_measurement_over_rounds(
		attr_func("block_creation_measurements.offer_clearing_time"), 
		experiment, 
		"offer_clearing_time")
	plot_measurement_over_rounds(
		attr_func("block_creation_measurements.db_validity_check_time"), 
		experiment, 
		"db_validity_check_time")
	plot_measurement_over_rounds(
		attr_func("block_creation_measurements.final_commit_time"), 
		experiment, 
		"final_commit_time")

def plot_overhead_time_over_rounds(experiment):
	plot_measurement_over_rounds(
		attr_func("state_commitment_time"),
		experiment, 
		"state_commitment_time")
	plot_measurement_over_rounds(
		attr_func("format_time"),
		experiment, 
		"format_time")

def plot_persistence_over_rounds(experiment):
	plot_measurement_over_rounds(
		attr_func("data_persistence_measurements.wait_for_persist_time"),
		experiment, 
		"wait_for_persist_time")
	plot_measurement_over_rounds(
		attr_func("data_persistence_measurements.header_write_time"),
		experiment, 
		"header_write_time")
	plot_measurement_over_rounds(
		attr_func("data_persistence_measurements.account_db_checkpoint_time"),
		experiment, 
		"account_db_checkpoint_time")
	plot_measurement_over_rounds(
		attr_func("data_persistence_measurements.account_db_checkpoint_sync_time"),
		experiment, 
		"account_db_checkpoint_sync_time")
	plot_measurement_over_rounds(
		attr_func("data_persistence_measurements.account_db_checkpoint_finish_time"),
		experiment, 
		"account_db_checkpoint_finish_time")
	plot_measurement_over_rounds(
		attr_func("data_persistence_measurements.offer_checkpoint_time"),
		experiment, 
		"offer_checkpoint_time")
	plot_measurement_over_rounds(
		attr_func("data_persistence_measurements.account_log_write_time"),
		experiment, 
		"account_log_write_time")
	plot_measurement_over_rounds(
		attr_func("data_persistence_measurements.block_hash_map_checkpoint_time"),
		experiment, 
		"block_hash_map_checkpoint_time")

def plot_validation_over_rounds(experiment):
	plot_measurement_over_rounds(
		attr_func("block_validation_measurements.clearing_param_check"),
		experiment,
		"clearing_param_check")
	plot_measurement_over_rounds(
		attr_func("block_validation_measurements.tx_validation_time"),
		experiment,
		"tx_validation_time")
	plot_measurement_over_rounds(
		attr_func("block_validation_measurements.tentative_commit_time"),
		experiment,
		"tentative_commit_time")
	plot_measurement_over_rounds(
		attr_func("block_validation_measurements.check_workunit_validation_time"),
		experiment,
		"check_workunit_validation_time")
	plot_measurement_over_rounds(
		attr_func("block_validation_measurements.get_dirty_account_time"),
		experiment,
		"get_dirty_account_time")
	plot_measurement_over_rounds(
		attr_func("block_validation_measurements.db_tentative_commit_time"),
		experiment,
		"db_tentative_commit_time")
	plot_measurement_over_rounds(
		attr_func("block_validation_measurements.workunit_hash_time"),
		experiment,
		"workunit_hash_time")
	plot_measurement_over_rounds(
		attr_func("block_validation_measurements.hash_time"),
		experiment,
		"hash_time (acct log)")
	plot_measurement_over_rounds(
		attr_func("block_validation_measurements.db_finalization_time"),
		experiment,
		"db_finalization_time")
	plot_measurement_over_rounds(
		attr_func("block_validation_measurements.workunit_finalization_time"),
		experiment,
		"workunit_finalization_time")
	plot_measurement_over_rounds(
		attr_func("block_validation_measurements.account_log_finalization_time"),
		experiment,
		"account_log_finalization_time")
	plot_measurement_over_rounds(
		attr_func("block_validation_measurements.header_map_finalization_time"),
		experiment,
		"header_map_finalization_time")


def plot_validation_overall_measurements_over_rounds(experiment):
	plot_measurement_over_rounds(
		attr_func("block_load_time"),
		experiment,
		"block_load_time")
	plot_measurement_over_rounds(
		attr_func("total_persistence_time"),
		experiment,
		"total_persistence_time")
	plot_measurement_over_rounds(
		attr_func("validation_logic_time"),
		experiment,
		"validation_logic_time")

def plot_validation_tx_details_over_rounds(experiment):
	plot_measurement_over_rounds(
		attr_func("block_validation_measurements.tx_validation_trie_merge_time"),
		experiment,
		"tx_validation_trie_merge_time")
	plot_measurement_over_rounds(
		attr_func("block_validation_measurements.tx_validation_processing_time"),
		experiment,
		"tx_validation_processing_time")
	plot_measurement_over_rounds(
		attr_func("block_validation_measurements.tx_validation_offer_merge_time"),
		experiment,
		"tx_validation_offer_merge_time")

def make_block_measurements_plot(experiment, figname):
	plt.figure()
	plot_block_measurements_over_rounds(experiment)
	plt.legend()
	plt.ylabel("seconds")
	plt.xlabel("block number")
	params_txtbox(experiment.params)
	plt.savefig(figname)
	plt.close()

def make_overhead_measurements_plot(experiment, figname):
	plt.figure()
	plot_overhead_time_over_rounds(experiment)
	plt.legend()
	plt.ylabel("seconds")
	plt.xlabel("block number")
	params_txtbox(experiment.params)
	plt.savefig(figname)
	plt.close()

def make_persistence_measurements_plot(experiment, figname):
	plt.figure()
	plot_persistence_over_rounds(experiment)
	plt.legend()
	plt.ylabel("seconds")
	plt.xlabel("block number")
	params_txtbox(experiment.params)
	plt.savefig(figname)
	plt.close()

def make_validation_measurements_plot(experiment, figname):
	plt.figure()
	plot_validation_over_rounds(experiment)
	plt.legend()
	plt.ylabel("seconds")
	plt.xlabel("block number")
	params_txtbox(experiment.params)
	plt.savefig(figname)
	plt.close()

def make_validation_tx_details_plot(experiment, figname):
	plt.figure()
	plot_validation_tx_details_over_rounds(experiment)
	plt.legend()
	plt.ylabel("seconds")
	plt.xlabel("block number")
	params_txtbox(experiment.params)
	plt.savefig(figname)
	plt.close()

def make_validation_overall_measurements_plot(experiment, figname):
	plt.figure()
	plot_validation_overall_measurements_over_rounds(experiment)
	plt.legend()
	plt.ylabel("seconds")
	plt.xlabel("block number")
	params_txtbox(experiment.params)
	plt.savefig(figname)
	plt.close()

def get_tx_processing_pts(attribute, block_results):
	output = []
	for i in range(0, block_results.processing_measurements.size()):
		output.append(
			operator.attrgetter(attribute)(block_results.processing_measurements[i]))
	return output

def make_tx_processing_scatterplot_oneattr(experiment, attribute_name, color):
	xvals = []
	yvals = []
	for i in range(0, experiment.block_results.size()):
		new_yvals = get_tx_processing_pts(attribute_name, experiment.block_results[i])

		#if (len(new_yvals) != experiment.params.num_threads):
		#	print ("warning: mismatch between num new yvals and num threads")

		xvals = xvals + ([i] * len(new_yvals));
		yvals = yvals + get_tx_processing_pts(attribute_name, experiment.block_results[i])
	plt.scatter(xvals, yvals, c=color, label = attribute_name)

def make_tx_processing_scatterplot(experiment, figname):
	plt.figure()
	make_tx_processing_scatterplot_oneattr(experiment, "process_time", "tab:red")
	make_tx_processing_scatterplot_oneattr(experiment, "finish_time", "tab:blue")
	plt.legend()
	plt.ylabel("seconds")
	plt.xlabel("block number")
	params_txtbox(experiment.params)
	plt.savefig(figname)
	plt.close()

results_folder = b'experiment_results/'

def make_production_plots(name):
	x = load_production_results(results_folder + name)
	make_block_measurements_plot(x, results_folder + name + b"_block_measurements.png")
	make_tx_processing_scatterplot(x, results_folder + name + b"_tx_processing.png")
	make_overhead_measurements_plot(x, results_folder + name + b"_overhead.png")
	make_persistence_measurements_plot(x, results_folder + name + b"_persistence.png")

def make_validation_plots(name):
	x = load_validation_results(results_folder + name)
	make_validation_measurements_plot(x, results_folder + name + b"_validation_measurements.png")
	make_persistence_measurements_plot(x, results_folder + name + b"_validation_persistence.png")
	make_validation_tx_details_plot(x, results_folder + name + b"_validation_tx_details.png")
	make_validation_overall_measurements_plot(x, results_folder + name + b"_validation_overall_measurements.png")

def same_params_ignoring_threadcount(params1, params2):
	if params1.tax_rate != params2.tax_rate:
		return False
	if params1.smooth_mult != params2.smooth_mult:
		return False
	if params1.num_assets != params2.num_assets:
		return False
	if params1.num_accounts != params2.num_accounts:
		return False
	return True

def make_comparison_plot(experiments, filenames, attribute, figname, params_check_fn):
	plt.figure()
	for i in range(0, len(experiments)):
		plot_measurement_over_rounds(
			attr_func(attribute), 
			experiments[i], 
			filenames[i])
		if not params_check_fn(experiments[i].params, experiments[0].params):
			raise RuntimeError("experiment params differ")
	plt.legend()
	plt.ylabel("seconds")
	plt.xlabel("block number")
	params_txtbox(experiments[0].params)
	print("made figure %s" % figname)
	plt.savefig(figname)
	plt.close()

def load_production_experiments(filenames):
	experiments = []
	for i in range(0, len(filenames)):
		experiments.append(load_production_results(results_folder + os.fsencode(filenames[i])))
	return experiments

def load_validation_experiments(filenames):
	experiments = []
	for i in range(0, len(filenames)):
		experiments.append(load_validation_results(results_folder + os.fsencode(filenames[i])))
	return experiments


def make_thread_comparison_plot(experiments, filenames, figname_root, attribute):
	make_comparison_plot(experiments, filenames, attribute, results_folder + os.fsencode(figname_root + "_" + attribute.replace(".", "_") + ".png"), same_params_ignoring_threadcount)

def make_production_thread_comparison_plots(filenames, figname_root):

	attributes = [
		"data_persistence_measurements.offer_checkpoint_time", 
		"data_persistence_measurements.account_log_write_time",
		"block_creation_measurements.offer_clearing_time",
		"block_creation_measurements.block_building_time",
		"total_time",
		"state_commitment_time",
		"production_hashing_measurements.db_state_commitment_time",
		"production_hashing_measurements.work_unit_commitment_time",
		"production_hashing_measurements.account_log_hash_time"]
	experiments = load_production_experiments(filenames)
	for attr in attributes:
		make_thread_comparison_plot(experiments, filenames, figname_root, attr)

def make_validation_thread_comparison_plots(filenames, figname_root):

	attributes = [
		"total_time",
		"data_persistence_measurements.account_log_write_time",
		"block_validation_measurements.tx_validation_time",
		"block_validation_measurements.db_tentative_commit_time",
		"block_validation_measurements.hash_time",
		"block_validation_measurements.account_log_finalization_time",
		"block_validation_measurements.check_workunit_validation_time",
		"block_validation_measurements.workunit_finalization_time"]
	experiments = load_validation_experiments(filenames)
	for attr in attributes:
		make_thread_comparison_plot(experiments, filenames, figname_root, attr)


def compare_validation_experiments(filenames, query_prop):
	experiments = load_validation_experiments(filenames)
	for i in range(0, len(filenames)):
		plot_measurement_over_rounds(
			attr_func(query_prop),
			experiments[i],
			filenames[i])
def compare_production_experiments(filenames, query_prop):
	experiments = load_production_experiments(filenames)
	for i in range(0, len(filenames)):
		plot_measurement_over_rounds(
			attr_func(query_prop),
			experiments[i],
			filenames[i])

def make_validation_comparison_plot(root_experiments, experiment_sub, query_prop):
	plt.figure()
	
	expnames = []
	for root in root_experiments:
		expnames.append(root + b"/" + experiment_sub)

	compare_validation_experiments(expnames, query_prop)

	plt.legend()
	plt.ylabel("seconds")
	plt.xlabel("block number")
	figname = os.fsencode("experiment_comparisons/comparison_" + query_prop.replace(".", "_") + ".png")
	plt.savefig(figname)
	plt.close()

def make_production_comparison_plot(root_experiments, experiment_sub, query_prop):
	plt.figure()
	
	expnames = []
	for root in root_experiments:
		expnames.append(root + b"/" + experiment_sub)

	compare_production_experiments(expnames, query_prop)

	plt.legend()
	plt.ylabel("seconds")
	plt.xlabel("block number")
	figname = os.fsencode("experiment_comparisons/comparison_" + query_prop.replace(".", "_") + ".png")
	plt.savefig(figname)
	plt.close()

def ratio_production_experiments(exp1name, exp2name, query_prop):
	plt.figure()

	exp1 = load_production_results(results_folder+ b"/"+  exp1name)
	exp2 = load_production_results(results_folder+ b"/"+exp2name)

	plot_ratio_over_rounds(attr_func(query_prop), exp1, exp2, query_prop)
	plot_ratio_smoothed(attr_func(query_prop), exp1, exp2, query_prop)
	plot_baseline_ratio(exp1.block_results.size())

	plt.ylabel("ratio")
	plt.xlabel("block number")
	figname = os.fsencode("experiment_comparisons/ratio_" + query_prop.replace(".", "_") + ".png")
	plt.legend()
	plt.savefig(figname)
	plt.close()

def make_production_ratio_plot(root1, root2, experiment_sub, query_prop):
	ratio_production_experiments(root1 + b"/" + experiment_sub, root2 + b"/" + experiment_sub, query_prop)


def ratio_validation_experiments(exp1name, exp2name, query_prop):
	plt.figure()

	exp1 = load_validation_results(results_folder+ b"/"+  exp1name)
	exp2 = load_validation_results(results_folder+ b"/"+exp2name)

	plot_ratio_over_rounds(attr_func(query_prop), exp1, exp2, query_prop)
	plot_ratio_smoothed(attr_func(query_prop), exp1, exp2, query_prop)
	plot_baseline_ratio(exp1.block_results.size())

	plt.ylabel("ratio")
	plt.xlabel("block number")
	figname = os.fsencode("experiment_comparisons/ratio_" + query_prop.replace(".", "_") + ".png")
	plt.legend()
	plt.savefig(figname)
	plt.close()

def make_validation_ratio_plot(root1, root2, experiment_sub, query_prop):
	ratio_validation_experiments(root1 + b"/" + experiment_sub, root2 + b"/" + experiment_sub, query_prop)


if __name__ == "__main__":
	import sys
	import argparse
	parser = argparse.ArgumentParser(description="Generate graphs")
	parser.add_argument("graph", choices = [
		"production_single", 
		"validation_single", 
		"production_threadcount_comparison", 
		"validation_threadcount_comparison" ])
	parser.add_argument("results_files", nargs = "+")
	parser.add_argument("--threadcount_comparison_filename", default = "thread_comparison")

	args = parser.parse_args()

	if (args.graph == 'production_single'):
		for file in args.results_files:
			make_production_plots(os.fsencode(file))
	elif(args.graph == 'validation_single'):
		for file in args.results_files:
			make_validation_plots(os.fsencode(file))
	elif args.graph == 'production_threadcount_comparison':
		make_production_thread_comparison_plots(args.results_files, args.threadcount_comparison_filename+"_production")
	elif args.graph == 'validation_threadcount_comparison':
		make_validation_thread_comparison_plots(args.results_files, args.threadcount_comparison_filename+"_validation")
