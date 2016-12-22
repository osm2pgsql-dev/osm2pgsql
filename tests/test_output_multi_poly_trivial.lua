function drop_all (...)
  return 1, {}
end

function drop_ways (...)
  return 1, {}, 0, 0
end

function test_rels (kv, num_keys)
  tags = {["foo"] = "bar"}
  return 0, tags
end

function test_members (kv, member_tags, roles, num_members)
  membersuperseded = {}
  for i = 1, num_members do
    membersuperseded[i] = 0
  end

  tags = kv
  tags["bar"] = "baz"

  return 0, tags, membersuperseded, 0, 0, 0
end
