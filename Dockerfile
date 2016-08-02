FROM alpine:3.4

RUN addgroup -S nginx \
    && adduser -S -G nginx nginx \
    && apk add --no-cache 'su-exec>=0.2'


ENV NGINX_VERSION=1.9.7 \
    NGINX_UPSTREAM_MODULE_URL=https://github.com/racktear/nginx_upstream_module.git \
    MSGPUCK_DOWNLOAD_URL=https://github.com/tarantool/msgpuck.git


RUN set -x \
  && apk add --no-cache --virtual .build-deps \
     build-base \
     cmake \
     linux-headers \
     openssl-dev \
     pcre-dev \
     zlib-dev \
     wget \
     git \
     tar \
  && apk add --no-cache --virtual .run-deps \
     ca-certificates \
     openssl \
     pcre \
     zlib \
  && : "---------- nginx-upstream-module ----------" \
  && git clone "$NGINX_UPSTREAM_MODULE_URL" /usr/src/nginx_upstream_module \
  && git -C /usr/src/nginx_upstream_module checkout docker-container \
  && git -C /usr/src/nginx_upstream_module submodule init \
  && git -C /usr/src/nginx_upstream_module submodule update \
  && make -C /usr/src/nginx_upstream_module yajl \
  && : "---------- nginx ----------" \
  && wget -O nginx.tar.gz http://nginx.org/download/nginx-${NGINX_VERSION}.tar.gz \
  && mkdir -p /usr/src/nginx \
  && tar -xzf nginx.tar.gz -C /usr/src/nginx \
      --strip-components=1 \
  && cd /usr/src/nginx \
  && ./configure \
      --add-module=/usr/src/nginx_upstream_module \
      --prefix=/etc/nginx \
      --sbin-path=/usr/sbin/nginx \
      --conf-path=/etc/nginx/nginx.conf \
      --error-log-path=/var/log/nginx/error.log \
      --http-log-path=/var/log/nginx/access.log \
      --pid-path=/var/run/nginx.pid \
      --lock-path=/var/run/nginx.lock \
      --http-client-body-temp-path=/var/cache/nginx/client_temp \
      --http-proxy-temp-path=/var/cache/nginx/proxy_temp \
      --http-fastcgi-temp-path=/var/cache/nginx/fastcgi_temp \
      --http-uwsgi-temp-path=/var/cache/nginx/uwsgi_temp \
      --http-scgi-temp-path=/var/cache/nginx/scgi_temp \
      --user=nginx \
      --group=nginx \
      --with-http_ssl_module \
      --with-http_realip_module \
      --with-http_addition_module \
      --with-http_sub_module \
      --with-http_dav_module \
      --with-http_flv_module \
      --with-http_mp4_module \
      --with-http_gunzip_module \
      --with-http_gzip_static_module \
      --with-http_random_index_module \
      --with-http_secure_link_module \
      --with-http_stub_status_module \
      --with-http_auth_request_module \
      --with-mail \
      --with-mail_ssl_module \
      --with-file-aio \
      --with-ipv6 \
      --with-threads \
      --with-stream \
      --with-stream_ssl_module \
      --with-http_v2_module \
  && make \
  && make install \
  && sed -i -e 's/#access_log  logs\/access.log  main;/access_log \/dev\/stdout;/' \
            -e 's/#error_log  logs\/error.log  notice;/error_log stderr notice;/' \
            /etc/nginx/nginx.conf \
  && rm -rf /usr/src/nginx \
  && rm -rf /usr/src/nginx_upstream_module \
  && : "---------- remove build deps ----------" \
  && apk del .build-deps

VOLUME ["/var/cache/nginx"]
