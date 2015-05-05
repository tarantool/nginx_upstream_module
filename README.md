# Tarantool NginX upstream module

  Key features:
  * United nginx features and tarantool features over HTTP(S).
  * Call tarantool methods via json rpc.
  * Load Balancing with elastic configuration.
  * Backup and fail tolerance.
  * Small overheads.
  
  Notice. Websocket currently not supported due to Tarantool limitation.

  About tarantool: http://tarantool.org
  
  About upstream: http://nginx.org/en/docs/http/ngx_http_upstream_module.html#upstream

## Status

Beta version.

## Protocol

  Module expects json posted over HTTP POST in request body.
  
  Server HTTP statuses
  
    OK - response body contains result or error;
         error may appear only if something happened within Tarantool,
         for instance: 'method not found'.
    
    INTERNAL SERVER ERROR - may appear in many cases,
                            most of them is 'out of memory' error;
    
    NOT ALLOWED - POST requests only are allowed.
    
    BAD REQUEST - json parse error, empty request body, etc.
    
    BAD GATEWAY - lost connection with Tarantool server(s);
                  Since both (i.e. json -> tp and tp -> json) parser works async,
                  this error may appear if 'params' or 'method' not exists in structure of incoming json,
                  see protocol description for more details.
                  
                  Notice. This behavior will change in future.

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
    $ git submodule init
    $ git submodule update --recursive
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

## Examples

  Python minimalistic example/test: test/client.py.
  
  Client side javascript example: example/echo.html.
  
  For those examples Tarantool must be launched with {example,test}/echo.lua and this module with "location = '/tnt'".




Please report bugs at https://github.com/tarantool/nginx_upstream_module/issues
We also warmly welcome your feedback in the discussion mailing list, tarantool@googlegroups.com.
