#!/usr/bin/env tarantool

json = require('json')
yaml = require('yaml')
os   = require('os')
fiber = require('fiber')

function echo(...)
  return ...
end

function echo_1(a)
  return {a}
end

function echo_2(a, b)
  return {a, b}
end

function rest_api(a, b)
  return echo_2(a, b)
end

function rest_api_get(a, b)
  return echo_2(a, b)
end

function echo_big(...)
  local a = {...}
  return a
end

function ret_4096()
  local out = {}
  for i = 0, 801 do
    out[i] = i;
  end
  return {{out, "101234567891234567"}};
end

function ret_4095()
  local out = {}
  for i = 0, 801 do
    out[i] = i;
  end
  return {{out, "10123456789123456"}};
end

function rest_api_parse_query_args(http_request_full)
  return {http_request_full}
end

-- BUG -- https://github.com/tarantool/nginx_upstream_module/issues/37 [[
function read(http_request_full)
    return {
        status = {
          code = 200,
          text = "OK"
        },
        meta = {
          debug = {
            front = "app-2",
            auth = true,
            source = false
          },
          exec_time = 0.005933,
          related = {
            "/ucp/0001/accounts/account/79031234567/services"
          }
        },
        data ={
          p2 = 79031234568,
          p1 = 79031234567,
          account = 79031234567
        },
        aux = {
          hello = "hello 79031234567"
        }
    }
end

function ucp(http_request_full)
    return read(http_request_full)
end
-- ]]

-- BUG -- https://github.com/tarantool/nginx_upstream_module/issues/44
function issue_44(request)
  local c = {}
  return c.a.b
end
-- ]]

function four_empty_strings()
  return {"", "", "", "", {a = ""}}
end

function issue_58(request)
  return true
end

-- BUG -- https://github.com/tarantool/nginx_upstream_module/issues/58
function delete(request, a1, a2)
  return request, a1, a2
end

function insert(request, a1, a2)
  return { request, a1, a2 }
end

function update(request, ...)
  return request, ...
end
-- ]]

function delete()
  return "ok"
end

function issue_59(request, ...)
  return { request, {...} }
end

function issue_71(request)
  return request
end

function pass_body_handler(request, ...)
  if request.body == nil then
    error('request == NIL')
  end
  return { request, { ... } }
end

function touch(req, ...)
  local out = {}
  for i = 0, 1801 do
    out[i] = i;
  end
  return {req, {out} }
end

function test_headers_out(req)
  if req.headers['x-added-header'] == nil then
    os.exit(1)
  end
  return true
end

function tnt_proxy(req, ...)

  local out = {...}
  for i = 0, 18012 do
    out[i] = i;
  end

  return
    {
      ngx = {
        tonumber(req.args.status_code) or 200,
        { ["X-Tarantool"] = "FROM_TNT" }
      }
    },
    req
--    out
end

function url_encoded(req, ...)
  return req, ...
end

function method_1(req)
  return req
end

function method_2(req)
  return req
end

function method_3(req)
  return req
end

-- Issue -- https://github.com/tarantool/nginx_upstream_module/issues/98
function error_if_escaped(req)
  local get_arg = req.args.getArg
  if get_arg == 'a+b' then
    error (string.format('regression, it should have +, getArg = %s',
      req.args.getArg))
  end
  return true
end

-- Issue - https://github.com/tarantool/nginx_upstream_module/issues/108
function issue_108(req)
  return req
end

function issue_120(request)
  return request
end

-- CFG
box.cfg {
    log_level = 5,
    listen = 9999,
    wal_mode = 'none',
}

-- FOR TESTING ONLY!!!
-- In real life you have to restrict an access
box.once('gr', function()
    box.schema.user.grant('guest', 'read,write,execute', 'universe')
end)

local t = box.schema.space.create('t', {if_not_exists=true})
t:create_index('pk', {if_not_exists=true})

t = box.schema.space.create('t2', {if_not_exists=true})
t:create_index('pk', {if_not_exists=true})

t = box.schema.space.create('t3', {if_not_exists=true})
t:create_index('pk', {if_not_exists=true})
i = t:create_index('sk', {parts={1,'unsigned', 2, 'str'}, if_not_exists=true})

t = box.schema.space.create('t4', {if_not_exists=true})
t:create_index('pk', {if_not_exists=true})

t = box.schema.space.create('t5', {if_not_exists=true})
t:create_index('pk', {if_not_exists=true, parts={1, 'str'}})
