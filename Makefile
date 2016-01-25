CUR_PATH    = $(shell pwd)
YAJL_PATH   = $(CUR_PATH)/third_party/yajl

NGX_PATH    = nginx
MODULE_PATH = $(CUR_PATH)
PREFIX_PATH = $(CUR_PATH)/test-root
INC_FLAGS   = -I$(CUR_PATH)/third_party
INC_FLAGS  += -I$(YAJL_PATH)/build/yajl-2.1.0/include
INC_FLAGS  += -I$(CUR_PATH)/third_party/msgpuck
YAJL_LIB    = $(YAJL_PATH)/build/yajl-2.1.0/lib/libyajl_s.a
LDFLAGS     = -L$(YAJL_PATH)/build/yajl-2.1.0/lib

DEV_CFLAGS += -ggdb3 -O0 -Wall -Werror

.PHONY: all build
all: build

yajl:
	echo "$(CUR_PATH)" > /dev/null
	ln -sf src third_party/yajl/yajl
	cd $(YAJL_PATH); ./configure; make distro

build: utils
	$(MAKE) -C $(NGX_PATH)

configure-debug:
	cd $(NGX_PATH) && \
	CFLAGS=" -DMY_DEBUG -Wall -Werror -ggdb3 $(INC_FLAGS)" ./auto/configure \
						--prefix=$(PREFIX_PATH) \
						--add-module=$(MODULE_PATH) \
						--with-debug \
						--with-ld-opt='$(LDFLAGS)'
	mkdir -p $(PREFIX_PATH)/conf $(PREFIX_PATH)/logs
	cp -Rf $(CUR_PATH)/misc/nginx.dev.conf $(PREFIX_PATH)/conf/nginx.conf || echo

configure:
	cd $(NGX_PATH) && \
	./auto/configure \
			--with-cc-opt='$(INC_FLAGS)'\
			--add-module='$(MODULE_PATH)'\
			--with-ld-opt='$(LDFLAGS)'

json2tp:
	$(CC) $(CFLAGS) $(DEV_CFLAGS) $(INC_FLAGS) $(LDFLAGS) -I$(CUR_PATH) \
				$(CUR_PATH)/misc/json2tp.c \
				tp_transcode.c \
				-o misc/json2tp \
				-lyajl_s

tp_dump:
	$(CC) $(CFLAGS) $(DEV_CFLAGS) $(INC_FLAGS) $(LDFLAGS) -I$(CUR_PATH) \
				$(CUR_PATH)/misc/tp_dump.c \
				tp_transcode.c \
				-o misc/tp_dump \
				-lyajl_s

test-dev: utils build
	$(CUR_PATH)/test/transcode.sh
	$(CUR_PATH)/test/nginx-tnt.sh

test: utils build
	$(CUR_PATH)/test/transcode.sh
	$(CUR_PATH)/test/basic_features.py
	$(CUR_PATH)/test/v20_features.py

clean:
	$(MAKE) -C $(NGX_PATH) clean 2>1 || echo "pass"
	rm -f misc/tp_{send,dump} misc/json2tp

utils: json2tp tp_dump
build-all: yajl configure build utils
build-all-debug: yajl configure-debug build utils

TAG = $(shell git describe --abbrev=0)

srpm:
	tar czf rpm/$(TAG).tar.gz ./* --exclude=.git		\
								  --exclude=.gitmodules \
								  --exclude=.gitignore 	\
								  --exclude=rpm
	rpmbuild -bs rpm/nginx.spec   --define '_sourcedir ./rpm/'	\
								  --define '_srcrpmdir ./'
