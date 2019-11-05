function drop_all (...)
  return 1, {}
end

function test_nodes_1 (kv, num_tags)
  if kv["foo"] then
    tags = {}
    tags["bar"] = kv["foo"]
    return 0, tags
  else
    return 1, {}
  end
end

function test_nodes_2 (kv, num_tags)
  if kv["foo"] then
    tags = {}
    tags["baz"] = kv["foo"]
    return 0, tags
  else
    return 1, {}
  end
end

function test_line_1 (kv, num_tags)
  if kv["foo"] and kv["area"] == "false" then
    tags = {}
    tags["bar"] = kv["foo"]
    return 0, tags, 0, 0
  else
    return 1, {}, 0, 0
  end
end

function test_line_2 (kv, num_tags)
  if kv["foo"] and kv["area"] == "false" then
    tags = {}
    tags["baz"] = kv["foo"]
    return 0, tags, 0, 0
  else
    return 1, {}, 0, 0
  end
end

function test_polygon_1 (kv, num_tags)
  if kv["foo"] and kv["area"] == "true" then
    tags = {}
    tags["bar"] = kv["foo"]
    return 0, tags, 0, 0
  else
    return 1, {}, 0, 0
  end
end

function test_polygon_2 (kv, num_tags)
  if kv["foo"] and kv["area"] == "true" then
    tags = {}
    tags["baz"] = kv["foo"]
    return 0, tags, 0, 0
  else
    return 1, {}, 0, 0
  end
end
