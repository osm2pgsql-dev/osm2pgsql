create table country_name as select country_code,name from place where class = 'place' and type = 'country' and country_code is not null;
create index idx_country_name_country_code on country_name using btree (country_code);

alter table worldboundaries add column name keyvalue[];

update worldboundaries set name = country_name.name from country_name where upper(worldboundaries.country_code) = upper(country_name.country_code);

update place set country_code = country_name.country_code from country_name where osm_type = 'R' and admin_level < 4 and type = 'administrative' and place.country_code is null and (get_name_by_language(place.name,ARRAY['name']) = get_name_by_language(country_name.name,ARRAY['name']));
