<a href="http://tarantool.org">
	<img src="https://avatars2.githubusercontent.com/u/2344919?v=2&s=250" align="right">
</a>

# Tarantool NginX upstream module
---------------------------------
Key features:
* Both nginx and Tarantool features accessible over HTTP(S).
* Tarantool methods callable via JSON-RPC or REST.
* Load balancing with elastic configuration.
* Backup and fault tolerance.
* Low overhead.

See more about:
* [Tarantool](http://tarantool.org)
* [nginx upstream](http://nginx.org/en/docs/http/ngx_http_upstream_module.html#upstream)

# Limitations
-------------
1. WebSockets are not supported until Tarantool supports out-of-band replies.
2. This module does not support Tarantool 1.6.x starting with 2.4.0.
   Since then it uses Tarantool 1.7 protocol features.

## Docker images
----------------
Tarantool NginX upstream module:
https://hub.docker.com/r/tarantool/tarantool-nginx

Tarantool:
https://hub.docker.com/r/tarantool/tarantool

## Status
---------
* v0.1.4 - Production ready.
* v0.2.0 - Stable.
* v0.2.1 - Production ready.
* v0.2.2 - Stable.
* v2.3.1 - Production ready.
* v2.3.2 - production ready.
* v2.3.7 - Production ready.
* v2.4.0-beta - Beta.
* v2.4.6-rc1 - Stable.
* v2.5-rc{1,2} - Stable.
* v2.5-stable - Stable.
* v2.6-rc3 - Stable.

## Contents
-----------
* [How to install](#how-to-install)
* [REST](#rest)
* [JSON](#json)
* [HTTP headers and status](#http-headers-and-status)
* [Directives](#directives)
  * [tnt_pass](#tnt_pass)
  * [tnt_http_methods](#tnt_http_methods)
  * [tnt_http_rest_methods](#tnt_http_rest_methods)
  * [tnt_pass_http_request](#tnt_pass_http_request)
  * [tnt_pass_http_request_buffer_size](#tnt_pass_http_request_buffer_size)
  * [tnt_method](#tnt_method)
  * [tnt_set_header](#tnt_set_header)
  * [tnt_send_timeout](#tnt_send_timeout)
  * [tnt_read_timeout](#tnt_read_timeout)
  * [tnt_buffer_size](#tnt_buffer_size)
  * [tnt_next_upstream](#tnt_next_upstream)
  * [tnt_connect_timeout](#tnt_connect_timeout)
  * [tnt_next_upstream](#tnt_next_upstream)
  * [tnt_next_upstream_tries](#tnt_next_upstream_tries)
  * [tnt_next_upstream_timeout](#tnt_next_upstream_timeout)
  * [tnt_pure_result](#tnt_pure_result)
  * [tnt_multireturn_skip_count](#tnt_multireturn_skip_count)
  * [Format](#format)
  * [tnt_insert](#tnt_insert)
  * [tnt_replace](#tnt_replace)
  * [tnt_delete](#tnt_delete)
  * [tnt_select](#tnt_select)
  * [tnt_select_limit_max](#tnt_select_limit_max)
  * [tnt_allowed_spaces](#tnt_allowed_spaces)
  * [tnt_allowed_indexes](#tnt_allowed_indexes)
  * [tnt_update](#tnt_update)
  * [tnt_upsert](#tnt_upsert)
* [Performance tuning](#performance-tuning)
* [Examples](#examples)
* [Copyright & license](#copyright--license)
* [See also](#see-also)
* [Contacts](#contacts)

## How to install
-----------------

### Build from source (Development version)

```bash
git clone https://github.com/tarantool/nginx_upstream_module.git nginx_upstream_module
cd nginx_upstream_module
git submodule update --init --recursive
git clone https://github.com/nginx/nginx.git nginx

# Ubuntu
apt-get install libpcre++0 gcc unzip libpcre3-dev zlib1g-dev libssl-dev libxslt-dev

make build-all
```

[Back to contents](#contents)

### Build via nginx 'configure'

  Requirements (for details, see REPO_ROOT/Makefile)

    libyajl >= 2.0(https://lloyd.github.io/yajl/)
    libmsgpuck >= 2.0 (https://github.com/rtsisyk/msgpuck)

    $ ./configure --add-module=REPO_ROOT && make

### Install on Mac OS X

```bash
brew tap denji/nginx
brew install nginx-full --with-tarantool-module
```

## Configure

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

[Back to contents](#contents)

## REST
-------

**Note:** since v0.2.0

With this module, you can call Tarantool stored procedures via HTTP
REST methods (GET, POST, PUT, PATCH, DELETE).

Example:

```nginx
    upstream backend {
      # Tarantool hosts
      server 127.0.0.1:9999;
    }

    server {
      # HTTP [GET | POST | PUT | PATCH | DELETE] /tnt_rest?q=1&q=2&q=3
      location /tnt_rest {
        # REST mode on
        tnt_http_rest_methods get post put patch delete; # or all

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

[Back to contents](#contents)

## JSON
-------

**Note:** since v0.1.4

The module expects JSON posted with HTTP POST, PUT (since v0.2.0),
or PATCH (since v2.3.8) and carried in request body.

Server HTTP statuses:

* **OK** - response body contains a result or an error;
  the error may appear only if something wrong happened within Tarantool,
  for instance: 'method not found'.
* **INTERNAL SERVER ERROR** - may appear in many cases,
  most of them being 'out of memory' error.
* **NOT ALLOWED** - in response to anything but a POST request.
* **BAD REQUEST** - JSON parse error, empty request body, etc.
* **BAD GATEWAY** - lost connection to Tarantool server(s).
  Since both (i.e. json -> tp and tp -> json) parsers work
  asynchronously, this error may appear if 'params' or 'method'
  does not exists in the structure of the incoming JSON, please
  see the protocol description for more details.

  **Note:** this behavior will change in the future.

### Input JSON form

```
[ { "method": STR, "params":[arg0 ... argN], "id": UINT }, ...N ]
```

* **method** - a string containing the name of the Tarantool method to be
  invoked (i.e. Tarantool "call")
* **params** - a structured array. Each element is an argument of the Tarantool
  "call".
* **id** - an identifier established by the Client. MUST contain an unsigned
  number not greater than unsigned int. May be 0.

These all are required fields.

### Output JSON form

```
[ { "result": JSON_RESULT_OBJECT, "id":UINT, "error": { "message": STR, "code": INT } }, ...N ]
```

* **result** - Tarantool execution result (a json object/array, etc).
  Version 2.4.0+ outputs a raw result, i.e. ``JSON_RESULT_OBJECT``.
  May be null or undefined.
* **id** - DEPRECATED in 2.4.0+ - request id.
  May be null or undefined.
* **error** - a structured object which contains an internal error message.
  This field exists only if an internal error occurred, for instance:
  "too large request", "input json parse error", etc.

  If this field exists, the input message was _probably_ not passed to
  the Tarantool backend.

  See "message"/"code" fields for details.

### Example

For instance, Tarantool has a stored procedure `echo`:

```Lua
function echo(a, b, c, d)
  return a, b, c, d
end
```

Syntax:

```
--> data sent to Server
<-- data sent to Client
```

rpc call 1:
```
--> { "method": "echo", "params": [42, 23], "id": 1 }
<-- { "id": 1, "result": [42, 23]
```

rpc call 2:
```
--> { "method": "echo", "params": [ [ {"hello": "world"} ], "!" ], "id": 2 }
<-- { "id": 2, "result": [ {"hello": "world"} ], "!" ]}
```

rpc call of a non-existent method:
```
--> { "method": "echo_2", "id": 1 }
<-- { "error": {"code": -32601, "message": "Method not found"}, "id": 1 }
```

rpc call with invalid JSON:
```
--> { "method": "echo", "params": [1, 2, 3, __wrong__ ] }
<-- { "error": { "code": -32700, "message": "Parse error" } }
```

rpc call Batch:
```
--> [
      { "method": "echo", "params": [42, 23], "id": 1 },
      { "method": "echo", "params": [ [ {"hello": "world"} ], "!" ], "id": 2 }
]
<-- [
      { "id": 1, "result": [42, 23]},
      { "id": 2, "result" : [{"hello": "world"} ], "!" ]},
]
```

rpc call Batch of a non-existent method:
```
--> [
      { "method": "echo_2", "params": [42, 23], "id": 1 },
      { "method": "echo", "params": [ [ {"hello": "world"} ], "!" ], "id": 2 }
]
<-- [
      { "error": {"code": -32601, "message": "Method not found"}, "id": 1 },
      {"id": 2, "result": [ {"hello": "world"} ], "!" ]}
]
```

rpc call Batch with invalid JSON:
```
--> [
      { "method": "echo", "params": [42, 23, __wrong__], "id": 1 },
      { "method": "echo", "params": [ [ {"hello": "world"} ], "!" ], "id": 2 }
]
<-- { "error": { "code": -32700, "message": "Parse error" } }
```

[Back to contents](#contents)

## HTTP headers and status
--------------------------

Sometimes you have to set status or headers which came from Tarantool.
For this purpose, you have to use something like
[ngx_lua](https://github.com/openresty/lua-nginx-module)
or [ngx_perl](http://nginx.org/en/docs/http/ngx_http_perl_module.html), etc.

With the methods, you can also transform the result from `Tarantool` into
something else.

Here is an example with `ngx_lua`:

```Lua
  -- Tarantool, stored procedure
  function foo(req, ...)
    local status = 200
    local headers = {
      ["X-Tarantool"] = "FROM_TNT",
    }
    local body = 'It works!'
    return status, headers, body
  end
```

```nginx
  # Nginx, configuration

  # If you're experience an problem with lua-resty-core like
  # https://github.com/openresty/lua-nginx-module/issues/1509
  # it can be disabled by the following directive.
  #
  # lua_load_resty_core off;

  # If you're not using lua-resty-core you may need to manually specify a path
  # to cjson module. See the documentation:
  # https://github.com/openresty/lua-nginx-module#lua_package_cpath
  #
  # lua_package_cpath "/path/in/lua/cpath/format/?.so";

  upstream tnt_upstream {
     server 127.0.0.1:9999;
     keepalive 10000;
  }

  location /tnt_proxy {
    internal;
    tnt_method foo;
    tnt_buffer_size 100k;
    tnt_pass_http_request on parse_args;
    tnt_pass tnt_upstream;
  }

  location /api {
    default_type application/json;
    rewrite_by_lua '

       local cjson = require("cjson")

       local map = {
         GET = ngx.HTTP_GET,
         POST = ngx.HTTP_POST,
         PUT = ngx.HTTP_PUT,
         -- ...
       }
       -- hide `{"params": [...]}` from a user
       ngx.req.read_body()
       local body = ngx.req.get_body_data()
       if body then
            body = "{\\"params\\": [" .. body .. "]}"
       end
       local res = ngx.location.capture("/tnt_proxy", {
         args = ngx.var.args,
         method = map[ngx.var.request_method],
         body = body
       })

       if res.status == ngx.HTTP_OK then
         local answ = cjson.decode(res.body)

         -- Read reply
         local result = answ["result"]

         if result ~= nil then
           ngx.status = result[1]
           for k, v in pairs(result[2]) do
             ngx.header[k] = v
           end

           local body = result[3]
           if type(body) == "string" then
             ngx.header["content_type"] = "text/plain"
             ngx.print(body)
           elseif type(body) == "table" then
             local body = cjson.encode(body)
             ngx.say(body)
           else
             ngx.status = 502
             ngx.say("Unexpected response from Tarantool")
           end
         else
           ngx.status = 502
           ngx.say("Tarantool does not work")
         end

         -- Finalize execution
         ngx.exit(ngx.OK)
       else
         ngx.status = res.status
         ngx.say(res.body)
       end
       ';
    }

```

[Back to contents](#contents)

## Directives
-------------

tnt_pass
--------
**syntax:** *tnt_pass UPSTREAM*

**default:** *no*

**context:** *location*

Specify the Tarantool server backend.

```nginx

  upstream tnt_upstream {
     127.0.0.1:9999
  };

  location = /tnt {
    tnt_pass 127.0.0.1:9999;
  }

  location = /tnt_next_location {
     tnt_pass tnt_upstream;
  }
```

[Back to contents](#contents)

tnt_http_methods
----------------
**syntax:** *tnt_http_methods post, put, patch, delete, all*

**default:** *post, delete*

**context:** *location*

Allow to accept one or many http methods.
If a method is allowed, the module expects [JSON](#json) carried in the request
body.
If `tnt_method` is not set, then the name of the Tarantool stored procedure is
the protocol [JSON](#json).

Example:

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

[Back to contents](#contents)

tnt_http_rest_methods
---------------------
**syntax:** *tnt_http_rest_methods get, post, put, patch, delete, all*

**default:** *no*

**context:** *location*

**NOTICE:**
This does not restrict anything. The option just says to NGINX:
use this methods for allowing REST requests.

If you have a wish to set some methods as not allowed methods, then
please use "if" inside locations.

For example:
```nginx
if ($request_method !~ ^(GET|POST|HEAD)$) {
    return 405 "Please use HEAD, PATCH and so on";
}
```

Allow to accept one or more REST methods.
If `tnt_method` is not set, then the name of the Tarantool stored procedure is
the first part of the URL path.

Example:

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

[Back to contents](#contents)

tnt_pass_http_request
---------------------
**syntax:** *tnt_pass_http_request [on|off|parse_args|unescape|pass_body|pass_headers_out|parse_urlencoded|pass_subrequest_uri]*

**default:** *off*

**context:** *location, location if*

Allow to pass HTTP headers and queries to Tarantool stored procedures.

Examples #1:

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

Examples #2 (pass_headers_out):

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

Examples #3 (parse_urlencoded):

```nginx
  location /tnt {
    tnt_http_rest_methods post;
    tnt_pass_http_request on parse_urlencoded;
    tnt_method tarantool_stored_procedure_name;
    tnt_pass 127.0.0.1:9999;
  }
```
```lua
  function tarantool_stored_procedure_name(req, ...)
    req.headers -- a lua table
    req.query -- a string
    req.args.q -- 1
    req.args_urlencoded.p -- 2
    return true
  end
```

```bash
  # Call tarantool_stored_procedure_name()
  $> wget NGINX_HOST/tarantool_stored_procedure_name/some/mega/path?q=1 --post-data='p=2'
```

Examples #4 (pass_subrequest_uri):

* Origin (unparsed) uri
```nginx
  location /web {
    # Backend processing /web/foo and replying with X-Accel-Redirect to
    # internal /tnt/bar
    proxy_pass http://x-accel-redirect-backend;
  }
  location /tnt {
    internal;
    tnt_pass_http_request on;
    tnt_method tarantool_xar_handler;
    tnt_pass 127.0.0.1:9999;
  }
```
```lua
  function tarantool_xar_handler(req, ...)
    print(req.uri) -- /web/foo
    return true
  end
```
* Subrequest uri
```nginx
  location /web {
    # Backend processing /web/foo and replying with X-Accel-Redirect to
    # internal /tnt/bar
    proxy_pass http://x-accel-redirect-backend;
  }
  location /tnt {
    internal;
    tnt_pass_http_request on pass_subrequest_uri;
    tnt_method tarantool_xar_handler;
    tnt_pass 127.0.0.1:9999;
  }
```
```lua
  function tarantool_xar_handler(req, ...)
    print(req.uri) -- /tnt/bar
    return true
  end
```

[Back to contents](#contents)

tnt_pass_http_request_buffer_size
---------------------------------
**syntax:** *tnt_pass_http_request_buffer_size SIZE*

**default:** *4k, 8k*

**context:** *location*

Specify the size of the buffer used for `tnt_pass_http_request`.

[Back to contents](#contents)

tnt_method
----------
**syntax:** *tnt_method STR*

**default:** *no*

**context:** *location, location if*

Specify the Tarantool call method. It can take a nginx's variable.

Examples:

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

  location ~ /api/([-_a-zA-Z0-9/]+)/ {
    # Also tnt_pass_http_request can mix with JSON communication [[
    tnt_http_rest_methods get;
    tnt_method $1;
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

  function call(req, ...)
    req.headers -- lua table
    req.query -- string
    return req, ...
  end
```
```bash
  # OK Call tarantool_stored_procedure_name()
  $> wget NGINX_HOST/tarantool_stored_procedure_name/some/mega/path?q=1

  # Error Call tarantool_stored_procedure_XXX()
  $> wget NGINX_HOST/tarantool_stored_procedure_XXX/some/mega/path?q=1

  # OK Call api_function
  $> wget NGINX_HOST/api/call/path?q=1

```

[Back to contents](#contents)

tnt_set_header
--------------
**syntax:** *tnt_set_header STR STR*

**default:** *no*

**context:** *location, location if*

Allows redefining or appending fields to the request header passed to the
Tarantool proxied server.
The value can contain text, variables, and their combinations.

Examples:

```nginx
  location tnt {
    # Also tnt_pass_http_request can mix with JSON communication [[
    tnt_http_rest_methods get;
    tnt_method tarantool_stored_procedure_name;
    #]]

    tnt_set_header X-Host $host;
    tnt_set_header X-GEO-COUNTRY $geoip_country_code;

    # [on|of]
    tnt_pass_http_request on;
    tnt_pass 127.0.0.1:9999;
  }

```
```lua
  function tarantool_stored_procedure_name(req, ...)
    req.headers['X-Host'] -- a hostname
    req.headers['X-GEO-COUNTRY'] -- a geo country
    return { 'OK' }
  end
```
```bash
  # OK Call tarantool_stored_procedure_name()
  $> wget NGINX_HOST/tarantool_stored_procedure_name/some/mega/path?q=1
```

[Back to contents](#contents)

tnt_send_timeout
----------------
**syntax:** *tnt_send_timeout TIME*

**default:** *60s*

**context:** *http, server, location*

The timeout for sending TCP requests to the Tarantool server, in seconds by
default.

It's wise to always explicitly specify the time unit to avoid confusion.
Time units supported are:
`s`(seconds), `ms`(milliseconds), `y`(years), `M`(months), `w`(weeks),
`d`(days), `h`(hours), and `m`(minutes).

[Back to contents](#contents)

tnt_read_timeout
-------------------
**syntax:** *tnt_read_timeout TIME*

**default:** *60s*

**context:** *http, server, location*

The timeout for reading TCP responses from the Tarantool server, in seconds by
default.

It's wise to always explicitly specify the time unit to avoid confusion.
Time units supported are: `s`(seconds), `ms`(milliseconds), `y`(years),
`M`(months), `w`(weeks), `d`(days), `h`(hours), and `m`(minutes).

[Back to contents](#contents)

tnt_connect_timeout
-------------------
**syntax:** *tnt_connect_timeout TIME*

**default:** *60s*

**context:** *http, server, location*

The timeout for connecting to the Tarantool server, in seconds by default.

It's wise to always explicitly specify the time unit to avoid confusion.
Time units supported are: `s`(seconds), `ms`(milliseconds), `y`(years),
`M`(months), `w`(weeks), `d`(days), `h`(hours), and `m`(minutes).
This time must be strictly less than 597 hours.

[Back to contents](#contents)

tnt_buffer_size
---------------
**syntax:** *tnt_buffer_size SIZE*

**default:** *4k, 8k*

**context:** *http, server, location*

This buffer size is used for reading Tarantool replies,
but it's not required to be as big as the largest possible Tarantool reply.

[Back to contents](#contents)

tnt_next_upstream
--------------------
**syntax:** *tnt_next_upstream [ error | timeout | invalid_response | off ]*

**default:** *error timeout*

**context:** *http, server, location*

Specify which failure conditions should cause the request to be forwarded to
another upstream server. Applies only when the value in [tnt_pass](#tnt_pass)
is an upstream with two or more servers.

[Back to contents](#contents)

tnt_next_upstream_tries
-----------------------
**syntax:** *tnt_next_upstream_tries SIZE*

**default:** *0*

**context:** *http, server, location*

Limit the number of possible tries for passing a request to the next server.
The 0 value turns off this limitation.

[Back to contents](#contents)

tnt_next_upstream_timeout
-------------------------
**syntax:** *tnt_next_upstream_timeout TIME*

**default:** *0*

**context:** *http, server, location*

Limit the time during which a request can be passed to the next server.
The 0 value turns off this limitation.

[Back to contents](#contents)

tnt_pure_result
---------------
**syntax:** *tnt_pure_result [on|off]*

**default:** *off*

**context:** *http, server, location*

Whether to wrap Tarantool response or not.

When this option is off:
```
{"id":0, "result": [ 1 ]}
```
When this option is on:
```
[[1]]
```

[Back to contents](#contents)

tnt_multireturn_skip_count
--------------------------

**DEPRECATED in 2.4.0+, RETURNED IN 2.5.0-rc2+**

**syntax:** *tnt_multireturn_skip_count [0|1|2]*

**default:** *0*

**context:** *http, server, location*

**Note:** Use this option wisely, it does not validate the outgoing JSON!
For details you can check this issue:
https://github.com/tarantool/nginx_upstream_module/issues/102

The module will skip one or more multireturn parts when this option is > 0.

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

[Back to contents](#contents)

Format
------

**syntax:** *tnt_{OPT} [ARGS] [FMT]*

Tarantool stores data in [tuples](https://tarantool.org/en/doc/1.7/book/box/data_model.html#tuple).
A tuple is a list of elements. Each element is a value or an object,
and each element should have a strong type.
The tuple format is called [MsgPack](https://en.wikipedia.org/wiki/MessagePack),
it's like JSON in a binary format.

The main goal of Format (see [FMT] above) is to enable conversion between
a query string and MsgPack without losing type information or value.

The syntax is: `{QUERY_ARG_NAME}=%{FMT_TYPE}`

Please look carefully for yours url encoding!

A good example is (also see examples [tnt_update](#tnt_update) and [tnt_upsert](#tnt_upsert)):
```
HTTP GET ... /url?space_id=512&value=some+string
it could be matched by using the following format 'space_id=%%space_id,value=%s'
```
Also this works with HTTP forms, i.e. HTTP POST, HTTP PUT and so on.

Here is a full list of {FMT_TYPE} types:

```
TUPLES

%n - int64
%f - float
%d - double
%s - string
%b - boolean

Special types

%%space_id - space_id
%%idx_id - index_id
%%off - [select](#tnt_select) offset
%%lim - [select](#tnt_select) limit
%%it - [select](#tnt_select) iterator type, allowed values are:
                             eq,req,all,lt,le,ge,gt,all_set,any_set,
                             all_non_set,overlaps,neighbor

KEYS (for [tnt_update](#tnt_update))

%kn - int64
%kf - float
%kd - double
%ks - string
%kb - boolean

Operations (for [tnt_upsert](#tnt_upsert))

%on - int64
%of - float
%od - double
%os - string
%ob - boolean

```

Examples can be found at:

* `examples/simple_rest_client.py`
* `examples/simple_rest_client.sh`

[Back to contents](#contents)

tnt_insert
----------
**syntax:** *tnt_insert [SIZE or off] [FMT]*

**default:** *None*

**context:** *location, location if*

**HTTP methods** *GET, POST, PUT, PATCH, DELETE*

**Content-Typer** *default, application/x-www-form-urlencoded*

This directive allows executing an insert query with Tarantool.

* The first argument is a space id.
* The second argument is a [format](#format) string.

Returns HTTP code 4XX if client's request doesn't well formatted. It means, that
this error raised if some of values missed or has wrong type.

Returns HTTP code 5XX if upstream is dead (no ping).

Also it can return an HTTP code 200 with an error formatted in JSON.
It happens when Tarantool can't issue a query.

Here is a description:
```
 {"error": { "message": STR, "code": INT } }
```

* **error** - a structured object which contains an internal error message.
  This field exists only if an internal error occurred, for instance:
  "too large request", "input json parse error", etc.

  If this field exists, the input message was _probably_ not passed to
  the Tarantool backend.

  See "message"/"code" fields for details.

Examples can be found at:

* `simple_rest_client.py`
* `simple_rest_client.sh`


[Back to contents](#contents)

tnt_replace
-----------
**syntax:** *tnt_replace [SIZE or off] [FMT]*

**default:** *None*

**context:** *location, location if*

**HTTP methods** *GET, POST, PUT, PATCH, DELETE*

**Content-Typer** *default, application/x-www-form-urlencoded*

This directive allows executing a replace query with Tarantool.

* The first argument is a space id.
* The second argument is a [format](#format) string.

Returns HTTP code 4XX if client's request doesn't well formatted. It means, that
this error raised if some of values missed or has wrong type.

Returns HTTP code 5XX if upstream is dead (no ping).

Also it can return an HTTP code 200 with an error formatted in JSON.
It happens when Tarantool can't issue a query.

Here is a description:
```
 {"error": { "message": STR, "code": INT } }
```

* **error** - a structured object which contains an internal error message.
  This field exists only if an internal error occurred, for instance:
  "too large request", "input json parse error", etc.

  If this field exists, the input message was _probably_ not passed to
  the Tarantool backend.

  See "message"/"code" fields for details.

Examples can be found at:

* `examples/simple_rest_client.py`
* `examples/simple_rest_client.sh`

[Back to contents](#contents)

tnt_delete
----------
**syntax:** *tnt_delete [SIZE or off] [SIZE or off] [FMT]*

**default:** *None*

**context:** *location, location if*

**HTTP methods** *GET, POST, PUT, PATCH, DELETE*

**Content-Typer** *default, application/x-www-form-urlencoded*

This directive allows executing a delete query with Tarantool.

* The first argument is a space id.
* The second argument is an index id.
* The third argument is a [format](#format) string.

Returns HTTP code 4XX if client's request doesn't well formatted. It means, that
this error raised if some of values missed or has wrong type.

Returns HTTP code 5XX if upstream is dead (no ping).

Also it can return an HTTP code 200 with an error formatted in JSON.
It happens when Tarantool can't issue a query.

Here is a description:
```
 {"error": { "message": STR, "code": INT } }
```

* **error** - a structured object which contains an internal error message.
  This field exists only if an internal error occurred, for instance:
  "too large request", "input json parse error", etc.

  If this field exists, the input message was _probably_ not passed to
  the Tarantool backend.

  See "message"/"code" fields for details.

Examples can be found at:

* `examples/simple_rest_client.py`
* `examples/simple_rest_client.sh`


[Back to contents](#contents)

tnt_select
----------
**syntax:** *tnt_select [SIZE or off] [SIZE or off] [SIZE] [SIZE] [ENUM] [FMT]*

**default:** *None*

**context:** *location, location if*

**HTTP methods** *GET, POST, PUT, PATCH, DELETE*

**Content-Typer** *default, application/x-www-form-urlencoded*

This directive allows executing a select query with Tarantool.

* The first argument is a space id.
* The second argument is an index id.
* The third argument is an offset.
* The fourth argument is an limit.
* The fifth argument is an iterator type, allowed values are:
  `eq`, `req`, `all`, `lt` ,`le`,`ge`, `gt`, `all_set`, `any_set`,
  `all_non_set`, `overlaps`, `neighbor`.
* The six argument is a [format](#format) string.

Returns HTTP code 4XX if client's request doesn't well formatted. It means, that
this error raised if some of values missed or has wrong type.

Returns HTTP code 5XX if upstream is dead (no ping).

Also it can return an HTTP code 200 with an error formatted in JSON.
It happens when Tarantool can't issue a query.

Here is a description:
```
 {"error": { "message": STR, "code": INT } }
```

* **error** - a structured object which contains an internal error message.
  This field exists only if an internal error occurred, for instance:
  "too large request", "input json parse error", etc.

  If this field exists, the input message was _probably_ not passed to
  the Tarantool backend.

  See "message"/"code" fields for details.

Examples can be found at:

* `examples/simple_rest_client.py`
* `examples/simple_rest_client.sh`

[Back to contents](#contents)

tnt_select_limit_max
--------------------
**syntax:** *tnt_select_limit_max [SIZE]*

**default:** *100*

**context:** *server, location, location if*

**HTTP methods** *GET, POST, PUT, PATCH, DELETE*

**Content-Typer** *default, application/x-www-form-urlencoded*

This is a constraint to avoid *large selects*. This is the maximum number
of returned tuples per select operation. If the client reaches this limit, then
the client gets an error on its side.

Returns HTTP code 4XX if client's request doesn't well formatted. It means, that
this error raised if some of values missed or has wrong type.

Returns HTTP code 5XX if upstream is dead (no ping).

Also it can return an HTTP code 200 with an error formatted in JSON.
It happens when Tarantool can't issue a query.

Here is a description:
```
 {"error": { "message": STR, "code": INT } }
```

* **error** - a structured object which contains an internal error message.
  This field exists only if an internal error occurred, for instance:
  "too large request", "input json parse error", etc.

  If this field exists, the input message was _probably_ not passed to
  the Tarantool backend.

  See "message"/"code" fields for details.

Examples can be found at:

* `examples/simple_rest_client.py`
* `examples/simple_rest_client.sh`

[Back to contents](#contents)

tnt_allowed_spaces
------------------
**syntax:** *tnt_allowed_spaces [STR]*

**default:** **

**context:** *server, location, location if*

This is a constraint to prohibit access to some spaces. The directive takes an
array of Tarantool space id-s (numbers), and each space in the list is allowed
to access from the client side.

Example:

```
location {
  ...
  tnt_allowed_spaces 512,523;
  tnt_insert off "s=%%space_id,i=%%idx_id";
}
```

[Back to contents](#contents)

tnt_allowed_indexes
-------------------
**syntax:** *tnt_allowed_indexes [STR]*

**default:** **

**context:** *server, location, location if*

This directive works like [tnt_allowed_spaces], but for indexes.

[Back to contents](#contents)

tnt_update
----------
**syntax:** *tnt_update [SIZE or off] [KEYS] [FMT]*

**default:** *None*

**context:** *location, location if*

**HTTP methods** *GET, POST, PUT, PATCH, DELETE*

**Content-Typer** *default, application/x-www-form-urlencoded*

This directive allows executing an update query with Tarantool.

* The first argument is a space id.
* The second argument is a [KEYS (for UPDATE)](#format) string.
it has special request form: [OPERATION_TYPE],[FIELDNO],[VALUE]
```
Possible OPERATION_TYPE (char) are:
 + for addition (values must be numeric)
 - for subtraction (values must be numeric)
 & for bitwise AND (values must be unsigned numeric)
 | for bitwise OR (values must be unsigned numeric)
 ^ for bitwise XOR (values must be unsigned numeric)
 : for string splice
 ! for insertion
 # for deletion
 = for assignment

FIELDNO (number) -  what field the operation will apply to. The field number can
be negative, meaning the position from the end of tuple. (#tuple + negative field number + 1)

VALUE (int64, float, double, string, boolean) – what value will be applied

More details could be found here [tarantool.org] (https://tarantool.org/en/doc/1.7/book/box/box_space.html?highlight=update#lua-function.space_object.update)
```
Before go further and start use this feature please read an example for this
section [1].

* The third argument is a [format](#format) string.

Examples can be found at:

* `examples/simple_rest_client.py`
* `examples/simple_rest_client.sh`

Returns HTTP code 4XX if client's request doesn't well formatted. It means, that
this error raised if some of values missed or has wrong type.

Returns HTTP code 5XX if upstream is dead (no ping).

Also it can return an HTTP code 200 with an error formatted in JSON.
It happens when Tarantool can't issue a query.

Here is a description:
```
 {"error": { "message": STR, "code": INT } }
```

* **error** - a structured object which contains an internal error message.
  This field exists only if an internal error occurred, for instance:
  "too large request", "input json parse error", etc.

  If this field exists, the input message was _probably_ not passed to
  the Tarantool backend.

  See "message"/"code" fields for details.

[Back to contents](#contents)

tnt_upsert
----------
**syntax:** *tnt_upsert [SIZE or off] [FMT] [OPERATIONS]*

**default:** *None*

**context:** *location, location if*

**HTTP methods** *GET, POST, PUT, PATCH, DELETE*

**Content-Typer** *default, application/x-www-form-urlencoded*

This directive allows executing an upsert query with Tarantool.

* The first argument is a space id.
* The second argument is a [format](#format) string.
* The third argument is a [OPERATIONS (for UPSERT)](#format) string.
it has special request form: [OPERATION_TYPE],[FIELDNO],[VALUE]
```
Possible OPERATION_TYPE (char) are:
 + for addition (values must be numeric)
 - for subtraction (values must be numeric)
 & for bitwise AND (values must be unsigned numeric)
 | for bitwise OR (values must be unsigned numeric)
 ^ for bitwise XOR (values must be unsigned numeric)
 : for string splice
 ! for insertion
 # for deletion
 = for assignment

FIELDNO (number) -  what field the operation will apply to. The field number can
be negative, meaning the position from the end of tuple. (#tuple + negative field number + 1)

VALUE (int64, float, double, string, boolean) – what value will be applied

More details could be found here [tarantool.org] (https://tarantool.org/en/doc/1.7/book/box/box_space.html?highlight=update#box-space-upsert)
```

Before go further and start use this feature please read an example for this
section [1].

[1] Example

Examples can be found at:

* `examples/simple_rest_client.py`
* `examples/simple_rest_client.sh`

Returns HTTP code 4XX if client's request doesn't well formatted. It means, that
this error raised if some of values missed or has wrong type.

Returns HTTP code 5XX if upstream is dead (no ping).

Also it can return an HTTP code 200 with an error formatted in JSON.
It happens when Tarantool can't issue a query.

Here is a description:
```
 {"error": { "message": STR, "code": INT } }
```

* **error** - a structured object which contains an internal error message.
  This field exists only if an internal error occurred, for instance:
  "too large request", "input json parse error", etc.

  If this field exists, the input message was _probably_ not passed to
  the Tarantool backend.

  See "message"/"code" fields for details.


[Back to contents](#contents)

## Examples
-----------

Python test: `test/basic_features.py`, `test/v20_feautres.py`, `nginx.dev.conf`.

Client-side javascript example: `example/echo.html`, `example/echo.lua`.

[Back to contents](#contents)

## Performance tuning
---------------------

* Use [HttpUpstreamKeepaliveModule](http://wiki.nginx.org/HttpUpstreamKeepaliveModule).
  * Use [keepalive](http://nginx.org/en/docs/http/ngx_http_upstream_module.html#keepalive).
  * Use [keepalive_requests](http://nginx.org/en/docs/http/ngx_http_core_module.html#keepalive_requests).
* Use multiple instances of Tarantool servers on your multi-core machines.
* Turn off unnecessary logging in Tarantool and NginX.
* Tune Linux network.
* Tune nginx buffers.

[Back to contents](#contents)

## Copyright & license
----------------------

[LICENSE](https://github.com/tarantool/nginx_upstream_module/blob/master/LICENSE)

[Back to contents](#contents)

## See also
-----------

* [Tarantool](http://tarantool.org) homepage.
* [lua-resty-tarantool](https://github.com/perusio/lua-resty-tarantool)
* Tarantool [protocol](http://tarantool.org/doc/dev_guide/box-protocol.html?highlight=protocol)

[Back to contents](#contents)

## Contacts

Please report bugs at https://github.com/tarantool/nginx_upstream_module/issues.

We also warmly welcome your feedback in the discussion mailing list,
tarantool@googlegroups.com

[Back to contents](#contents)
