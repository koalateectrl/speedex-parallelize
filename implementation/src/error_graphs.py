from experiments_xdr import *
from block_xdr import *

import numpy as np

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import operator
import os

def get_filenames(folder):
	return os.listdir(folder)

def load_results_map(root_folder):
	files = get_filenames(root_folder)

	output_dictionary = {}
	for bfile in files:
		if b"results" not in bfile:
			continue

		file = root_folder + b"/" + bfile
		
		print (file)

		x = PriceComputationExperiment.new()
		x.load_from_file(file)
		for i in range(0, x.experiments.size()):
			exp = x.experiments[i]
			key = (exp.tax_rate, exp.smooth_mult, exp.num_txs)
			if not (key in output_dictionary.keys()):
				output_dictionary[key] = []
			output_dictionary[key].append(PriceComputationSingleExperiment.copy(exp))
	return output_dictionary


def get_taxes(results):
	keys = []
	for key in results.keys():
		keys.append(key[0])

	return set(keys)

def get_smooth_mults(results):
	keys = []
	for key in results.keys():
		keys.append(key[1])

	return set(keys)

def round_getter():
	def method(x):
		return operator.attrgetter("num_rounds")(x)
	return method

def tax_getter():
	def method(x):
		return operator.attrgetter("runtime")(x)
	return method



def plot_tax_attr(smooth_mult, num_txs, results, attrfunc):
	taxes = get_taxes(results)
	taxes = list(taxes)
	taxes.sort()

	xpts = []
	ypts = []

	for tax in taxes:
		key = (tax, smooth_mult, num_txs)
		if (key in results.keys()):
			if (len(results[key]) != 1):
				print(results[key])
				raise ValueError("invalid number of results!? TODO edit if needed")

			local_res = results[key][0]

			if (local_res.results.size() != local_res.num_trials):
				print("some trials timed out!", local_res.results.size(), key)
				continue

			avg_acc = 0
			for i in range(0, local_res.results.size()):
				avg_acc = avg_acc + attrfunc(local_res.results[i])

			if (local_res.results.size() > 0):
				xpts.append(tax)
				ypts.append(avg_acc / local_res.results.size())

	if len(xpts) > 0:
		plt.plot(xpts, ypts, label=str(smooth_mult))
	plt.xlabel("tax_rate")


def plot_tax_rounds(smooth_mult, num_txs, results):
	plot_tax_attr(smooth_mult, num_txs, results, round_getter())

def plot_tax_time(smooth_mult, num_txs, results):
	plot_tax_attr(smooth_mult, num_txs, results, tax_getter())

def plot_smooth_attr(tax, num_txs, results, attrfunc):
	smooth_mults = get_smooth_mults(results)
	smooth_mults = list(smooth_mults)
	smooth_mults.sort()

	xpts = []
	ypts = []

	for smooth_mult in smooth_mults:
		key = (tax, smooth_mult, num_txs)
		if (key in results.keys()):
			if (len(results[key]) != 1):
				print(results[key])
				raise ValueError("invalid number of results!? TODO edit if needed")

			local_res = results[key][0]

			if (local_res.results.size() != local_res.num_trials):
				print("some trials timed out!", local_res.results.size(), key)
				continue

			avg_acc = 0
			for i in range(0, local_res.results.size()):
				avg_acc = avg_acc + attrfunc(local_res.results[i])

			if (local_res.results.size() > 0):
				xpts.append(smooth_mult)
				ypts.append(avg_acc / local_res.results.size())

	if len(xpts) > 0:
		plt.plot(xpts, ypts, label=str(tax))
	plt.xlabel("smooth_mult")

def plot_smooth_rounds(tax, num_txs, results):
	plot_smooth_attr(tax, num_txs, results, round_getter())

def plot_smooth_time(tax, num_txs, results):
	plot_smooth_attr(tax, num_txs, results, tax_getter())


input_folder = "pricecomp_approxcompare_tuned"

total_res = load_results_map(os.fsencode(input_folder))

for num_txs in [500, 1000, 2000, 5000, 10000, 50000, 100000, 500000, 1000000, 5000000, 10000000, 20000000]:
	plt.figure()
	mults = get_smooth_mults(total_res)
	for mult in mults:
		plot_tax_time(mult, num_txs, total_res)
	plt.legend()

	plt.savefig(input_folder + "/smoothmult_runtime_compare" + str(num_txs)+".png")
	plt.close()

for num_txs in [500, 1000, 2000, 5000, 10000, 50000, 100000, 500000, 1000000, 5000000]:
	plt.figure()
	mults = get_smooth_mults(total_res)
	for mult in mults:
		plot_tax_rounds(mult, num_txs, total_res)

	plt.legend()

	plt.savefig(input_folder + "/smoothmult_rounds_compare" + str(num_txs)+".png")
	plt.close()

for num_txs in [500, 1000, 2000, 5000, 10000, 50000, 100000, 500000, 1000000, 5000000]:
	plt.figure()
	taxes = get_taxes(total_res)
	for tax in taxes:
		plot_smooth_time(tax, num_txs, total_res)

	plt.legend()

	plt.savefig(input_folder + "/tax_runtime_compare" + str(num_txs)+".png")
	plt.close()

for num_txs in [500, 1000, 2000, 5000, 10000, 50000, 100000, 500000, 1000000, 5000000]:
	plt.figure()
	taxes = get_taxes(total_res)
	for tax in taxes:
		plot_smooth_rounds(tax, num_txs, total_res)

	plt.legend()

	plt.savefig(input_folder + "/tax_rounds_compare" + str(num_txs)+".png")
	plt.close()






