CUR_PATH    = $(shell pwd)
YAJL_PATH   = $(CUR_PATH)/third_party/yajl

NGX_PATH    = nginx
MODULE_PATH = $(CUR_PATH)
PREFIX_PATH = $(CUR_PATH)/test-root
INC_FLAGS   = -I$(CUR_PATH)/third_party
INC_FLAGS  += -I$(YAJL_PATH)/build/yajl-2.1.0/include
INC_FLAGS  += -I$(CUR_PATH)/third_party/msgpuck
INC_FLAGS  += -I$(CUR_PATH)/src
YAJL_LIB    = $(YAJL_PATH)/build/yajl-2.1.0/lib/libyajl_s.a
LDFLAGS     = -L$(YAJL_PATH)/build/yajl-2.1.0/lib

DEV_CFLAGS += -ggdb3 -O0 -Wall -Werror

.PHONY: all build
all: build

yajl:
	echo "$(CUR_PATH)" > /dev/null
	ln -sf src third_party/yajl/yajl
	cd $(YAJL_PATH); CFLAGS=" -fPIC" ./configure; make distro

gen_version:
	$(shell cat $(MODULE_PATH)/src/ngx_http_tnt_version.h.in | sed 's/@VERSION_STRING@/"$(shell git describe --tags --dirty)"/g' > $(MODULE_PATH)/src/ngx_http_tnt_version.h)

build: gen_version utils
	$(MAKE) -C $(NGX_PATH)

configure:
	cd $(NGX_PATH) && \
	./auto/configure \
			--with-cc-opt='$(INC_FLAGS)'\
			--add-module='$(MODULE_PATH)'

configure-as-dynamic:
	cd $(NGX_PATH) && \
		./auto/configure --add-dynamic-module='$(MODULE_PATH)'

configure-debug:
	cd $(NGX_PATH) && \
	CFLAGS=" -DMY_DEBUG -Wall -Werror -ggdb3 " ./auto/configure \
						--prefix=$(PREFIX_PATH) \
						--add-module=$(MODULE_PATH) \
						--with-debug
	mkdir -p $(PREFIX_PATH)/conf $(PREFIX_PATH)/logs
	cp -Rf $(CUR_PATH)/misc/nginx.dev.conf $(PREFIX_PATH)/conf/nginx.conf || echo

configure-as-dynamic-debug:
	cd $(NGX_PATH) && \
	CFLAGS=" -DMY_DEBUG -Wall -Werror -ggdb3 " ./auto/configure \
						--prefix=$(PREFIX_PATH) \
						--add-dynamic-module=$(MODULE_PATH) \
						--with-debug
	mkdir -p $(PREFIX_PATH)/conf $(PREFIX_PATH)/logs $(PREFIX_PATH)/modules
#	cp -f $(CUR_PATH)/nginx/objs/ngx_http_tnt_module.so $(PREFIX_PATH)/modules/ngx_http_tnt_module.so
	cp -f $(CUR_PATH)/nginx/objs/ngx_http_tnt_module.so /usr/local/nginx/modules/ngx_http_tnt_module.so
	cp -f $(CUR_PATH)/misc/nginx.dev.dyn.conf $(PREFIX_PATH)/conf/nginx.conf

json2tp:
	$(CC) $(CFLAGS) $(DEV_CFLAGS) $(INC_FLAGS) $(LDFLAGS)\
				$(CUR_PATH)/misc/json2tp.c \
				src/tp_transcode.c \
				-o misc/json2tp \
				-lyajl_s

tp_dump:
	$(CC) $(CFLAGS) $(DEV_CFLAGS) $(INC_FLAGS) $(LDFLAGS)\
				$(CUR_PATH)/misc/tp_dump.c \
				src/tp_transcode.c \
				-o misc/tp_dump \
				-lyajl_s

tp_allowed:
	$(CC) $(CFLAGS) $(DEV_CFLAGS) $(INC_FLAGS) $(LDFLAGS)\
				$(CUR_PATH)/test/tp_allowed.c\
				$(CUR_PATH)/src/tp_allowed_methods.c\
				-o test/tp_allowed
	$(CUR_PATH)/test/tp_allowed

test-dev-man: utils build
	$(CUR_PATH)/test/transcode.sh
	$(CUR_PATH)/test/run_all.sh

test-man: utils build
	$(CUR_PATH)/test/transcode.sh
	$(CUR_PATH)/test/basic_features.py
	$(CUR_PATH)/test/v20_features.py
	$(CUR_PATH)/test/v23_features.py

#test-auto: utils build
#	$(shell $(MODULE_PATH)/test/auto.sh)

#test: test-auto
#check: test

clean:
	$(MAKE) -C $(NGX_PATH) clean 2>1 || echo "pass"
	rm -f misc/tp_{send,dump} misc/json2tp

utils: json2tp tp_dump

build-all: yajl configure build utils
build-all-dynamic: yajl configure-as-dynamic build utils

build-all-debug: yajl configure-debug build utils
build-all-dynamic-debug: yajl configure-as-dynamic-debug build utils
