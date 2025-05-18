MAKENAME = INE5424

INTERFACE_NAME:=$(shell ip addr | awk '/state UP/ {print $$2}' | head -n 1 | sed 's/.$$//')

TEST_MODULES = e1 e2 e3
TESTS = e1/e1_communicator_test e1/e1_ping_pong_test e1/e1_load_test e1/e1_latency_test e2/e2_one_to_one_test e2/e2_latency_test e2/e2_throughput_test e2/e2_many_to_many_test e2/e2_many_to_one_test e2/e2_broadcast_test e2/e2_broadcast_neighborhood_test e3/e3_one_pub_sub_test e3/e3_two_subs_one_pub_test
MODULES = ethernet utils

SRC_DIR = src
INCL_DIR = include
TST_DIR = tests/src
BUILD_DIR = build
BIN_DIR = bin

CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -Wpedantic -Werror
LDFLAGS = -lm -lpthread
