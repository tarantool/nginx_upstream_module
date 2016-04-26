json=require('json');

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


box.cfg {
    log_level = 5;
    listen = 9999;
    wal_mode = 'none';
}

if not box.space.tester then
    box.schema.user.grant('guest', 'read,write,execute', 'universe')
    box.schema.create_space('tester')
end
