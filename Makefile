include Config.mk
include Utils.mk

vpath %.cc $(SRC_DIR)
vpath %.cc $(TST_SRC_DIR)

ifdef INCL_DIR
	CXXFLAGS += -I$(INCL_DIR)
endif

ifdef TST_INCL_DIR
	CXXFLAGS += -I$(TST_INCL_DIR)
endif

ifdef INTERFACE_NAME
$(info $(MAKENAME) Make: selected network interface $(INTERFACE_NAME))
	CXXFLAGS += -DINTERFACE_NAME=\"$(INTERFACE_NAME)\"
else
$(info $(MAKENAME) Make: selected loopback as network interface)
$(info !!!!!!!!!!!!!!!!!!!!!)
$(info $(MAKENAME) Make: loopback duplicates the number of receives,)
$(info thus causing some tests to have incorrect behavior.)
$(info If possible, have a non-loopback active network interface.)
$(info !!!!!!!!!!!!!!!!!!!!!)
endif

TEST_BUILD_DIRS = $(patsubst %,$(BUILD_DIR)/%,$(TEST_MODULES))
TEST_BIN_DIRS = $(patsubst %,$(BIN_DIR)/%,$(TEST_MODULES))

OBJS = $(patsubst %,$(BUILD_DIR)/%.o,$(MODULES))
TEST_OBJS = $(patsubst %,$(BUILD_DIR)/%.o,$(TESTS))
TEST_TARGETS = $(patsubst %,$(BIN_DIR)/%,$(TESTS))

.PHONY: clean test

all: test

test: $(OBJS) $(TEST_OBJS) $(TEST_TARGETS)
	@$(run_tests)

$(BIN_DIR)/%: $(BUILD_DIR)/%.o $(OBJS) | $(BIN_DIR) $(TEST_BIN_DIRS)
	@$(link_binary)

$(BUILD_DIR)/%.o: %.cc | $(BUILD_DIR) $(TEST_BUILD_DIRS)
	@$(assemble_object)

$(BUILD_DIR) $(TEST_BUILD_DIRS) $(BIN_DIR) $(TEST_BIN_DIRS):
	@$(create_dir)

clean:
	@$(clean)
