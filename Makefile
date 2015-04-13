.PHONY = all

NGX_PATH    = nginx
MODULE_PATH = $(PWD)
PREFIX_PATH = $(PWD)/test
all: build

build:
	$(MAKE) -C $(NGX_PATH)

configure-debug:
	cd $(NGX_PATH) && \
	CFLAGS="-ggdb3 -O0 -Wall -Werror" ./configure \
						--prefix=$(PREFIX_PATH) \
						--add-module=$(MODULE_PATH) \
						--without-http_rewrite_module \
						--with-debug
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

build-all: configure build
build-all-debug: configure-debug build
