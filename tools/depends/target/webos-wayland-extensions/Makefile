-include ../../Makefile.include
include WEBOS-WAYLAND-EXTENSIONS-VERSION
DEPS = WEBOS-WAYLAND-EXTENSIONS-VERSION Makefile ../../download-files.include

ifeq ($(CROSS_COMPILING), yes)
  DEPS += ../../Makefile.include
else
  ROOT_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
endif

include ../../download-files.include
all: .installed-$(PLATFORM)

RETRIEVE_TOOL_FLAGS := -Ls --create-dirs --retry 10 --retry-delay 3

download: $(TARBALLS_LOCATION)/$(ARCHIVE)

$(TARBALLS_LOCATION)/$(ARCHIVE):
	cd $(TARBALLS_LOCATION); $(RETRIEVE_TOOL) $(RETRIEVE_TOOL_FLAGS) -o $(TARBALLS_LOCATION)/$(ARCHIVE) $(BASE_URL)/archive/refs/tags/submissions/$(VERSION).tar.gz
$(PLATFORM): $(DEPS) | $(TARBALLS_LOCATION)/$(ARCHIVE).$(HASH_TYPE)
	rm -rf $(PLATFORM); mkdir -p $(PLATFORM)
	cd $(PLATFORM); $(ARCHIVE_TOOL) $(ARCHIVE_TOOL_FLAGS) $(TARBALLS_LOCATION)/$(ARCHIVE)

.installed-$(PLATFORM): $(PLATFORM)
	rm -f ${PREFIX}/lib/pkgconfig/wayland-webos-client.pc
	echo 'includedir=$(PREFIX)/include' >> ${PREFIX}/lib/pkgconfig/wayland-webos-client.pc
	echo 'Name: wayland-webos-client' >> ${PREFIX}/lib/pkgconfig/wayland-webos-client.pc
	echo 'Description: wayland-webos-client' >> ${PREFIX}/lib/pkgconfig/wayland-webos-client.pc
	echo 'Version: 1.0.0' >> ${PREFIX}/lib/pkgconfig/wayland-webos-client.pc

	mkdir -p $(PREFIX)/share/wayland-webos
	cp $(PLATFORM)/protocol/webos-shell.xml $(PREFIX)/share/wayland-webos
	touch $@

clean:
	rm -f .installed-$(PLATFORM)

distclean::
	rm -rf $(PLATFORM) .installed-$(PLATFORM)
