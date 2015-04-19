json=require('json');

function echo(a)
  if type(a) == 'table' then
    return  {{a}}
  end
  return {a}
end

function big_echo()
  local out = {}
  for i = 0, 100000 do
    out[i] = "string";
  end
  return {{out}};
end

box.cfg {
    log_level = 5;
    listen = 9999;
}

if not box.space.tester then
    box.schema.user.grant('guest', 'read,write,execute', 'universe')
    box.schema.create_space('tester')
end
