
local test_table = osm2pgsql.define_node_table("nodes", {
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
    local test_type = object.tags.type

    if test_type == 'nil' then
        test_table:add_row()
        return
    end
    if test_type == 'boolean' then
        local row = { tbool = true, tint2 = true, tint4 = true,
                      tint8 = true, tdirn = true }
        test_table:add_row(row)

        row = { tbool = false, tint2 = false, tint4 = false,
                tint8 = false, tdirn = false }
        test_table:add_row(row)
        return
    end
    if test_type == 'boolean-fail' then
        test_table:add_row{ [object.tags.column] = true }
        return
    end
    if test_type == 'number' then
        local numbers = { -2^31 - 1, -2^31, -2^31 + 1,
                          -2^15 - 1, -2^15, -2^15 + 1,
                          -2, -1, -0.5, 0, 0.5, 1, 2,
                          2^15 - 1, 2^15, 2^15 + 1,
                          2^31 - 1, 2^31, 2^31 + 1 }
        for _, n in ipairs(numbers) do
            test_table:add_row{
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
    if test_type == 'number-fail' then
        test_table:add_row{ [object.tags.column] = 1 }
        return
    end
    if test_type == 'string-bool' then
        test_table:add_row{ tbool = '1',     ttext = 'istrue' };
        test_table:add_row{ tbool = 'yes',   ttext = 'istrue' };
        test_table:add_row{ tbool = 'true',  ttext = 'istrue' };

        test_table:add_row{ tbool = '0',     ttext = 'isfalse' };
        test_table:add_row{ tbool = 'no',    ttext = 'isfalse' };
        test_table:add_row{ tbool = 'false', ttext = 'isfalse' };

        test_table:add_row{ tbool = '',    ttext = 'isnull' };
        test_table:add_row{ tbool = '2',   ttext = 'isnull' };
        test_table:add_row{ tbool = 'YES', ttext = 'isnull' };
        return
    end
    if test_type == 'string-direction' then
        test_table:add_row{ tdirn = '1',   tint2 = 1 };
        test_table:add_row{ tdirn = 'yes', tint2 = 1 };

        test_table:add_row{ tdirn = '0',   tint2 = 0 };
        test_table:add_row{ tdirn = 'no',  tint2 = 0 };

        test_table:add_row{ tdirn = '-1',  tint2 = -1 };

        test_table:add_row{ tdirn = '2',   tint2 = nil };
        test_table:add_row{ tdirn = '-2',  tint2 = nil };
        test_table:add_row{ tdirn = '',    tint2 = nil };
        test_table:add_row{ tdirn = 'FOO', tint2 = nil };
        return
    end
    if test_type == 'string-with-number' then
        local numbers = { -2^31 - 1, -2^31, -2^31 + 1,
                          -2^15 - 1, -2^15, -2^15 + 1,
                          -2, -1, 0, 1, 2,
                          2^15 - 1, 2^15, 2^15 + 1,
                          2^31 - 1, 2^31, 2^31 + 1 }
        for _, n in ipairs(numbers) do
            test_table:add_row{
                ttext = string.format('%d', n),
                tint2 = string.format('%d', n),
                tint4 = string.format('%d', n),
                tint8 = string.format('%d', n),
                treal = string.format('%d', n),
                tsqlt = string.format('%d', n),
            }
        end
        test_table:add_row{
            ttext = ' 42',
            tint2 = ' 42',
            tint4 = ' 42',
            tint8 = ' 42',
            treal = ' 42',
            tsqlt = ' 42',
        }
        return
    end
    if test_type == 'string-with-invalid-number' then
        local not_numbers = { '', 'abc', '0a', '0xa', '--1', '1foo', '1.2' }
        for _, n in ipairs(not_numbers) do
            test_table:add_row{
                ttext = n,
                tint2 = n,
                tint4 = n,
                tint8 = n,
            }
        end
        return
    end
    if test_type == 'function-fail' then
        test_table:add_row{ [object.tags.column] = table.insert }
        return
    end
    if test_type == 'table' then
        test_table:add_row{ thstr = {} }
        test_table:add_row{ thstr = { a = 'b', c = 'd' } }
        return
    end
    if test_type == 'table-hstore-fail' then
        test_table:add_row{ thstr = { num = 1, bln = true } }
        return
    end
    if test_type == 'table-fail' then
        test_table:add_row{ [object.tags.column] = {} }
        return
    end
end

