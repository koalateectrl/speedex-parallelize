import numpy as np
import cvxpy as cp
import os

import experiments_xdr as ex
import block_xdr as blk

def price_to_double(price):
	return float(price) / (2**24)

def run_experiment(n, m, experiment_data, results):

	sell_asset = np.zeros(m, dtype=np.uint)
	buy_asset = np.zeros(m, dtype=np.uint)
	ratios = np.zeros((m, 1))
	endowments = np.zeros((m, 1))


	for i in range(0, m):
		op = experiment_data[i].transaction.operations[0].body.createSellOfferOp
		sell_asset[i] = op.category.sellAsset
		buy_asset[i] = op.category.buyAsset
		ratios[i] = price_to_double(op.minPrice)
		#endowments[i] = 1
		if (ratios[i] < 0.00001):
                	print("funny business")
		#print(ratios[i])
		#print(op.amount)
		endowments[i] = op.amount


	sell_expanded = np.zeros((m, n))
	for i in range(0, m):
		#print(sell_asset[i])
		sell_expanded[i,sell_asset[i]] = 1

	buy_expanded = np.zeros((m, n))
	for i in range(0, m):
		buy_expanded[i, buy_asset[i]] = 1

	sell_endow_expanded = np.zeros((m,n))
	for i in range(0,m):
		sell_endow_expanded[i, sell_asset[i]] = endowments[i]


	ones = np.ones((m, 1))

	y = cp.Variable((m, 1), name="y")

	beta = cp.Variable((m, 1), name = "beta")
	p = cp.Variable((n, 1), name = "price")

	constraints = [
		y >= 0,
		beta >= 0,
		y <= sell_endow_expanded @ p,
		cp.multiply(ratios, beta) <= buy_expanded @ p,
		beta <= sell_expanded @ p, 
		y.T @ buy_expanded == y.T @ sell_expanded,
		p >= 1,
	]

	def get_x_lg_x_over_y(x, y):
		return cp.kl_div(x,y) + x - y

	#print(endowments)

	objective = cp.Minimize(cp.sum(cp.multiply(endowments, get_x_lg_x_over_y(sell_expanded @ p, beta))) - cp.sum(cp.multiply(y, cp.log(ratios))))

	problem = cp.Problem(objective, constraints)
	error = 0.001
	max_iters = 100
	success = False
	while not success:
		try:
			problem.solve(verbose=False, abstol = error, reltol=error, abstol_inacc=error, reltol_inacc=error, max_iters=max_iters)#, feastol = error, feastol_inacc = error)
			print("done solving, first")
			print(problem.status)
			print(problem.value)
			success=True
		except:
			print("had an error!")
			max_iters += 20
			if max_iters > 200:
				print ("hit max iter count")
				return
	print("done solving")
	duration = problem.solver_stats.solve_time
	print("got duration", duration)

	#print((beta.value, y.value, p.value))
	#print((problem.status, problem.value))
	results.results.null_check()
	if (duration is not None):
		print('adding res')
		out = blk.TatonnementMeasurements.new()
		out.runtime = duration
		results.results.push_back(out)
	print("finished")
	#print("done all")
	#print(y.value)
	#print("sell endow exp @ p")
	#print((sell_endow_expanded @ p).value)
	#print(sell_endow_expanded)
	#print((y.T @ buy_expanded).value, (y.T @ sell_expanded).value)
	#print("foo")
	
def run_sosp_experiment(data_directory, outfile_name):
	num_tx_list = [100, 500, 1000, 2000, 5000, 10000, 50000]#, 100000, 500000]#, 1000000]
	
	results_out = ex.PriceComputationExperiment.new()

	num_trials = 5

	params_filename = os.fsencode(data_directory) + b'/params';

	params = ex.ExperimentParameters.new()
	params.load_from_file(params_filename)

	for j in range(0, len(num_tx_list)):
		#print(num_tx_list[j])
		num_txs = num_tx_list[j]
		results = ex.PriceComputationSingleExperiment.new()
		results.num_assets = params.num_assets
		results.tax_rate = params.tax_rate
		results.smooth_mult = params.smooth_mult
		results.num_txs = num_txs
		results.num_trials = num_trials

		results_out.experiments.push_back(results)

	for i in range(0, num_trials):
		block_num = i+1

		tx_filename = os.fsencode(data_directory) + b'/' + os.fsencode(str(block_num)) + b'.txs';
		print(tx_filename)
		experiment_data = ex.ExperimentBlock.new()
		experiment_data.load_from_file(tx_filename)

		for j in range(0, len(num_tx_list)):
			num_txs = num_tx_list[j]
			try:
				print(num_txs)
				run_experiment(params.num_assets, num_txs, experiment_data, results_out.experiments[j])
			except Exception as e:
				print("error when running experiment!")
				print(e)

	results_out.save_to_file(os.fsencode(outfile_name))

if __name__== "__main__":
	import sys
	import argparse
	parser = argparse.ArgumentParser(description = "run cvxpy comparison for sosp paper")
	parser.add_argument("data_directory")
	parser.add_argument("outfile_name")

	args = parser.parse_args()

	run_sosp_experiment(args.data_directory, args.outfile_name)


