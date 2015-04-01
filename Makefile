.PHONY = all

NGX_PATH    = /Users/dedok/workspace/3rd/nginx
MODULE_PATH = $(PWD)
PREFIX_PATH = $(PWD)/test
all: build

build:
	$(MAKE) -C $(NGX_PATH)

configure-debug:
	cd $(NGX_PATH) &&                                         \
	CFLAGS="-ggdb3 -O0 -Wall -Werror" ./configure             \
            --prefix=$(PREFIX_PATH)                         \
            --add-module=$(MODULE_PATH)                       \
						--without-http_rewrite_module       						\
            --with-debug
#	mkdir -p $(PREFIX_PATH)/conf $(PREFIX_PATH)/logs
#	cp -Rf $(NGX_PATH)/conf/* $(PREFIX_PATH)/conf
#	rm -f $(PREFIX_PATH)/conf/nginx.conf $(PREFIX_PATH)/conf/nginx.dev.conf
#	ln -s $(PWD)/nginx.dev.conf $(PREFIX_PATH)/conf/nginx.conf

configure:
	cd $(NGX_PATH) &&                     \
	./configure                           \
		--add-module=$(MODULE_PATH)         \
		--with-http_ssl_module              \
		--without-http_rewrite_module       \
		--prefix=$(PREFIX_PATH)


clean:
	$(MAKE) -C $(NGX_PATH) clean

build-all: configure build
build-all-debug: configure-debug build
