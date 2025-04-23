MAKENAME = INE5424

INTERFACE_NAME:=$(shell ip addr | awk '/state UP/ {print $$2}' | head -n 1 | sed 's/.$$//')

TESTS = communicator_test #ping_pong_test load_test api_test
MODULES = ethernet utils

SRC_DIR = src
INCL_DIR = include
TST_DIR = tests/src
BUILD_DIR = build
BIN_DIR = bin

CXX = g++
CXXFLAGS = -O0 -g -std=c++20 -Wall -Wextra -Wpedantic -Werror
LDFLAGS = -lm -lpthread
