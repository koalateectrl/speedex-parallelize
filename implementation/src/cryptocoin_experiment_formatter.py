
from cryptocoin_experiment_xdr import *

import csv
import dateutil.parser as date_parser
import datetime


coin_folder = b'coingecko_data/'
end_date = datetime.datetime(2021, 4, 29, 0, 0, 0, 0)
default_length = 500

coins = [b'cake', b'ada', b'bch', b'bnb', b'btc', b'doge', b'dot', b'eos', b'eth', b'etc', b'ht', b'ltc', b'matic', b'trx', b'uni', b'usdt', b'vet', b'xrp', b'zec', b'usdc']

def load_coin(coin_name, length):
	filename = coin_folder + coin_name +b'-usd-max.csv'
	output = {}

	with open(filename, 'r') as csvfile:
		reader = csv.reader(csvfile)
		header_row = True
		for row in reader:
			if header_row:
				print (row)
				header_row = False
				continue
			cur_date = date_parser.parse(row[0], ignoretz = True)
			delta = cur_date - end_date
			output[delta.days] = (float(row[1]), float(row[3]))

	x = Cryptocoin.new()

	for i in range(0, len(coin_name)):
		x.name.push_back(coin_name[i])

	discontinuity = False

	for i in range(0, length):
		idx = i - length
		(price, volume) = (1, 0)
		y = DateSnapshot.new()

		if not idx in output.keys():
			print (idx, coin)
			#print (output.keys())
			if (discontinuity):
				raise ValueError("not enough data in " + str(coin_name))
		else:
			(price, volume) = output[idx]
			discontinuity = True
		y.volume = volume
		y.price = price
		x.snapshots.push_back(y)
	return x

data = CryptocoinExperiment.new()

for coin in coins:
	data.coins.push_back(load_coin(coin, default_length))

data.save_to_file(coin_folder + b'unified_data')