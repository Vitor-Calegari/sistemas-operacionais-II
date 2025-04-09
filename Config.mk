MAKENAME = Car

TESTS = ordered_list_test communicator_test api_test load_test
MODULES = engine ethernet message utils

SRC_DIR = src
INCL_DIR = include
TST_DIR = tests/src
BUILD_DIR = build
BIN_DIR = bin

CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -Wpedantic -Werror
LDFLAGS = -lm -lpthread
