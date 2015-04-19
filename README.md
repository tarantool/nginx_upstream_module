# nginx_upstream_module
Tarantool NginX upstream module (JSON API, websockets, load balancing)

  Key features:
  * United nginx features and tarantool features over HTTP(S).
  * Call tarantool methods via json rpc.

  About tarantool: http://tarantool.org
  
  About upstream: http://nginx.org/en/docs/http/ngx_http_upstream_module.html#upstream

## Protocol

  Module expectes json posted over HTTP POST in request body.

### Input JSON form

    { "method": METHOD_NAME, "params":[TNT_METHOD, arg0 ... argN], "id": ID }

    "method"

      A String containing the name of the method to be invoked,
      i.e. Tarantool "call", "eval", etc.

      Notice. Only tarantool "call" method is supported.

    "params"

      Here is a Structured array, that MUST hold the TNT_METHOD name as first element before all of the rest elements.
      These elements are arguments of TNT_METHOD.

    "id"
      An identifier established by the Client MUST contain an unsigned Number not
      greater than unsigned int.

    These are required fields.

### Output JSON form

    { "result": JSON_RESULT_OBJECT, "id" ID, "error": { "message": STR } }

    "result"

      Tarantool executing result as json object/array etc.
      MAY be null or undefined.

    "id"

      Request id is returned back.
      MAY be null or undefined in case of internal error.


    "error"

      Here is a Structured object which contains internal error message.
      This field exists only if internal error occured, for instance "too large
      request", "input json parse error", etc.

      If this field exists input message did not pass to Tarantool backend.

      See "message" field for details.


## Compilation and install

### Build from the sources

    $ cd REPO_ROOT
    $ git submodule init
    $ git submodule update
    $ git clone https://github.com/nginx/nginx.git nginx
    $ make build-all # build-all-debug i.e. debug version

### Build module via nginx 'configure'

  Requirements (for details see REPO_ROOT/Makefile)

    libyajl >= 2.0(https://lloyd.github.io/yajl/)
    libtp (https://github.com/tarantool/tarantool-c)

    $ ./configure --add-module=REPO_ROOT && make

## Configuration

```nginx

    upstream backend {
        server 127.0.0.1:9999;
        # ...
    }

    server {
      location = /tnt {
        tnt_pass backend;
      }
    }

```

Please report bugs at https://github.com/tarantool/nginx_upstream_module/issues
We also warmly welcome your feedback in the discussion mailing list, tarantool@googlegroups.com.
