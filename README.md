# Tarantool NginX upstream module
=================
  Key features:
  * Benefit from nginx features and tarantool features over HTTP(S).
  * Call tarantool methods via JSON RPC or REST.
  * Load Balancing with elastic configuration.
  * Backup and fault tolerance.
  * Low overhead.

Note: Websockets are currently not supported until Tarantool support out of band replies.

About tarantool: http://tarantool.org

About upstream: http://nginx.org/en/docs/http/ngx_http_upstream_module.html#upstream

## Status
=================
  * v0.1.4 - Production ready.
  * v0.2.0 - Stable.
  * v0.2.1 - Alpha.

## Content
=================
* [REST](#rest)
* [JSON](#json)
* [Compilation and install](#compilation-and-install)
* [Directives](#directives)
  * [tnt_pass](#tnt_pass)
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

## REST
=================
  NOTE: since v0.2.0

  With this module you can call Tarantool stored procedure via HTTP GET,POST,PUT,DELETE
  e.g. call Tarantool stored procedure via HTTP REST

  Example
  ```nginx
    upstream backend {
      # Tarantool hosts
      server 127.0.0.1:9999;
    }

    server {
      # GET | POST | PUT | DELETE tnt_test?q=1&q=2&q=3
      location /tnt_rest {
        # REST mode on
        tnt_http_rest_methods get post put delete; # or all

        # Pass http headers and uri
        tnt_http_passhttp_request on;

        # Module on
        tnt_pass backend;
      }
    }

```

```lua
-- Tarantool procudure
function tnt_rest(req)
 req.headers -- http headers
 req.uri -- uri
 return { 'ok' }
end

```

```bash
 $> wget NGX_HOST/tnt_rest?arg1=1&argN=N
```

## JSON
=================
  NOTE: since v0.1.4

  The module expects JSON posted with HTTP POST, PUT(since v0.2.0) and carried in request body.

  Server HTTP statuses

    OK - response body contains a result or an error;
         the error may appear only if something happened within Tarantool,
         for instance: 'method not found'.

    INTERNAL SERVER ERROR - may appear in many cases,
                            most of them is 'out of memory' error;

    NOT ALLOWED - in reponse to anything but  a POST request.

    BAD REQUEST - JSON parse error, empty request body, etc.

    BAD GATEWAY - lost connection to Tarantool server(s);
                  Since both (i.e. json -> tp and tp -> json) parsers work asynchronouly,
                  this error may appear if 'params' or 'method' do not exists in tbe structure
                  of incoming JSON, please see the protocol description for more details.

                  Note: this behavior will change in  the future.

### Input JSON form

    [ { "method": STR, "params":[arg0 ... argN], "id": UINT }, ...N ]

    "method"

      A String containing the name of the method to be invoked (i.e. Tarantool "call")

    "params"

      Here is a Structured array. Each element is a argument of Tarantool "call".


    "id"

      An identifier established by the Client MUST contain an unsigned Number not
      greater than unsigned int.

      MAY be 0.

    These are required fields.

### Output JSON form

    [ { "result": JSON_RESULT_OBJECT, "id":UINT, "error": { "message": STR, "code": INT } }, ...N ]

    "result"

      Tarantool executing result as json object/array etc.

      MAY be null or undefined.

    "id"

      Request id is returned back.

      MAY be null or undefined.


    "error"

      Here is a Structured object which contains internal error message.
      This field exists only if internal error occured, for instance:
      "too large request", "input json parse error", etc.

      If this field exists input message _probably_ did not pass to Tarantool backend.

      See "message"/"code" field for details.


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

      rpc call of non-existent method:
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

      rpc call Batch of non-existent method:
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

## Compilation and install

### Build from the sources

    $ git clone https://github.com/tarantool/nginx_upstream_module.git nginx_upstream_module
    $ cd nginx_upstream_module
    $ git submodule update --init --recursive
    $ git clone https://github.com/nginx/nginx.git nginx
    $ make build-all # build-all-debug i.e. debug version

### Build module via nginx 'configure'

  Requirements (for details see REPO_ROOT/Makefile)

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
## Directives
=================
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

 location = /tnt_next {
     tnt_pass tnt_upstream;
 }
```

[Content](#content)

tnt_http_rest_methods
----------------
**syntax:** *tnt_http_rest_methods get, post, put, delete, all*

**default:** *no*

**context:** *location*

Allow to accept one or many of RESTs methods.
If `tnt_method` not set then name of Tarantool stored procedure are first part of url path.

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

[Content](#content)

tnt_pass_http_request
------------------
**syntax:** *tnt_pass_http_request [on|off]*

**default:** *no*

**context:** *location, location if*

Allow to pass to Tarantool stored procedure HTTP headers and query.

Examples
```nginx
  location tnt {
    # Also tnt_pass_http_request can mix with JSON communication
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
```
```bash
  # Call tarantool_stored_procedure_name()
  $> wget NGINX_HOST/tarantool_stored_procedure_name/some/mega/path?q=1
```

[Content](#content)

tnt_pass_http_request_buffer_size
------------------------
**syntax:** *tnt_pass_http_request_buffer_size SIZE*

**default:** *4k, 8k*

**context:** *location*

Specify the size of the buffer used for `tnt_pass_http_request`.

[Content](#content)

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
[Content](#content)

tnt_send_timeout
-------------------
**syntax:** *tnt_send_timeout TIME*

**default:** *60s*

**context:** *http, server, location*

The timeout for sending TCP requests to the Tarantool server, in seconds by default.
It's wise to always explicitly specify the time unit to avoid confusion.
Time units supported are `s`(seconds), `ms`(milliseconds), `y`(years), `M`(months), `w`(weeks), `d`(days), `h`(hours), and `m`(minutes).

[Content](#content)

tnt_read_timeout
-------------------
**syntax:** *tnt_read_timeout TIME*

**default:** *60s*

**context:** *http, server, location*

The timeout for reading TCP responses from the Tarantool server, in seconds by default.

It's wise to always explicitly specify the time unit to avoid confusion.
Time units supported are `s`(seconds), `ms`(milliseconds), `y`(years), `M`(months), `w`(weeks), `d`(days), `h`(hours), and `m`(minutes).

[Content](#content)

tnt_connect_timeout
----------------------
**syntax:** *tnt_connect_timeout TIME*

**default:** *60s*

**context:** *http, server, location*

The timeout for connecting to the Tarantool server, in seconds by default.

It's wise to always explicitly specify the time unit to avoid confusion.
Time units supported are `s`(seconds), `ms`(milliseconds), `y`(years), `M`(months), `w`(weeks), `d`(days), `h`(hours), and `m`(minutes).
This time must be less than 597 hours.

[Content](#content)

tnt_buffer_size
------------------
**syntax:** *tnt_buffer_size SIZE*

**default:** *4k, 8k*

**context:** *http, server, location*

This buffer size is used for reading Tarantool replies,
but it's not required to be as big as the largest possible Tarantool reply.

[Content](#content)

tnt_next_upstream
--------------------
**syntax:** *tnt_next_upstream [ error | timeout | invalid_response | off ]*

**default:** *error timeout*

**context:** *http, server, location*

Specify which failure conditions should cause the request to be forwarded to another
upstream server. Applies only when the value in [tnt_pass](#tnt_pass) is an upstream with two or more
servers.

[Content](#content)

tnt_next_upstream_tries
--------------------
**syntax:** *tnt_next_upstream_tries SIZE*

**default:** *0*

**context:** *http, server, location*

Limits the number of possible tries for passing a request to the next server.
The 0 value turns off this limitation.

tnt_next_upstream_timeout
--------------------
**syntax:** *tnt_next_upstream_timeout TIME*

**default:** *0*

**context:** *http, server, location*
Limits the time during which a request can be passed to the next server.
The 0 value turns off this limitation.

[Content](#content)

## Performance Tuning
==================
* Use [HttpUpstreamKeepaliveModule](http://wiki.nginx.org/HttpUpstreamKeepaliveModule)) with this module.
  ** Use [keepalive](http://nginx.org/en/docs/http/ngx_http_upstream_module.html#keepalive).
  ** use [keepalive_requests](http://nginx.org/en/docs/http/ngx_http_core_module.html#keepalive_requests).
* Using multiple instance of Tarantool servers on your multi-core machines.
* Turn off nginx, Tarantool unnecessary logging.
* Tune Linux Network.
* Tune nginx buffers.

[Content](#content)

## Examples
=================
Python test: test/basic_features.py, test/v20_feautres.py, nginx.dev.conf.

Client side javascript example: example/echo.html, example/echo.lua.

[Content](#content)

Please report bugs at https://github.com/tarantool/nginx_upstream_module/issues
We also warmly welcome your feedback in the discussion mailing list, tarantool@googlegroups.com.
