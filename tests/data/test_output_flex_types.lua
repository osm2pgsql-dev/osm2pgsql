
local table = osm2pgsql.define_node_table("nodes", {
    { column = 'ttext', type = 'text' },
    { column = 'tbool', type = 'boolean' },
    { column = 'tint2', type = 'int2' },
    { column = 'tint4', type = 'int4' },
    { column = 'tint8', type = 'int8' },
    { column = 'treal', type = 'real' },
    { column = 'thstr', type = 'hstore' },
    { column = 'tdirn', type = 'direction' },
    { column = 'tsqlt', type = 'varchar' },
})

function osm2pgsql.process_node(object)
    if object.tags.type == 'nil' then
        table:add_row{}
        return
    end
    if object.tags.type == 'boolean' then
        local row = { tbool = true, tint2 = true, tint4 = true,
                      tint8 = true, tdirn = true }
        table:add_row(row)

        row = { tbool = false, tint2 = false, tint4 = false,
                tint8 = false, tdirn = false }
        table:add_row(row)
        return
    end
    if object.tags.type == 'boolean-fail' then
        table:add_row{ [object.tags.column] = true }
        return
    end
    if object.tags.type == 'number' then
        local numbers = { -2^31 - 1, -2^31, -2^31 + 1,
                          -2^15 - 1, -2^15, -2^15 + 1,
                          -2, -1, -0.5, 0, 0.5, 1, 2,
                          2^15 - 1, 2^15, 2^15 + 1,
                          2^31 - 1, 2^31, 2^31 + 1 }
        for _, n in ipairs(numbers) do
            table:add_row{
                ttext = n,
                tbool = n,
                tint2 = n,
                tint4 = n,
                tint8 = n,
                treal = n,
                tdirn = n,
                tsqlt = n,
            }
        end
        return
    end
    if object.tags.type == 'number-fail' then
        table:add_row{ [object.tags.column] = 1 }
        return
    end
    if object.tags.type == 'function-fail' then
        table:add_row{ [object.tags.column] = table.insert }
        return
    end
    if object.tags.type == 'table' then
        table:add_row{ thstr = {} }
        table:add_row{ thstr = { a = 'b', c = 'd' } }
        return
    end
    if object.tags.type == 'table-hstore-fail' then
        table:add_row{ thstr = { num = 1, bln = true } }
        return
    end
    if object.tags.type == 'table-fail' then
        table:add_row{ [object.tags.column] = {} }
        return
    end
end

