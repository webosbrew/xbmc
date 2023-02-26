include ../../Makefile.include WEBOS-USERLAND-VERSION ../../download-files.include
DEPS= ../../Makefile.include Makefile WEBOS-USERLAND-VERSION ../../download-files.include

# configuration settings
CMAKE_OPTIONS=-DBUILD_SHARED_LIBS=ON \
              -DENABLE_TESTS=OFF \
              -DENABLE_DOCUMENTATION=OFF \
              -DENABLE_UTILS=OFF \
              -DENABLE_EXAMPLES=OFF

LIBDYLIB=$(PLATFORM)/build/libEGL.so

all: .installed-$(PLATFORM)

RETRIEVE_TOOL_FLAGS := -Ls --create-dirs --retry 10 --retry-delay 3

download: $(TARBALLS_LOCATION)/$(ARCHIVE)

$(TARBALLS_LOCATION)/$(ARCHIVE):
	cd $(TARBALLS_LOCATION); $(RETRIEVE_TOOL) $(RETRIEVE_TOOL_FLAGS) -o $(TARBALLS_LOCATION)/$(ARCHIVE) $(BASE_URL)/archive/refs/tags/$(VERSION).tar.gz

$(PLATFORM): $(DEPS) | $(TARBALLS_LOCATION)/$(ARCHIVE).$(HASH_TYPE)
	-rm -rf $(PLATFORM); mkdir -p $(PLATFORM)
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
