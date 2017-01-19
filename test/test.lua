json=require('json')
yaml=require('yaml')

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

box.cfg {
    log_level = 5;
    listen = 9999;
    wal_mode = 'none';
}

box.once('gr', function()
    box.schema.user.grant('guest', 'read,write,execute', 'universe')
end)

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

function update(request)
  return request
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
