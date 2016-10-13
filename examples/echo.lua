box.cfg {
    log_level = 5;
    listen = 9999;
}

box.once('grant', function()
    box.schema.user.grant('guest', 'read,write,execute', 'universe')
    box.schema.create_space('tester')
end)

-- Table
local users = box.schema.space.create('users', {if_not_exists=true})
-- Indexes
users:create_index('user_id', {if_not_exists=true})

-- $ wget 127.0.0.1:8081/tnt --post-data='{"method":"add_user","params": ["Vasa","Soshnikov"]}'
function add_user(user_first_name, user_last_name)
  print(user_last_name, user_first_name)
  return users:auto_increment{user_first_name, user_last_name}
end

-- $ wget 127.0.0.1:8081/tnt --post-data='{"method":"get_user_by_id","params", [1]}'
function get_user_by_id(user_id)
  return users.index.user_id:get{user_id}
end

-- $ wget 127.0.0.1:8081/tnt --post-data='{"method":"get_users"}'
function get_users()
  return users:select{}
end

function echo(a)
  if type(a) == 'table' then
    return  {{a}}
  end
  return {a}
end
