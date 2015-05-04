json=require('json');

function echo(a)
  if type(a) == 'table' then
    return  {{a}}
  end
  return {a}
end

box.cfg {
    log_level = 5;
    listen = 10001;
}

if not box.space.tester then
    box.schema.user.grant('guest', 'read,write,execute', 'universe')
    box.schema.create_space('tester')
end
