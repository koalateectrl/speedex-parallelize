import matplotlib
matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np
import os

import experiments_xdr as ex

from matplotlib.markers import MarkerStyle

output_filename = "price_comparison_graph.tex"

import seaborn as sns

sns.set(rc={'text.usetex' : True})

#input_lines = [
#	("Tâtonnement 10 assets", "sosp_tat_results", "o"),
#	("Convex Solver 10 assets", "cvxpy_results", "+")
#	]

def times_mean(times):
	acc = 0.0
	for i in range(0, times.size()):
		acc = acc + times[i].runtime
	return acc / times.size()


def plot_one_experiment_line(line_name, results_filename, mark, fill, color):
	print(results_filename)
	results = ex.PriceComputationExperiment.new()
	results.load_from_file(os.fsencode(results_filename))
	meanx = []
	meany = []

	xvals = []
	yvals = []
	for i in range(0, results.experiments.size()):
	#for key in data.keys():
		num_txs = results.experiments[i].num_txs
		res = results.experiments[i].results
		if (res.size() > 0):
			meanx.append(num_txs)
			meany.append(times_mean(res))
		if (res.size() != results.experiments[i].num_trials):
			print ("timeouts:", (results.experiments[i].num_trials - res.size()))
		for j in range(0, res.size()):
			xvals.append(num_txs)
			yvals.append(res[j].runtime)
		#	print(key, vals[i])
	#plt.scatter(xvals, yvals, marker=MarkerStyle(mark, fillstyle=fill), color=color)
	sns.lineplot(meanx, meany, marker=MarkerStyle(mark, fillstyle=fill), color=color, label=line_name, markersize=10, edgecolor="black")

	#plt.scatter(meanx, meany, mark=mark)

plot_one_experiment_line("CVXPY, 10 Assets", "pricecomp_measurements/cvxpy_10", "D", "left", "tab:blue")
#plot_one_experiment_line("cvxpy 20", "pricecomp_measurements/cvxpy_20", "D", "right", "tab:blue")
#plot_one_experiment_line("cvxpy 50", "pricecomp_measurements/cvxpy_50", "D", "full", "tab:blue")
plot_one_experiment_line("T\^atonnement, 10 Assets", "pricecomp_measurements/tatonnement_10", "o", "left", "tab:red")
plot_one_experiment_line("T\^atonnement, 20 Assets", "pricecomp_measurements/tatonnement_20", "o", "right", "tab:red")
plot_one_experiment_line("T\^atonnement, 50 Assets", "pricecomp_measurements/tatonnement_50", "o", "full", "tab:red")
#for (legend_name, filename, mark) in input_lines:
#	plot_one_experiment_line(legend_name, filename, mark)

plt.xscale('log')
plt.yscale('log')
plt.axis([500,20000000,0.001, 300])
plt.xlabel("Number of Offers")
plt.ylabel("Time (s)")

plt.legend()


import tikzplotlib
tikzplotlib.save(output_filename)

plt.savefig("out.png")