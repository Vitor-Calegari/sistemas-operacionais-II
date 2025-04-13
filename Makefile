include Config.mk
include Utils.mk

vpath %.cc $(SRC_DIR)
vpath %.cc $(TST_DIR)

ifdef INCL_DIR
	CXXFLAGS += -I$(INCL_DIR)
endif

ifdef INTERFACE_NAME
$(info $(MAKENAME) Make: Selected network interface $(INTERFACE_NAME))
	CXXFLAGS += -DINTERFACE_NAME=\"$(INTERFACE_NAME)\"
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
