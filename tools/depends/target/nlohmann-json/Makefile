include ../../Makefile.include
include NLOHMANN-JSON-VERSION
DEPS = NLOHMANN-JSON-VERSION Makefile ../../download-files.include

ifeq ($(CROSS_COMPILING), yes)
  DEPS += ../../Makefile.include
else
  ROOT_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
endif

# configuration settings
CMAKE_OPTIONS=-DJSON_BuildTests=OFF


include ../../download-files.include
all: .installed-$(PLATFORM)

RETRIEVE_TOOL_FLAGS := -Ls --create-dirs --retry 10 --retry-delay 3

download: $(TARBALLS_LOCATION)/$(ARCHIVE)

LIBDYLIB=$(PLATFORM)/single_include/nlohmann/json.hpp

$(TARBALLS_LOCATION)/$(ARCHIVE):
	cd $(TARBALLS_LOCATION); $(RETRIEVE_TOOL) $(RETRIEVE_TOOL_FLAGS) -o $(TARBALLS_LOCATION)/$(ARCHIVE) $(BASE_URL)/archive/refs/tags/$(VERSION).tar.gz

$(PLATFORM): $(DEPS) | $(TARBALLS_LOCATION)/$(ARCHIVE).$(HASH_TYPE)
	rm -rf $(PLATFORM); mkdir -p $(PLATFORM)
	cd $(PLATFORM); $(ARCHIVE_TOOL) $(ARCHIVE_TOOL_FLAGS) $(TARBALLS_LOCATION)/$(ARCHIVE)
	cd $(PLATFORM); mkdir -p build
	cd $(PLATFORM)/build; $(CMAKE) $(CMAKE_OPTIONS) ..

$(LIBDYLIB): $(PLATFORM)
	$(MAKE) -C $(PLATFORM)/build

.installed-$(PLATFORM): $(LIBDYLIB)
	$(MAKE) -C $(PLATFORM)/build install
	touch $@

clean:
	$(MAKE) -C $(PLATFORM)/build clean
	rm -f .installed-$(PLATFORM)

distclean::
	rm -rf $(PLATFORM) .installed-$(PLATFORM)

