include Config.mk
include Utils.mk

vpath %.cc $(SRC_DIR)
vpath %.cc $(TST_DIR)

ifdef INCL_DIR
	CXXFLAGS += -I$(INCL_DIR)
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

OBJS = $(patsubst %,$(BUILD_DIR)/%.o,$(MODULES))
TESTS_OBJS = $(patsubst %,$(BUILD_DIR)/%.o,$(TESTS))
TESTS_TARGETS = $(patsubst %,$(BIN_DIR)/%,$(TESTS))

.PHONY: clean test

all: test

test: $(OBJS) $(TESTS_OBJS) $(TESTS_TARGETS)
	@$(run_tests)

$(BIN_DIR)/%: $(BUILD_DIR)/%.o $(OBJS) | $(BIN_DIR)
	@$(link_binary)

$(BUILD_DIR)/%.o: %.cc | $(BUILD_DIR)
	@$(assemble_object)

$(BUILD_DIR) $(BIN_DIR):
	@$(create_dir)

clean:
	@$(clean)
