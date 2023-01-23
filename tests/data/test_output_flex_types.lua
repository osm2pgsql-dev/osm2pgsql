
local test_table = osm2pgsql.define_node_table("nodes", {
    { column = 'ttext', type = 'text' },
    { column = 'tbool', type = 'boolean' },
    { column = 'tint2', type = 'int2' },
    { column = 'tint4', type = 'int4' },
    { column = 'tint8', type = 'int8' },
    { column = 'treal', type = 'real' },
    { column = 'thstr', type = 'hstore' },
    { column = 'tjson', type = 'jsonb' },
    { column = 'tdirn', type = 'direction' },
    { column = 'tsqlt', sql_type = 'varchar' },
})

function osm2pgsql.process_node(object)
    local test_type = object.tags.type

    if test_type == 'nil' then
        test_table:insert({})
        return
    end
    if test_type == 'boolean' then
        local row = { tbool = true, tint2 = true, tint4 = true,
                      tint8 = true, tjson = true, tdirn = true }
        test_table:insert(row)

        row = { tbool = false, tint2 = false, tint4 = false,
                tint8 = false, tjson = false, tdirn = false }
        test_table:insert(row)
        return
    end
    if test_type == 'boolean-fail' then
        test_table:insert{ [object.tags.column] = true }
        return
    end
    if test_type == 'number' then
        local numbers = { -2^31 - 1, -2^31, -2^31 + 1,
                          -2^15 - 1, -2^15, -2^15 + 1,
                          -2, -1, -0.5, 0, 0.5, 1, 2,
                          2^15 - 1, 2^15, 2^15 + 1,
                          2^31 - 1, 2^31, 2^31 + 1 }
        for _, n in ipairs(numbers) do
            test_table:insert{
                ttext = n,
                tbool = n,
                tint2 = n,
                tint4 = n,
                tint8 = n,
                treal = n,
                tjson = n,
                tdirn = n,
                tsqlt = n,
            }
        end
        return
    end
    if test_type == 'number-fail' then
        test_table:insert{ [object.tags.column] = 1 }
        return
    end
    if test_type == 'string-bool' then
        test_table:insert{ tbool = '1',     ttext = 'istrue' };
        test_table:insert{ tbool = 'yes',   ttext = 'istrue' };
        test_table:insert{ tbool = 'true',  ttext = 'istrue' };

        test_table:insert{ tbool = '0',     ttext = 'isfalse' };
        test_table:insert{ tbool = 'no',    ttext = 'isfalse' };
        test_table:insert{ tbool = 'false', ttext = 'isfalse' };

        test_table:insert{ tbool = '',    ttext = 'isnull' };
        test_table:insert{ tbool = '2',   ttext = 'isnull' };
        test_table:insert{ tbool = 'YES', ttext = 'isnull' };
        return
    end
    if test_type == 'string-direction' then
        test_table:insert{ tdirn = '1',   tint2 = 1 };
        test_table:insert{ tdirn = 'yes', tint2 = 1 };

        test_table:insert{ tdirn = '0',   tint2 = 0 };
        test_table:insert{ tdirn = 'no',  tint2 = 0 };

        test_table:insert{ tdirn = '-1',  tint2 = -1 };

        test_table:insert{ tdirn = '2',   tint2 = nil };
        test_table:insert{ tdirn = '-2',  tint2 = nil };
        test_table:insert{ tdirn = '',    tint2 = nil };
        test_table:insert{ tdirn = 'FOO', tint2 = nil };
        return
    end
    if test_type == 'string-with-number' then
        local numbers = { -2^31 - 1, -2^31, -2^31 + 1,
                          -2^15 - 1, -2^15, -2^15 + 1,
                          -2, -1, 0, 1, 2,
                          2^15 - 1, 2^15, 2^15 + 1,
                          2^31 - 1, 2^31, 2^31 + 1 }
        for _, n in ipairs(numbers) do
            test_table:insert{
                ttext = string.format('%d', n),
                tint2 = string.format('%d', n),
                tint4 = string.format('%d', n),
                tint8 = string.format('%d', n),
                treal = string.format('%d', n),
                tsqlt = string.format('%d', n),
            }
        end
        test_table:insert{
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
            test_table:insert{
                ttext = n,
                tint2 = n,
                tint4 = n,
                tint8 = n,
                treal = n,
            }
        end
        return
    end
    if test_type == 'function-fail' then
        test_table:insert{ [object.tags.column] = table.insert }
        return
    end
    if test_type == 'table' then
        local t = { a = 'b', c = 'd' }
        test_table:insert{ thstr = {}, tjson = {} }
        test_table:insert{ thstr = t, tjson = t }
        return
    end
    if test_type == 'table-hstore-fail' then
        test_table:insert{ thstr = { num = 1, bln = true } }
        return
    end
    if test_type == 'table-fail' then
        test_table:insert{ [object.tags.column] = {} }
        return
    end
    if test_type == 'json' then
        test_table:insert{ tjson = {
            astring = '123',
            aninteger = 124,
            anumber = 12.5,
            atrue = true,
            afalse = false,
            anull = nil,
            atable = { a = 'nested', tab = 'le' },
            anarray = { 4, 3, 7 }
        }}
        return
    end
    if test_type == 'json-loop' then
        local atable = { a = 'b' }
        atable.c = atable
        test_table:insert{ tjson = atable }
        return
    end
end

