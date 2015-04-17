
CUR_PATH    = $(PWD)
YAJL_PATH   = $(PWD)/third_party/yajl
TNTC_PATH   = $(PWD)/third_party/tarantool-c

NGX_PATH    = nginx
MODULE_PATH = $(PWD)
PREFIX_PATH = $(PWD)/test
INC_FLAGS   = -I$(TNTC_PATH)/src -I$(TNTC_PATH)/src/msgpuck
INC_FLAGS  += -I$(YAJL_PATH)
LDFLALGS    = -L$(YAJL_PATH)/build/yajl-2.1.0/lib/

.PHONY: all build
all: build

yajl:
	ln -sf src third_party/yajl/yajl
	cd $(YAJL_PATH); ./configure; make distro
	
build:
	$(MAKE) -C $(NGX_PATH)

configure-debug:
	cd $(NGX_PATH) && \
	CFLAGS="-ggdb3 -O0 -Wall -Werror $(INC_FLAGS)" ./configure \
						--prefix=$(PREFIX_PATH) \
						--add-module=$(MODULE_PATH) \
						--without-http_rewrite_module \
						--with-debug --with-ld-opt='$(LDFLAGS)'
	mkdir -p $(PREFIX_PATH)/conf $(PREFIX_PATH)/logs
	cp -Rf $(NGX_PATH)/conf/* $(PREFIX_PATH)/conf
	rm -f $(PREFIX_PATH)/conf/nginx.conf $(PREFIX_PATH)/conf/nginx.dev.conf
	ln -s $(PWD)/misc/nginx.dev.conf $(PREFIX_PATH)/conf/nginx.conf > /dev/null

configure:
	cd $(NGX_PATH) && \
	./configure \
		--add-module=$(MODULE_PATH) \
		--prefix=$(PREFIX_PATH)


clean:
	$(MAKE) -C $(NGX_PATH) clean

build-all: yajl configure build
build-all-debug: yajl configure-debug build

