define link_binary =
	$(CXX) -o $@ $^ $(LDFLAGS)
	echo $(MAKENAME) "Make: binary" $(@F) "linked ("$(^F)")"
endef

define assemble_object =
	$(CXX) -c -o $@ $< $(CXXFLAGS)
	echo $(MAKENAME) "Make: object" $(@F) "assembled ("$(^F)")"
endef

define create_dir =
	if ! [ -d $@ ]; then \
	  mkdir $@;          \
	fi
endef

define clean =
	rm -rf $(BIN_DIR)/* $(BUILD_DIR)/*
	echo $(MAKENAME) "Make: cleansed binary and object files"
endef
