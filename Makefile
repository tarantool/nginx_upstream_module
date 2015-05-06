CUR_PATH    = $(shell pwd)
YAJL_PATH   = $(CUR_PATH)/third_party/yajl
TNTC_PATH   = $(CUR_PATH)/third_party/tarantool-c

NGX_PATH    = nginx
MODULE_PATH = $(CUR_PATH)
PREFIX_PATH = $(CUR_PATH)/test-root
INC_FLAGS   = -I$(TNTC_PATH)/src -I$(TNTC_PATH)/src/msgpuck
INC_FLAGS  += -I$(YAJL_PATH)/build/yajl-2.1.0/include/
YAJL_LIB   = $(YAJL_PATH)/build/yajl-2.1.0/lib/libyajl_s.a
LDFLAGS    = -L$(YAJL_PATH)/build/yajl-2.1.0/lib/

CFLAGS     += -ggdb3 -O0 -Wall -Werror

.PHONY: all build
all: build

yajl:
	echo "$(CUR_PATH)" > /dev/null
	ln -sf src third_party/yajl/yajl
	cd $(YAJL_PATH); ./configure; make distro

tarantool-c:
	cd $(TNTC_PATH)
	git submodule init
	git submodule update --recursive

build: utils
	$(MAKE) -C $(NGX_PATH)

configure-debug:
	cd $(NGX_PATH) && \
	CFLAGS=" -Wall -Werror -ggdb3 $(INC_FLAGS)" ./configure \
						--prefix=$(PREFIX_PATH) \
						--add-module=$(MODULE_PATH) \
						--with-debug --with-ld-opt='$(LDFLAGS)'
	mkdir -p $(PREFIX_PATH)/conf $(PREFIX_PATH)/logs
	unlink $(PREFIX_PATH)/conf/nginx.conf > /dev/null || echo "pass"
	cp -Rf $(NGX_PATH)/conf/* $(PREFIX_PATH)/conf
	rm -f $(PREFIX_PATH)/conf/nginx.conf
	ln -s $(CUR_PATH)/misc/nginx.dev.conf $(PREFIX_PATH)/conf/nginx.conf > /dev/null

configure:
	cd $(NGX_PATH) && \
	./configure --with-cc-opt='$(INC_FLAGS)'\
			--add-module='$(MODULE_PATH)'\
			--with-ld-opt='$(LDFLAGS)'

json2tp:
	$(CC) $(CFLAGS) $(INC_FLAGS) $(LDFLAGS) -I$(CUR_PATH) \
				$(CUR_PATH)/misc/json2tp.c \
				tp_transcode.c \
				-o misc/json2tp \
				-lyajl_s

tp_dump:
	$(CC) $(CFLAGS) $(INC_FLAGS) $(LDFLAGS) -I$(CUR_PATH) \
				$(CUR_PATH)/misc/tp_dump.c \
				tp_transcode.c \
				-o misc/tp_dump \
				-lyajl_s

test: utils build
	$(CUR_PATH)/test/transcode.sh
	$(CUR_PATH)/test/nginx-tnt.sh

clean:
	$(MAKE) -C $(NGX_PATH) clean 2>1 || echo "pass"
	rm -f misc/tp_{send,dump} misc/json2tp

utils: json2tp tp_dump
build-all: yajl tarantool-c configure build utils
build-all-debug: yajl tarantool-c configure-debug build utils

TAG = $(shell git tag --contains)

srpm:
	tar czf rpm/$(TAG).tar.gz ./* --exclude=.git		\
								  --exclude=.gitmodules \
								  --exclude=.gitignore 	\
								  --exclude=rpm
	rpmbuild -bs rpm/nginx.spec   --define '_sourcedir ./rpm/'	\
								  --define '_srcrpmdir ./'
