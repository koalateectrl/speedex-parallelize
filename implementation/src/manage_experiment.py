from consensus_api_xdr import *
from experiments_xdr import *
import socket
import time
import os
import sys

def connect_and_send_signal(socket_params):
	with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
		s.connect(socket_params)
		client = ExperimentControlV1.new(s.fileno())
		client.signal_start()

def make_control_socket_params(node_idx):
	return (("10.10.1." + str(node_idx), 9013))

def teardown_node(socket_params):
	with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
		s.connect(socket_params)
		client = ExperimentControlV1.new(s.fileno())
		client.write_measurements();
		#measurements = client.get_measurements(); #get_measurements seems to be bugged at the moment, unclear why
		client.signal_start()
		#return measurements;

def connect_validators(num_nodes):
	for i in range(0, num_nodes-1):
		idx = i + 2
		connect_and_send_signal(("10.10.1."+str(idx), 9013))

def send_signal_to_all(nodes):
	for i in nodes:
		connect_and_send_signal(make_control_socket_params(i))
	#connect_and_send_signal(("10.10.1.1", 9013))


def poll_target(index):
	with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
		s.connect(("10.10.1."+str(index), 9013))
		client = ExperimentControlV1.new(s.fileno())
		while True:
			print("polling node" + str(i) + " for liveness:")
			res = client.is_running()
			if res == 0:
				print("node "+ str(i) + " finished")
				return
			time.sleep(5)



def teardown_experiment(root_folder):
	os.makedirs(root_folder, exist_ok=True)
	for i in [1,2,3,4]:
		teardown_node(make_control_socket_params(i))
		#measurements = teardown_node(make_control_socket_params(i))
		#measurements.save_xdr_to_file(root_folder + os.fsencode("/measurements_node_" + str(i)))


def run_experiment(root_folder):
	connect_validators(4)
	time.sleep(1)
	send_signal_to_all([1])
	poll_target(1)
	teardown_experiment(os.fsencode(root_folder))

if __name__ == '__main__':
	if len(sys.argv) != 2:
		print("usage: <blah> root_folder")
	else:
		print(sys.argv)
		run_experiment(sys.argv[1])

