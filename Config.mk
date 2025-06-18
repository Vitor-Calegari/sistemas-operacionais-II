MAKENAME = INE5424

INTERFACE_NAME:=$(shell ip addr | awk '/state UP/ {print $$2}' | head -n 1 | sed 's/.$$//')

TEST_MODULES = e1 e2 e3 e4 e5 e6
TESTS = e1/e1_communicator_test e1/e1_load_test e1/e1_latency_test e2/e2_one_to_one_test e2/e2_latency_test e2/e2_throughput_test e2/e2_many_to_many_test e2/e2_many_to_one_test e2/e2_broadcast_test e3/e3_one_pub_sub_test e3/e3_one_pub_many_subs_test e3/e3_already_running_test e3/e3_response_time_test e3/e3_many_pubs_subs_test e3/e3_unsubscribe_test e4/e4_components_same_car e4/e4_components_many_cars e4/e4_send_time_test e4/e4_one_to_one_time_test e5/e5_quadrant_test e5/e5_validate_mac_test e5/e5_drop_test e5/e5_out_of_range_test e5/e5_diff_quadrant_test e6/e6_shared_mem_test e6/e6_socket_test
MODULES = ethernet shared_mem utils mac

SRC_DIR = src
INCL_DIR = include
TST_SRC_DIR = tests/src
TST_INCL_DIR = tests/include
BUILD_DIR = build
BIN_DIR = bin

CXX = g++
CXXFLAGS = -g -std=c++20 -Wall -Wextra -Wpedantic -Werror
LDFLAGS = -lm -lpthread -lssl -lcrypto
