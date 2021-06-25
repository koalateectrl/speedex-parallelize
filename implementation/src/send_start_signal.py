
from consensus_api_xdr import *
import socket

def connect_and_send_signal(socket_params):
	with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
		s.connect(socket_params)
		client = ExperimentControlV1.new(s.fileno())
		client.signal_start()

def teardown_node(socket_params):
	with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
		s.connect(socket_params)
		client = ExperimentControlV1.new(s.fileno())
		client.write_measurements()
		client.signal_start()

def connect_validators(num_nodes):
	for i in range(0, num_nodes-1):
		idx = i + 2
		connect_and_send_signal(("10.10.1."+str(idx), 9013))

def start_production():
	connect_and_send_signal(("10.10.1.1", 9013))



def write_validator_measurements(num_nodes):
	for i in range(0, num_nodes-1):
		idx = i + 2
		teardown_node(("10.10.1." + str(idx), 9013))


if __name__ == "__main__":
	import sys
	import argparse
	parser = argparse.ArgumentParser(description="control experiment")
	parser.add_argument("control_option", choices = [
		"connect_validators", 
		"start_production", 
		"teardown" ])
	parser.add_argument("num_validators", type=int, nargs = 1)

	args = parser.parse_args()
	print(args.num_validators)
	if (args.control_option == 'connect_validators'):
		connect_validators(args.num_validators[0])
	elif (args.control_option == 'start_production'):
		start_production()
	elif (args.control_option == 'teardown'):
		write_validator_measurements(args.num_validators[0])
