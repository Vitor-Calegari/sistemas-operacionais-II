include Config.mk
include Utils.mk

vpath %.cc $(SRC_DIR)
vpath %.cc $(TST_DIR)

ifdef INCL_DIR
	CXXFLAGS += -I$(INCL_DIR)
endif

OBJS = $(patsubst %,$(BUILD_DIR)/%.o,$(MAIN) $(MODULES))

.PHONY: clean

$(BIN_DIR)/$(MAIN): $(OBJS) | $(BIN_DIR)
	@$(link_binary)

$(BUILD_DIR)/%.o: %.cc | $(BUILD_DIR)
	@$(assemble_object)

$(BUILD_DIR) $(BIN_DIR):
	@$(create_dir)

clean:
	@$(clean)
