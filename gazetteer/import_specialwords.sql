select getorcreate_country(get_standard_name(get_name_by_language(country_name.name,ARRAY['name'])), country_code) from country_name where get_name_by_language(country_name.name, ARRAY['name']) is not null;
select getorcreate_country(get_standard_name(get_name_by_language(country_name.name,ARRAY['name:en'])), country_code) from country_name where get_name_by_language(country_name.name, ARRAY['name:en']) is not null;
select getorcreate_country(get_standard_name(get_name_by_language(country_name.name,ARRAY['name:fr'])), country_code) from country_name where get_name_by_language(country_name.name, ARRAY['name:fr']) is not null;
select getorcreate_country(get_standard_name(get_name_by_language(country_name.name,ARRAY['name:de'])), country_code) from country_name where get_name_by_language(country_name.name, ARRAY['name:ed']) is not null;
select getorcreate_country(get_standard_name(get_name_by_language(country_name.name,ARRAY['name:es'])), country_code) from country_name where get_name_by_language(country_name.name, ARRAY['name:es']) is not null;
select getorcreate_country(get_standard_name(get_name_by_language(country_name.name,ARRAY['name:cy'])), country_code) from country_name where get_name_by_language(country_name.name, ARRAY['name:cy']) is not null;

select getorcreate_amenity('pub', 'amenity','pub');
select getorcreate_amenity('pubs', 'amenity','pub');

insert into word values (nextval('seq_word'),' restaurant','restaurant','amenity','restaurant',0,null);
insert into word values (nextval('seq_word'),' restaurants','restaurant','amenity','restaurant',0,null);
insert into word values (nextval('seq_word'),' cafe','cafe','amenity','cafe',0,null);
insert into word values (nextval('seq_word'),' cafes','cafe','amenity','cafe',0,null);
insert into word values (nextval('seq_word'),' bank','bank','amenity','bank',0,null);
insert into word values (nextval('seq_word'),' banks','bank','amenity','bank',0,null);
insert into word values (nextval('seq_word'),' school','school','amenity','school',0,null);
insert into word values (nextval('seq_word'),' schools','school','amenity','school',0,null);
insert into word values (nextval('seq_word'),' church','church','amenity','place_of_worship',0,null);
insert into word values (nextval('seq_word'),' place of worship','church','amenity','place_of_worship',0,null);
insert into word values (nextval('seq_word'),' park','park','leisure','park',0,null);
insert into word values (nextval('seq_word'),' wood','wood','natural','wood',0,null);
insert into word values (nextval('seq_word'),' post box','post box','amenity','post_box',0,null);
insert into word values (nextval('seq_word'),' post boxess','post box','amenity','post_box',0,null);
insert into word values (nextval('seq_word'),' postbox','post box','amenity','post_box',0,null);
insert into word values (nextval('seq_word'),' post office','post office','amenity','post_office',0,null);
insert into word values (nextval('seq_word'),' postoffice','post office','amenity','post_office',0,null);
insert into word values (nextval('seq_word'),' cinema','cinema','amenity','cinema',0,null);
insert into word values (nextval('seq_word'),' cinemas','cinema','amenity','cinema',0,null);
insert into word values (nextval('seq_word'),' kino','cinema','amenity','cinema',0,null);
insert into word values (nextval('seq_word'),' train station','train station','railway','station',0,null);
insert into word values (nextval('seq_word'),' railway station','train station','railway','station',0,null);
insert into word values (nextval('seq_word'),' rail station','train station','railway','station',0,null);
insert into word values (nextval('seq_word'),' station','train station','railway','station',0,null);
insert into word values (nextval('seq_word'),' hospital','hospital','amenity','hospital',0,null);
insert into word values (nextval('seq_word'),' hospital','hospital','amenity','hospital',0,null);
insert into word values (nextval('seq_word'),' zoo','zoo','tourism','zoo',0,null);

--insert into word select nextval('seq_word'),' '||make_standard_name(country_code),country_name_en,null,null,null,null,country_code from (select distinct country_code,country_name_en from country) as x;
--insert into word select nextval('seq_word'),' '||make_standard_name(country_name_en),country_name_en,null,null,null,null,country_code from (select distinct country_code,country_name_en from country) as x;
--insert into word values (nextval('seq_word'),' de','Germany',null,null,0,null,'gm');
--insert into word values (nextval('seq_word'),' uk','Germany',null,null,0,null,'gb');

