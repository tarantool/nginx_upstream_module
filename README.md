# Tarantool NginX upstream module (v2.3.1)
---------------------------------
Key features:
* Both nginx and tarantool features accessible over HTTP(S).
* Tarantool methods callable via JSON-RPC or REST.
* Load balancing with elastic configuration.
* Backup and fault tolerance.
* Low overhead.

Note: WebSockets are currently not supported until Tarantool supports out-of-band replies.

About Tarantool: http://tarantool.org

About upstream: http://nginx.org/en/docs/http/ngx_http_upstream_module.html#upstream

## Docker images

Nginx upstream module - https://hub.docker.com/r/tarantool/tarantool-nginx

Tarantool - https://hub.docker.com/r/tarantool/tarantool

## Status
---------
* v0.1.4 - Production ready.
* v0.2.0 - Stable.
* v0.2.1 - Production ready.
* v0.2.2 - Stable.
* v2.3.1 - Production ready.
* v2.3.2 - Production ready.

## Content
----------
* [Compilation and install](#compilation-and-install)
* [REST](#rest)
* [JSON](#json)
* [Directives](#directives)
  * [tnt_pass](#tnt_pass)
  * [tnt_http_methods](#tnt_http_methods)
  * [tnt_http_rest_methods](#tnt_http_rest_methods)
  * [tnt_pass_http_request](#tnt_pass_http_request)
  * [tnt_pass_http_request_buffer_size](#tnt_pass_http_request_buffer_size)
  * [tnt_method](#tnt_method)
  * [tnt_http_allowed_methods - experemental](#tnt_http_allowed_methods)
  * [tnt_send_timeout](#tnt_send_timeout)
  * [tnt_read_timeout](#tnt_read_timeout)
  * [tnt_buffer_size](#tnt_buffer_size)
  * [tnt_next_upstream](#tnt_next_upstream)
  * [tnt_connect_timeout](#tnt_connect_timeout)
  * [tnt_next_upstream](#tnt_next_upstream)
  * [tnt_next_upstream_tries](#tnt_next_upstream_tries)
  * [tnt_next_upstream_timeout](#tnt_next_upstream_timeout)
* [Performance tuning](#performance-tuning)
* [Examples](#examples)
* [Copyright & License](#copyright--license)
* [See also](#see-also)

## Compilation and install
--------------------------

### Build from source
```bash
git clone https://github.com/tarantool/nginx_upstream_module.git nginx_upstream_module
cd nginx_upstream_module
git submodule update --init --recursive
git clone https://github.com/nginx/nginx.git nginx
sudo apt-get install libpcre-dev zlib1-dev # install dependencies to build nginx
make build-all # or 'build-all-debug' for debug version
```
[Back to content](#content)

### Build module via nginx 'configure'

  Requirements (for details, see REPO_ROOT/Makefile)

    libyajl >= 2.0(https://lloyd.github.io/yajl/)
    libmsgpuck >= 1.0 (https://github.com/rtsisyk/msgpuck)

    $ ./configure --add-module=REPO_ROOT && make

## Configuration

```nginx
    ## Typical configuration, for more see http://nginx.org/en/docs/http/ngx_http_upstream_module.html#upstream
    upstream backend {
        server 127.0.0.1:9999 max_fails=1 fail_timeout=30s;
        server 127.0.0.1:10000;

        # ...
        server 127.0.0.1:10001 backup;

        # ...
    }

    server {
      location = /tnt {
        tnt_pass backend;
      }
    }

```

[Back to content](#content)

## REST
-------

  NOTE: since v0.2.0

  With this module, you can call Tarantool stored procedures via HTTP REST methods (GET, POST, PUT, DELETE)

  Example
  ```nginx
    upstream backend {
      # Tarantool hosts
      server 127.0.0.1:9999;
    }

    server {
      # HTTP [GET | POST | PUT | DELETE] /tnt_rest?q=1&q=2&q=3
      location /tnt_rest {
        # REST mode on
        tnt_http_rest_methods get post put delete; # or all

        # Pass http headers and uri
        tnt_pass_http_request on;

        # Module on
        tnt_pass backend;
      }
    }

```

```lua
-- Tarantool procedure
function tnt_rest(req)
 req.headers -- http headers
 req.uri -- uri
 return { 'ok' }
end

```

```bash
 $> wget NGX_HOST/tnt_rest?arg1=1&argN=N
```

[Back to content](#content)

## JSON
-------

  NOTE: since v0.1.4

  The module expects JSON posted with HTTP POST or PUT (since v0.2.0) and carried in request body.

  Server HTTP statuses

    OK - response body contains a result or an error;
         the error may appear only if something wrong happened within Tarantool,
         for instance: 'method not found'.

    INTERNAL SERVER ERROR - may appear in many cases,
                            most of them being 'out of memory' error;

    NOT ALLOWED - in response to anything but a POST request.

    BAD REQUEST - JSON parse error, empty request body, etc.

    BAD GATEWAY - lost connection to Tarantool server(s);
                  Since both (i.e. json -> tp and tp -> json) parsers work asynchronously,
                  this error may appear if 'params' or 'method' does not exists in the structure
                  of the incoming JSON, please see the protocol description for more details.

                  Note: this behavior will change in the future.

### Input JSON form

    [ { "method": STR, "params":[arg0 ... argN], "id": UINT }, ...N ]

    "method"

      A String containing the name of the Tarantool method to be invoked (i.e. Tarantool "call")

    "params"

      A Structured array. Each element is an argument of the Tarantool "call".


    "id"

      An identifier established by the Client. MUST contain an unsigned Number not
      greater than unsigned int.

      MAY be 0.

    These are required fields.

### Output JSON form

    [ { "result": JSON_RESULT_OBJECT, "id":UINT, "error": { "message": STR, "code": INT } }, ...N ]

    "result"

      Tarantool execution result (a json object/array, etc).

      MAY be null or undefined.

    "id"

      Request id is returned back.

      MAY be null or undefined.


    "error"

      A Structured object which contains an internal error message.
      This field exists only if an internal error occurred, for instance:
      "too large request", "input json parse error", etc.

      If this field exists, the input message was _probably_  not passed to the Tarantool backend.

      See "message"/"code" fields for details.


### Example

      Syntax:

      --> data sent to Server
      <-- data sent to Client

      rpc call 1:
      --> { "method": "echo", "params": [42, 23], "id": 1 }
      <-- { "result": [42, 23], "id": 1 }

      rpc call 2:
      --> { "method": "echo", "params": [ [ {"hello": "world"} ], "!" ], "id": 2 }
      <-- { "result": [ [ {"hello": "world"} ], "!" ], "id": 2 }

      rpc call of a non-existent method:
      --> { "method": "echo_2", "id": 1 }
      <-- { "error": {"code": -32601, "message": "Method not found"}, "id": 1 }

      rpc call with invalid JSON:
      --> { "method": "echo", "params": [1, 2, 3, __wrong__ ] }
      <-- { "error": { "code": -32700, "message": "Parse error" } }

      rpc call Batch:
      --> [
            { "method": "echo", "params": [42, 23], "id": 1 },
            { "method": "echo", "params": [ [ {"hello": "world"} ], "!" ], "id": 2 }
      ]
      <-- [
            { "result": [42, 23], "id": 1 },
            { "result": [ [ {"hello": "world"} ], "!" ], "id": 2 }
      ]

      rpc call Batch of a non-existent method:
       --> [
            { "method": "echo_2", "params": [42, 23], "id": 1 },
            { "method": "echo", "params": [ [ {"hello": "world"} ], "!" ], "id": 2 }
      ]
      <-- [
            { "error": {"code": -32601, "message": "Method not found"}, "id": 1 },
            { "result": [ [ {"hello": "world"} ], "!" ], "id": 2 }
      ]

      rpc call Batch with invalid JSON:
      --> [
            { "method": "echo", "params": [42, 23, __wrong__], "id": 1 },
            { "method": "echo", "params": [ [ {"hello": "world"} ], "!" ], "id": 2 }
      ]
      <-- { "error": { "code": -32700, "message": "Parse error" } }
      
[Back to content](#content)

## Directives
-------------
[Back to content](#content)

tnt_pass
------------
**syntax:** *tnt_pass UPSTREAM*

**default:** *no*

**context:** *location*

Specify the Tarantool server backend.

```nginx
  location = /tnt {
    tnt_pass 127.0.0.1:9999;
  }

  upstream tnt_upstream {
     127.0.0.1:9999;
  };

 location = /tnt_next_location {
     tnt_pass tnt_upstream;
 }
```

[Back to content](#content)

tnt_http_methods
----------------
**syntax:** *tnt_http_methods post, put, delete, all*

**default:** *post, delete*

**context:** *location*

Allow to accept one or many http methods.
If method alloed, then module expects JSON carried in request body, for details see [JSON](#json)
If `tnt_method` is not set, then the name of the Tarantool stored procedure is the protocol [JSON](#json).

Example
```nginx
  location tnt {
    tnt_http_methods delete;
    tnt_pass 127.0.0.1:9999;
  }
```

```bash
  # Call tarantool_stored_procedure_name()
  $> wget --method=delete --body-data='{"method":"lua_function", "params": [], "id": 0}' NGINX_HOST/tnt
```

[Back to content](#content)

tnt_http_rest_methods
----------------
**syntax:** *tnt_http_rest_methods get, post, put, delete, all*

**default:** *no*

**context:** *location*

Allow to accept one or many REST methods.
If `tnt_method` is not set, then the name of the Tarantool stored procedure is the first part of the URL path.

Example
```nginx
  location tnt {
    tnt_http_rest_methods get;
    tnt_pass 127.0.0.1:9999;
  }
```

```bash
  # Call tarantool_stored_procedure_name()
  $> wget NGINX_HOST/tarantool_stored_procedure_name/some/mega/path?q=1
```

[Back to content](#content)

tnt_pass_http_request
------------------
**syntax:** *tnt_pass_http_request [on|off|parse_args|unescape|pass_body|pass_headers_out]*

**default:** *off*

**context:** *location, location if*

Allow to pass HTTP headers and queries to Tarantool stored procedures.

Examples #1
```nginx
  location tnt {
    # Also, tnt_pass_http_request can be used together with JSON communication
    tnt_http_rest_methods get;

    # [on|of]
    tnt_pass_http_request on;
    tnt_pass 127.0.0.1:9999;
  }
```
```lua
  function tarantool_stored_procedure_name(req, ...)
    req.headers -- lua table
    req.query -- string
    return { 'OK' }
  end

  -- With parse_args 
  function tarantool_stored_procedure_name_1(req, ...)
    req.headers -- lua table
    req.query -- string
    req.args -- query args as lua table
    return { 'OK' }
  end

  -- With pass_body
  function tarantool_stored_procedure_name_2(req, ...)
    req.body -- request body, type string
  end
```
Examples #2 (pass_headers_out)
```nginx
  location @tnt {
    tnt_http_rest_methods get;
    tnt_pass_http_request on pass_headers_out;
    tnt_method tarantool_stored_procedure_name;
    tnt_pass 127.0.0.1:9999;
  }

  location / {
    add_header "X-Req-time" "$request_time";
    proxy_pass http://backend;
    post_action @tnt;
  }
```
```lua
  function tarantool_stored_procedure_name(req, ...)
    req.headers -- lua table
    req.headers['X-Req-time'] -- set by add_header
    req.query -- string
    return true
  end
```

```bash
  # Call tarantool_stored_procedure_name()
  $> wget NGINX_HOST/tarantool_stored_procedure_name/some/mega/path?q=1
```

[Back to content](#content)

tnt_pass_http_request_buffer_size
------------------------
**syntax:** *tnt_pass_http_request_buffer_size SIZE*

**default:** *4k, 8k*

**context:** *location*

Specify the size of the buffer used for `tnt_pass_http_request`.

[Back to content](#content)

tnt_method
-----------
**syntax:** *tnt_method STR*

**default:** *no*

**context:** *location, location if*

Specify the Tarantool call method.

Examples
```nginx
  location tnt {
    # Also tnt_pass_http_request can mix with JSON communication [[
    tnt_http_rest_methods get;
    tnt_method tarantool_stored_procedure_name;
    #]]

    # [on|of]
    tnt_pass_http_request on;
    tnt_pass 127.0.0.1:9999;
  }
```
```lua
  function tarantool_stored_procedure_name(req, ...)
    req.headers -- lua table
    req.query -- string
    return { 'OK' }
  end
```
```bash
  # OK Call tarantool_stored_procedure_name()
  $> wget NGINX_HOST/tarantool_stored_procedure_name/some/mega/path?q=1

  # Error Call tarantool_stored_procedure_XXX()
  $> wget NGINX_HOST/tarantool_stored_procedure_XXX/some/mega/path?q=1
```

[Back to content](#content)

tnt_send_timeout
-------------------
**syntax:** *tnt_send_timeout TIME*

**default:** *60s*

**context:** *http, server, location*

The timeout for sending TCP requests to the Tarantool server, in seconds by default.
It's wise to always explicitly specify the time unit to avoid confusion.
Time units supported are `s`(seconds), `ms`(milliseconds), `y`(years), `M`(months), `w`(weeks), `d`(days), `h`(hours), and `m`(minutes).

[Back to content](#content)

tnt_read_timeout
-------------------
**syntax:** *tnt_read_timeout TIME*

**default:** *60s*

**context:** *http, server, location*

The timeout for reading TCP responses from the Tarantool server, in seconds by default.

It's wise to always explicitly specify the time unit to avoid confusion.
Time units supported are `s`(seconds), `ms`(milliseconds), `y`(years), `M`(months), `w`(weeks), `d`(days), `h`(hours), and `m`(minutes).

[Back to content](#content)

tnt_connect_timeout
----------------------
**syntax:** *tnt_connect_timeout TIME*

**default:** *60s*

**context:** *http, server, location*

The timeout for connecting to the Tarantool server, in seconds by default.

It's wise to always explicitly specify the time unit to avoid confusion.
Time units supported are `s`(seconds), `ms`(milliseconds), `y`(years), `M`(months), `w`(weeks), `d`(days), `h`(hours), and `m`(minutes).
This time must be strictly less than 597 hours.

[Back to content](#content)

tnt_buffer_size
------------------
**syntax:** *tnt_buffer_size SIZE*

**default:** *4k, 8k*

**context:** *http, server, location*

This buffer size is used for reading Tarantool replies,
but it's not required to be as big as the largest possible Tarantool reply.

[Back to content](#content)

tnt_next_upstream
--------------------
**syntax:** *tnt_next_upstream [ error | timeout | invalid_response | off ]*

**default:** *error timeout*

**context:** *http, server, location*

Specify which failure conditions should cause the request to be forwarded to another
upstream server. Applies only when the value in [tnt_pass](#tnt_pass) is an upstream with two or more
servers.

[Back to content](#content)

tnt_next_upstream_tries
--------------------
**syntax:** *tnt_next_upstream_tries SIZE*

**default:** *0*

**context:** *http, server, location*

Limit the number of possible tries for passing a request to the next server.
The 0 value turns off this limitation.

tnt_next_upstream_timeout
--------------------
**syntax:** *tnt_next_upstream_timeout TIME*

**default:** *0*

**context:** *http, server, location*


Limit the time during which a request can be passed to the next server.
The 0 value turns off this limitation.

[Back to content](#content)

tnt_pure_result
--------------------
**syntax:** *tnt_pure_result [on|off]*

**default:** *off*

**context:** *http, server, location*


Whether to wrap tnt response or not.
When this option is off:
```
{"id":0, "result": [[ 1 ]]}
```
When this option is on:
```
[[1]]
```

[Back to content](#content)

tnt_multireturn_skip_count
--------------------
**syntax:** *tnt_multireturn_skip_count [0|1|2]*

**default:** *0*

**context:** *http, server, location*


Module will skip one or more multireturn parts when this option is > 0
When it is set to 0:
```
{"id":0, "result": [[1]]}
```

When it is set to 1:
```
{"id":0, "result": [1]}
```

When it is set to 2:
```
{"id": 0, "result": 1}
```

[Back to content](#content)


## Performance Tuning
---------------------
* Use [HttpUpstreamKeepaliveModule](http://wiki.nginx.org/HttpUpstreamKeepaliveModule).
  * Use [keepalive](http://nginx.org/en/docs/http/ngx_http_upstream_module.html#keepalive).
  * use [keepalive_requests](http://nginx.org/en/docs/http/ngx_http_core_module.html#keepalive_requests).
* Use multiple instances of Tarantool servers on your multi-core machines.
* Turn off unnecessary logging in Tarantool and NginX.
* Tune Linux network.
* Tune nginx buffers.

[Back to content](#content)

## Examples
-----------
Python test: test/basic_features.py, test/v20_feautres.py, nginx.dev.conf.

Client side javascript example: example/echo.html, example/echo.lua.

[Back to content](#content)

## Copyright & License
----------------------
[LICENSE](https://github.com/tarantool/nginx_upstream_module/blob/master/LICENSE)

[Back to content](#content)

## See also
-----------
* [Tarantool](http://tarantool.org) homepage.
* [lua-resty-tarantool](https://github.com/perusio/lua-resty-tarantool)
* Tarantool [protocol](http://tarantool.org/doc/dev_guide/box-protocol.html?highlight=protocol)

[Back to content](#content)

================
Please report bugs at https://github.com/tarantool/nginx_upstream_module/issues.

We also warmly welcome your feedback in the discussion mailing list, tarantool@googlegroups.com.
