CUR_PATH    = $(shell pwd)
YAJL_PATH   = $(CUR_PATH)/third_party/yajl

NGX_PATH      = $(CUR_PATH)/nginx
MODULE_PATH   = $(CUR_PATH)
PREFIX_PATH   = $(CUR_PATH)/test-root

NGX_CONFIGURE = ./auto/configure
## Some versions of nginx have different path of the configure,
## following lines are handle it {{
ifeq ($(shell [ -e "$(NGX_PATH)/configure" ] && echo 1 || echo 0 ), 1)
NGX_CONFIGURE=./configure
endif
## }}

MODULE_PATH = $(CUR_PATH)
PREFIX_PATH = $(CUR_PATH)/test-root
TEST_PATH   = $(CUR_PATH)/test
INC_FLAGS   = -I$(CUR_PATH)/third_party
INC_FLAGS  += -I$(YAJL_PATH)/build/yajl-2.1.0/include
INC_FLAGS  += -I$(CUR_PATH)/third_party/msgpuck
INC_FLAGS  += -I$(CUR_PATH)/src
YAJL_LIB    = $(YAJL_PATH)/build/yajl-2.1.0/lib/libyajl_s.a
LDFLAGS     = -L$(YAJL_PATH)/build/yajl-2.1.0/lib
LDFLAGS    += -L$(MODULE_PATH)/third_party/msgpuck
PREFIX      = /usr/local/nginx

DEV_CFLAGS += -ggdb3 -O0 -Wall -Werror

.PHONY: all build
all: build

yajl:
	ln -sf src third_party/yajl/yajl
	cd $(YAJL_PATH); ./configure; make distro

msgpack:
	cd $(MODULE_PATH)/third_party/msgpuck && \
			cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo . && \
			make && \
			cd -

gen_version:
	$(shell cat $(MODULE_PATH)/src/ngx_http_tnt_version.h.in | sed 's/@VERSION_STRING@/"$(shell git describe --tags --dirty)"/g' > $(MODULE_PATH)/src/ngx_http_tnt_version.h)

build: gen_version utils
	$(MAKE) -C $(NGX_PATH)

__install:
	cd $(NGX_PATH) && $(NGX_CONFIGURE) \
			--with-cc-opt='$(INC_FLAGS)' \
			--add-module='$(MODULE_PATH)' \
			--prefix=$(PREFIX)
	make -j2
	sudo mkdir -p $(PREFIX)/conf $(PREFIX)/logs $(PREFIX)/sbin
	sudo cp -Rf $(NGX_PATH)/conf/* $(PREFIX)/conf
	sudo cp $(NGX_PATH)/objs/nginx $(PREFIX)/sbin/nginx

configure-as-dynamic:
	cd $(NGX_PATH) && $(NGX_CONFIGURE) --add-dynamic-module='$(MODULE_PATH)'

configure-debug:
	cd $(NGX_PATH) && \
		CFLAGS="$(DEV_CFLAGS)" $(NGX_CONFIGURE) \
						--prefix=$(PREFIX_PATH) \
						--with-http_addition_module \
						--add-module=$(MODULE_PATH) \
						--with-debug

configure-for-testing: configure-debug
	mkdir -p $(PREFIX_PATH)/conf $(PREFIX_PATH)/logs
	cp -Rf $(NGX_PATH)/conf/* $(PREFIX_PATH)/conf
	cp -f $(TEST_PATH)/ngx_confs/tnt_server_test.conf $(PREFIX_PATH)/conf/tnt_server_test.conf
	cp -f $(TEST_PATH)/ngx_confs/nginx.dev.conf $(PREFIX_PATH)/conf/nginx.conf

configure-as-dynamic-debug:
	cd $(NGX_PATH) && \
		CFLAGS="$(DEV_CFLAGS)" $(NGX_CONFIGURE) \
						--prefix=$(PREFIX_PATH) \
						--add-dynamic-module=$(MODULE_PATH) \
						--with-debug
	mkdir -p $(PREFIX_PATH)/conf $(PREFIX_PATH)/logs $(PREFIX_PATH)/modules
	cp -Rf $(NGX_PATH)/conf/* $(PREFIX_PATH)/conf
	cp -f $(TEST_PATH)/ngx_confs/nginx.dev.dyn.conf $(PREFIX_PATH)/conf/nginx.conf
	cp -f $(TEST_PATH)/ngx_confs/tnt_server_test.conf $(PREFIX_PATH)/conf/tnt_server_test.conf

json2tp:
	$(CC) $(CFLAGS) $(DEV_CFLAGS) $(INC_FLAGS) $(LDFLAGS)\
				$(CUR_PATH)/misc/json2tp.c \
				src/json_encoders.c \
				src/tp_transcode.c \
				-o misc/json2tp \
				-lyajl_s \
				-lmsgpuck

tp_dump:
	$(CC) $(CFLAGS) $(DEV_CFLAGS) $(INC_FLAGS) $(LDFLAGS)\
				$(CUR_PATH)/misc/tp_dump.c \
				src/json_encoders.c \
				src/tp_transcode.c \
				-o misc/tp_dump \
				-lyajl_s \
				-lmsgpuck

test-dev-man: utils build
	$(TEST_PATH)/transcode.sh
	$(TEST_PATH)/run_all.sh

test-man: utils build
	$(TEST_PATH)/transcode.sh
	$(TEST_PATH)/basic_features.py
	$(TEST_PATH)/v20_features.py
	$(TEST_PATH)/v23_features.py

#test-auto: utils build
#	$(shell $(MODULE_PATH)/test/auto.sh)

#test: test-auto
#check: test

clean:
	$(MAKE) -C $(NGX_PATH) clean 2>1 || echo "pass"
	rm -f misc/tp_{send,dump} misc/json2tp

utils: json2tp tp_dump

build-all: msgpack yajl configure-debug build utils
build-all-dynamic: msgpack yajl configure-as-dynamic build utils
