function drop_all (...)
  return 1, {}
end

-- A generic way to process ways, given a function which determines if tags are interesting
-- Takes an optional function to process tags. Always says it's a polygon if there's matching tags
function test_ways (kv, num_keys)
  tags = {["foo"] = "bar"}
  return 0, tags, 1, 0
end
