from cryptocoin_experiment_xdr import *
from experiments_xdr import *

import os

def get_block_volumes(idx, experiment_name, num_assets):
	filename = experiment_name + os.fsencode(str(idx) + ".txs")
	out = GeneratedCoinVolumeSnapshots.new()
	data = ExperimentBlock.new()

	data.load_from_file(filename)

	for i in range(0, num_assets):
		out.coin_volumes.push_back(0)

	for i in range(0, data.size()):
		try:
			op = data[i].transaction.operations[0].body.createSellOfferOp
			amount = op.amount
			sell = op.category.sellAsset
			out.coin_volumes[sell] = out.coin_volumes[sell] + amount

		except:
			continue

	for i in range(0, num_assets):
		print(out.coin_volumes[i])
	return out

exp_name = b'experiment_data/cryptocoin_synth4/'

total = GeneratedExperimentVolumeData.new()

for i in range(1, 501):
	print(i)
	total.snapshots.push_back(get_block_volumes(i, exp_name, 20))

total.save_to_file(b'summarized_gen_volume')
