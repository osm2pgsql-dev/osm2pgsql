
#include "format.hpp"
#include "pgsql-helper.hpp"

void create_geom_check_trigger(pg_conn_t *db_connection,
                               std::string const &schema,
                               std::string const &table,
                               std::string const &geom_column)
{
    std::string func_name = qualified_name(schema, table + "_osm2pgsql_valid");

    db_connection->exec(
        "CREATE OR REPLACE FUNCTION {}()\n"
        "RETURNS TRIGGER AS $$\n"
        "BEGIN\n"
        "  IF ST_IsValid(NEW.{}) THEN \n"
        "    RETURN NEW;\n"
        "  END IF;\n"
        "  RETURN NULL;\n"
        "END;"
        "$$ LANGUAGE plpgsql IMMUTABLE;"_format(func_name, geom_column));

    db_connection->exec(
        "CREATE TRIGGER \"{}\""
        " BEFORE INSERT OR UPDATE"
        " ON {}"
        " FOR EACH ROW EXECUTE PROCEDURE"
        " {}();"_format(table + "_osm2pgsql_valid",
                        qualified_name(schema, table), func_name));
}
