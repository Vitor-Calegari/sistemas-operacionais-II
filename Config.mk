MAKENAME = Car

MAIN = car
MODULES = message observed observer

SRC_DIR = src
INCL_DIR = include
BUILD_DIR = build
BIN_DIR = bin

CXX = g++
CXXFLAGS = -std=c++20 -O0 -g -Wall -Wextra -Wpedantic -Werror
LDFLAGS = -lm
