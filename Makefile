CUR_PATH    = $(PWD)
YAJL_PATH   = $(PWD)/third_party/yajl
TNTC_PATH   = $(PWD)/third_party/tarantool-c

NGX_PATH    = nginx
MODULE_PATH = $(PWD)
PREFIX_PATH = $(PWD)/test-root
INC_FLAGS   = -I$(TNTC_PATH)/src -I$(TNTC_PATH)/src/msgpuck
INC_FLAGS  += -I$(YAJL_PATH)
LDFLALGS    = -L$(YAJL_PATH)/build/yajl-2.1.0/lib/

CFLAGS     += -ggdb3 -O0 -Wall -Werror

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

json2tp:
	$(CC) $(CFLAGS) $(INC_FLAGS) $(LDFLAGS) -lyajl_s \
				$(CUR_PATH)/misc/json2tp.c \
				tp_transcode.c \
				-o misc/json2tp

tp_dump:
	$(CC) $(CFLAGS) $(INC_FLAGS) $(LDFLAGS) -lyajl_s \
				$(CUR_PATH)/misc/tp_dump.c \
				tp_transcode.c \
				-o misc/tp_dump
tp_send:
	$(CC) $(CFLAGS) $(INC_FLAGS) $(LDFLAGS) -lyajl_s \
				$(CUR_PATH)/misc/tp_send.c \
				tp_transcode.c \
				-o misc/tp_send

clean:
	$(MAKE) -C $(NGX_PATH) clean
	rm -f misc/tp_{send,dump} misc/json2tp

utils: json2tp tp_dump tp_send
build-all: yajl configure build utils
build-all-debug: yajl configure-debug build utils

