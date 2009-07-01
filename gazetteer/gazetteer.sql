DROP TRIGGER IF EXISTS place_before_insert on placex;
DROP TRIGGER IF EXISTS place_before_update on placex;

DROP SEQUENCE seq_location;
CREATE SEQUENCE seq_location start 1;

--drop table IF EXISTS query_log;
CREATE TABLE query_log (
  starttime timestamp,
  query text
  );
GRANT INSERT ON query_log TO "www-data" ;

CREATE TYPE wordscore AS (
  word text,
  score float
);

drop table IF EXISTS word;
CREATE TABLE word (
  word_id INTEGER,
  word_token text,
  word text,
  class text,
  type text,
  search_name_count INTEGER
  );
SELECT AddGeometryColumn('word', 'location', 4326, 'GEOMETRY', 2);
CREATE INDEX idx_word_word_id on word USING BTREE (word_id);
CREATE INDEX idx_word_word_token on word USING BTREE (word_token);
GRANT SELECT ON word TO "www-data" ;
DROP SEQUENCE seq_word;
CREATE SEQUENCE seq_word start 1;

drop table IF EXISTS location_area;
CREATE TABLE location_area (
  location_id bigint,
  place_id bigint,
  country_code varchar(2),
  name keyvalue[],
  keywords INTEGER[],
  rank_search INTEGER NOT NULL,
  rank_address INTEGER NOT NULL
  );
SELECT AddGeometryColumn('location_area', 'centroid', 4326, 'POINT', 2);
SELECT AddGeometryColumn('location_area', 'area', 4326, 'POLYGON', 2);
CREATE INDEX idx_location_area_centroid ON location_area USING GIST (centroid);
CREATE INDEX idx_location_area_area ON location_area USING GIST (area);
CREATE INDEX idx_location_area_place on location_area USING BTREE (place_id);

drop table IF EXISTS location_point;
CREATE TABLE location_point (
  location_id bigint,
  place_id bigint,
  country_code varchar(2),
  name keyvalue[],
  keywords INTEGER[],
  rank_search INTEGER NOT NULL,
  rank_address INTEGER NOT NULL
  );
SELECT AddGeometryColumn('location_point', 'centroid', 4326, 'POINT', 2);
CREATE INDEX idx_location_point_centroid ON location_point USING GIST (centroid);
CREATE INDEX idx_location_point_location_id on location_point USING BTREE (location_id);
CREATE INDEX idx_location_point_place_id on location_point USING BTREE (place_id);

drop table IF EXISTS search_name;
CREATE TABLE search_name (
  place_id bigint,
  search_rank integer,
  country_code varchar(2),
  name_vector integer[],
  nameaddress_vector integer[]
  );
CREATE INDEX search_name_name_vector_idx ON search_name USING GIN (name_vector gin__int_ops);
CREATE INDEX searchnameplacesearch_search_nameaddress_vector_idx ON search_name USING GIN (nameaddress_vector gin__int_ops);
SELECT AddGeometryColumn('search_name', 'centroid', 4326, 'POINT', 2);
CREATE INDEX idx_search_name_centroid ON search_name USING GIST (centroid);

drop table IF EXISTS search_name_addressline;
drop table IF EXISTS place_addressline;
CREATE TABLE place_addressline (
  place_id bigint,
  location_id bigint,
  fromarea boolean,
  distance float
--  addressline_admin_level integer,
--  addressline_language text,
--  addressline_text text
  );
CREATE INDEX idx_place_addressline_place_id on place_addressline USING BTREE (place_id);

CREATE OR REPLACE FUNCTION create_tables_location_point() RETURNS BOOLEAN
  AS $$
DECLARE
  i INTEGER;
  postfix TEXT;
BEGIN

  FOR i in 0..26 LOOP

    postfix := '_'||i;

    EXECUTE 'drop table IF EXISTS location_point'||postfix;
    EXECUTE 'CREATE TABLE location_point'||postfix||' ('||
      'location_id bigint,'||
      'place_id bigint,'||
      'country_code varchar(2),'||
      'name keyvalue[],'||
      'keywords TEXT[],'||
      'rank_search INTEGER NOT NULL,'||
      'rank_address INTEGER NOT NULL'||
      ')';
   EXECUTE 'SELECT AddGeometryColumn(''location_point'||postfix||''', ''centroid'', 4326, ''POINT'', 2)';
   EXECUTE 'CREATE INDEX idx_location_point'||postfix||'_centroid ON location_point'||postfix||' USING GIST (centroid)';
  END LOOP;

  RETURN true;
END;
$$
LANGUAGE plpgsql;
select create_tables_location_point();

CREATE OR REPLACE FUNCTION transliteration(text) RETURNS text
  AS '/home/twain/osm2pgsql/gazetteer/gazetteer.so', 'transliteration'
LANGUAGE c IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION make_standard_name(name TEXT) RETURNS TEXT
  AS $$
DECLARE
  out varchar;
BEGIN
out := transliteration(name);
out := ' '||regexp_replace(out,' +',' ','g')||' ';

-- postgresql regex seems very inefficient so avoid
out := replace(out, ' rd ',' road ');
out := replace(out, ' street ',' st ');
out := replace(out, ' saint ',' st ');
out := replace(out, ' dr ',' drive ');
out := replace(out, ' drv ',' drive ');
out := replace(out, ' av ',' avenue ');
out := replace(out, ' ave ',' avenue ');
out := replace(out, ' ln ',' lane ');
out := replace(out, ' ct ',' court ');
out := replace(out, ' pl ',' place ');

out := replace(out, ' bwy ','broadway');
out := replace(out, ' blvd ','boulevard');
out := replace(out, ' bvd ','boulevard');
out := replace(out, ' bld ','boulevard');
out := replace(out, ' bd ','boulevard');

out := replace(out, ' cl ','close');
out := replace(out, ' cres ','crescent');
out := replace(out, ' est ','estate');
out := replace(out, ' gdn ','garden');
out := replace(out, ' gdns ','gardens');
out := replace(out, ' gro ','grove');
out := replace(out, ' hwy ','highway');
out := replace(out, ' sq ','square');

out := replace(out, ' n ','north');
out := replace(out, ' s ','south');
out := replace(out, ' e ','east');
out := replace(out, ' w ','west');

out := replace(out, ' ne ','north east');
out := replace(out, ' nw ','north west');
out := replace(out, ' se ','south east');
out := replace(out, ' sw ','south west');

out := replace(out, ' north-east ','north east');
out := replace(out, ' north-west ','north west');
out := replace(out, ' south-east ','south east');
out := replace(out, ' south-west ','south west');
out := replace(out, ' northeast ','north east');
out := replace(out, ' northwest ','north west');
out := replace(out, ' southeast ','south east');
out := replace(out, ' southwest ','south west');

out := replace(out, ' the ',' ');
out := replace(out, ' and ',' ');

out := replace(out, ' und ',' ');
out := replace(out, ' der ',' ');
out := replace(out, ' die ',' ');
out := replace(out, ' das ',' ');

return trim(out);
END;
$$
LANGUAGE 'plpgsql' IMMUTABLE;

CREATE OR REPLACE FUNCTION getorcreate_word_id(lookup_word TEXT) 
  RETURNS INTEGER
  AS $$
DECLARE
  lookup_token TEXT;
  return_word_id INTEGER;
BEGIN
  lookup_token := trim(lookup_word);
  SELECT word_id FROM word WHERE word_token = lookup_token into return_word_id;
  IF return_word_id IS NULL THEN
    return_word_id := nextval('seq_word');
    INSERT INTO word VALUES (return_word_id, lookup_token, lookup_word, null, null, 0, null);
  END IF;
  RETURN return_word_id;
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION getorcreate_name_id(lookup_word TEXT) 
  RETURNS INTEGER
  AS $$
DECLARE
  lookup_token TEXT;
  return_word_id INTEGER;
BEGIN
  lookup_token := ' '||trim(lookup_word);
  SELECT word_id FROM word WHERE word_token = lookup_token into return_word_id;
  IF return_word_id IS NULL THEN
    return_word_id := nextval('seq_word');
    INSERT INTO word VALUES (return_word_id, lookup_token, lookup_word, null, null, 0, null);
  END IF;
  RETURN return_word_id;
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION get_word_id(lookup_word TEXT) 
  RETURNS INTEGER
  AS $$
DECLARE
  lookup_token TEXT;
  return_word_id INTEGER;
BEGIN
  lookup_token := trim(lookup_word);
  SELECT word_id FROM word WHERE word_token = lookup_token into return_word_id;
  RETURN return_word_id;
END;
$$
LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION get_name_id(lookup_word TEXT) 
  RETURNS INTEGER
  AS $$
DECLARE
  lookup_token TEXT;
  return_word_id INTEGER;
BEGIN
  lookup_token := ' '||trim(lookup_word);
  SELECT word_id FROM word WHERE word_token = lookup_token into return_word_id;
  RETURN return_word_id;
END;
$$
LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION array_merge(a INTEGER[], b INTEGER[])
  RETURNS INTEGER[]
  AS $$
DECLARE
  i INTEGER;
  r INTEGER[];
BEGIN
  r := a;
  FOR i IN 1..array_upper(b, 1) LOOP  
    IF NOT (ARRAY[b[i]] && r) THEN
      r := r || b[i];
    END IF;
  END LOOP;
  RETURN r;
END;
$$
LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION make_keywords(src keyvalue[]) RETURNS INTEGER[]
  AS $$
DECLARE
  result INTEGER[];
  s TEXT;
  w INTEGER;
  words TEXT[];
  i INTEGER;
  j INTEGER;
BEGIN
  result := '{}'::INTEGER[];

  IF NOT array_upper(src, 1) IS NULL THEN

    FOR i IN 1..array_upper(src, 1) LOOP

      s := make_standard_name(src[i].value);

      w := getorcreate_name_id(s);
      IF NOT (ARRAY[w] && result) THEN
        result := result || w;
      END IF;

      words := string_to_array(s, ' ');
      IF array_upper(words, 1) IS NOT NULL THEN
        FOR j IN 1..array_upper(words, 1) LOOP
          IF (words[j] != '') THEN
            w = getorcreate_word_id(words[j]);
            IF NOT (ARRAY[w] && result) THEN
              result := result || w;
            END IF;
          END IF;
        END LOOP;
      END IF;

    END LOOP;
  END IF;
  RETURN result;
END;
$$
LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION make_keywords(src TEXT) RETURNS INTEGER[]
  AS $$
DECLARE
  result INTEGER[];
  s TEXT;
  w INTEGER;
  words TEXT[];
  i INTEGER;
  j INTEGER;
BEGIN
  result := '{}'::INTEGER[];

  s := make_standard_name(src);
  w := getorcreate_name_id(s);

  IF NOT (ARRAY[w] && result) THEN
    result := result || w;
  END IF;

  words := string_to_array(s, ' ');
  IF array_upper(words, 1) IS NOT NULL THEN
    FOR j IN 1..array_upper(words, 1) LOOP
      IF (words[j] != '') THEN
        w = getorcreate_word_id(words[j]);
        IF NOT (ARRAY[w] && result) THEN
          result := result || w;
        END IF;
      END IF;
    END LOOP;
  END IF;

  RETURN result;
END;
$$
LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION get_word_score(wordscores wordscore[], words text[]) RETURNS integer
  AS $$
DECLARE
  idxword integer;
  idxscores integer;
  result integer;
BEGIN
  IF (wordscores is null OR words is null) THEN
    RETURN 0;
  END IF;

  result := 0;
  FOR idxword in 1 .. array_upper(words, 1) LOOP
    FOR idxscores in 1 .. array_upper(wordscores, 1) LOOP
      IF wordscores[idxscores].word = words[idxword] THEN
        result := result + wordscores[idxscores].score;
      END IF;
    END LOOP;
  END LOOP;

  RETURN result;
END;
$$
LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION get_country_code(place geometry) RETURNS TEXT
  AS $$
DECLARE
  nearcountry RECORD;
BEGIN
  -- Are we in a country? Ignore the error condition of being in multiple countries
  FOR nearcountry IN select distinct country_code from country where ST_Contains(country.geometry, ST_Centroid(place)) limit 1
  LOOP
    RETURN nearcountry.country_code;
  END LOOP;
  -- Not in a country - try nearest withing 12 miles of a country
  FOR nearcountry IN select country_code from country where st_distance(country.geometry, ST_Centroid(place)) < 0.5 order by st_distance(country.geometry, place) asc limit 1
  LOOP
    RETURN nearcountry.country_code;
  END LOOP;
  RETURN NULL;
END;
$$
LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION add_location(
    place_id BIGINT,
    place_country_code varchar(2),
    name keyvalue[],
    rank_search INTEGER,
    rank_address INTEGER,
    geometry GEOMETRY
  ) 
  RETURNS BOOLEAN
  AS $$
DECLARE
  keywords INTEGER[];
  country_code VARCHAR(2);
  locationid INTEGER;
BEGIN
  -- 26 = street/highway
  IF rank_search < 26 THEN
    keywords := make_keywords(name);
    IF place_country_code IS NULL THEN
      country_code := get_country_code(geometry);
    ELSE
      country_code := place_country_code;
    END IF;

    locationid := nextval('seq_location');
    IF (ST_GeometryType(geometry) = 'ST_Polygon' AND ST_IsValid(geometry)) THEN
      INSERT INTO location_area values (locationid,place_id,country_code,name,keywords,
        rank_search,rank_address,ST_Centroid(geometry),geometry);
    END IF;
      INSERT INTO location_point values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 26 THEN
        INSERT INTO location_point_26 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 25 THEN
        INSERT INTO location_point_25 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 24 THEN
        INSERT INTO location_point_24 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 23 THEN
        INSERT INTO location_point_23 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 22 THEN
        INSERT INTO location_point_22 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 21 THEN
        INSERT INTO location_point_21 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 20 THEN
        INSERT INTO location_point_20 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 19 THEN
        INSERT INTO location_point_19 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 18 THEN
        INSERT INTO location_point_18 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 17 THEN
        INSERT INTO location_point_17 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 16 THEN
        INSERT INTO location_point_16 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 15 THEN
        INSERT INTO location_point_15 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 14 THEN
        INSERT INTO location_point_14 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 13 THEN
        INSERT INTO location_point_13 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 12 THEN
        INSERT INTO location_point_12 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 11 THEN
        INSERT INTO location_point_11 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 10 THEN
        INSERT INTO location_point_10 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 9 THEN
        INSERT INTO location_point_9 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 8 THEN
        INSERT INTO location_point_8 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 7 THEN
        INSERT INTO location_point_7 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 6 THEN
        INSERT INTO location_point_6 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 5 THEN
        INSERT INTO location_point_5 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 4 THEN
        INSERT INTO location_point_4 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 3 THEN
        INSERT INTO location_point_3 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 2 THEN
        INSERT INTO location_point_2 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 1 THEN
        INSERT INTO location_point_1 values (locationid,place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;
      END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;
      END IF;END IF;END IF;END IF;END IF;END IF;
    RETURN true;
  END IF;
  RETURN false;
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION place_insert() RETURNS TRIGGER
  AS $$
DECLARE
  i INTEGER;
  postcode TEXT;
  result BOOLEAN;
  country_code VARCHAR(2);
BEGIN
--  RAISE WARNING '%',NEW;

  IF ST_X(ST_Centroid(NEW.geometry))::text in ('NaN','Infinity','-Infinity') OR ST_Y(ST_Centroid(NEW.geometry))::text in ('NaN','Infinity','-Infinity') THEN
    RAISE WARNING 'Invalid geometary, rejecting: % %', NEW.osm_type, NEW.osm_id;
    RETURN NULL;
  END IF;

  NEW.place_id := nextval('seq_place');
  NEW.indexed := false;

  IF NEW.osm_type = 'X' THEN
    -- 'E'xternal records should already be in the right format so do nothing
  ELSE
    NEW.rank_search := 100;
    NEW.rank_address := NEW.rank_search;

    -- By doing this here we have the country available to us - currently only used for postcode
    IF NEW.class = 'place' THEN

      IF NEW.country_code IS NULL THEN
        NEW.country_code := get_country_code(NEW.geometry);
      END IF;

      IF NEW.type in ('state') THEN
        NEW.rank_search := 8;
        NEW.rank_address := NEW.rank_search;
      ELSEIF NEW.type in ('county') THEN
        NEW.rank_search := 12;
        NEW.rank_address := NEW.rank_search;
      ELSEIF NEW.type in ('island') THEN
        NEW.rank_search := 17;
        NEW.rank_address := 0;
      ELSEIF NEW.type in ('city') THEN
        NEW.rank_search := 16;
        NEW.rank_address := NEW.rank_search;
      ELSEIF NEW.type in ('town','village','hamlet') THEN
        NEW.rank_search := 17;
        NEW.rank_address := NEW.rank_search;
      ELSEIF NEW.type in ('moor') THEN
        NEW.rank_search := 17;
        NEW.rank_address := 0;
      ELSEIF NEW.type in ('suburb','croft') THEN
        NEW.rank_search := 20;
        NEW.rank_address := NEW.rank_search;
      ELSEIF NEW.type in ('farm','locality','islet') THEN
        NEW.rank_search := 20;
        NEW.rank_address := 0;
      ELSEIF NEW.type in ('postcode') THEN

        -- Postcode processing is very country dependant
        IF NEW.country_code = 'GB' THEN
          IF NEW.postcode ~ '^([A-Z][A-Z]?[0-9][0-9A-Z]? [0-9][A-Z][A-Z])$' THEN
            NEW.rank_search := 25;
            NEW.rank_address := 5;
            NEW.name := ARRAY[ROW('ref',NEW.postcode)::keyvalue];
          ELSEIF NEW.postcode ~ '^([A-Z][A-Z]?[0-9][0-9A-Z]? [0-9])$' THEN
            NEW.rank_search := 23;
            NEW.rank_address := 5;
            NEW.name := ARRAY[ROW('ref',NEW.postcode)::keyvalue];
          ELSEIF NEW.postcode ~ '^([A-Z][A-Z]?[0-9][0-9A-Z])$' THEN
            NEW.rank_search := 21;
            NEW.rank_address := 5;
            NEW.name := ARRAY[ROW('ref',NEW.postcode)::keyvalue];
          END IF;

        ELSEIF NEW.country_code = 'DE' THEN
          IF NEW.postcode ~ '^([0-9]{5})$' THEN
            NEW.name := ARRAY[ROW('ref',NEW.postcode)::keyvalue];
            NEW.rank_search := 21;
            NEW.rank_address := 11;
          END IF;

        ELSE
          -- Guess at the postcode format and coverage (!)
          IF upper(NEW.postcode) ~ '^[A-Z0-9]{1,5}$' THEN -- Probably too short to be very local
            NEW.name := ARRAY[ROW('ref',NEW.postcode)::keyvalue];
            NEW.rank_search := 21;
            NEW.rank_address := 11;
          ELSE
            -- Does it look splitable into and area and local code?
            postcode := substring(upper(NEW.postcode) from '^([- :A-Z0-9]+)([- :][A-Z0-9]+)$');

            IF postcode IS NOT NULL THEN
              result := add_location(NEW.place_id,NEW.country_code,ARRAY[ROW('ref',postcode)::keyvalue],21,11,NEW.geometry);
              NEW.name := ARRAY[ROW('ref',NEW.postcode)::keyvalue];
              NEW.rank_search := 25;
              NEW.rank_address := 11;
            ELSEIF NEW.postcode ~ '^[- :A-Z0-9]{6,}$' THEN
              NEW.name := ARRAY[ROW('ref',NEW.postcode)::keyvalue];
              NEW.rank_search := 21;
              NEW.rank_address := 11;
            END IF;
          END IF;
        END IF;

      ELSEIF NEW.type in ('airport','street') THEN
        NEW.rank_search := 26;
        NEW.rank_address := NEW.rank_search;
      ELSEIF NEW.type in ('house','houses','building') THEN
        NEW.rank_search := 28;
        NEW.rank_address := NEW.rank_search;
      END IF;

    ELSEIF NEW.class = 'boundary' THEN
      NEW.country_code := get_country_code(NEW.geometry);
      NEW.rank_search := NEW.admin_level * 2;
      NEW.rank_address := NEW.rank_search;
    ELSEIF NEW.class = 'highway' AND NEW.osm_type != 'N' THEN
      NEW.rank_search := 26;
      NEW.rank_address := NEW.rank_search;
    END IF;

  END IF;

  IF array_upper(NEW.name, 1) is not null THEN
    result := add_location(NEW.place_id,NEW.country_code,NEW.name,NEW.rank_search,NEW.rank_address,NEW.geometry);
  END IF;

  RETURN NEW;

END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION place_update() RETURNS TRIGGER
  AS $$
DECLARE

  place_centroid GEOMETRY;
  place_geometry_text TEXT;

  search_maxdistance FLOAT[];
  search_mindistance FLOAT[];
--  search_scores wordscore[];
--  search_scores_pos INTEGER;
  search_country_code_conflict BOOLEAN;

  i INTEGER;
--  j INTEGER;
--  numLocations INTEGER;
  iMax FLOAT;
--  notfound BOOLEAN;
  location RECORD;
  search_diameter FLOAT;
  search_prevdiameter FLOAT;
  search_maxrank INTEGER;

  name_vector INTEGER[];
  nameaddress_vector INTEGER[];

--  start TIMESTAMP;

BEGIN

  IF NEW.indexed and NOT OLD.indexed THEN

    search_country_code_conflict := false;

    IF array_upper(NEW.name, 1) IS NULL THEN
      
      -- For un-named features there is no point in indexing
      -- instead just find the nearest highway (road) and link to that

      IF NEW.rank_search > 26 THEN

      END IF;

    ELSE

      DELETE FROM place_addressline WHERE place_id = NEW.place_id;

      -- Speed up searches - just use the centroid of the feature
      -- cheaper but less acurate
      place_centroid := ST_Centroid(NEW.geometry);
      place_geometry_text := 'ST_GeomFromText('''||ST_AsText(NEW.geometry)||''','||ST_SRID(NEW.geometry)||')';

      -- Default maximum distances to look for a place (huge!)
--      search_scores_pos := 0;
      FOR i IN 1..28 LOOP
        search_maxdistance[i] := 1;
        search_mindistance[i] := 0.0;
      END LOOP;
      -- Minimum size to search, can be larger but don't let it shink below this
      search_mindistance[14] := 0.2;
      search_mindistance[15] := 0.1;
      search_mindistance[16] := 0.05;
      search_mindistance[17] := 0.03;
      search_mindistance[18] := 0.015;
      search_mindistance[19] := 0.008;
      search_mindistance[20] := 0.006;
      search_mindistance[21] := 0.004;
      search_mindistance[22] := 0.003;
      search_mindistance[23] := 0.002;
      search_mindistance[24] := 0.002;
      search_mindistance[25] := 0.001;
      search_mindistance[26] := 0.001;

      search_maxdistance[14] := 1;
      search_maxdistance[15] := 0.5;
      search_maxdistance[16] := 0.15;
      search_maxdistance[17] := 0.05;
      search_maxdistance[18] := 0.02;
      search_maxdistance[19] := 0.02;
      search_maxdistance[20] := 0.02;
      search_maxdistance[21] := 0.02;
      search_maxdistance[22] := 0.02;
      search_maxdistance[23] := 0.02;
      search_maxdistance[24] := 0.02;
      search_maxdistance[25] := 0.02;
      search_maxdistance[26] := 0.02;

      -- Add in the local name
      name_vector := make_keywords(NEW.name);
      nameaddress_vector := name_vector;

--RAISE WARNING 'FINDING SEARCH STRING FOR: % % %', NEW.place_id, NEW.name, name_vector;

  -- Try for an area match
  FOR location IN SELECT
    location_id,
    name,
    keywords,
    country_code,
    rank_address,
    rank_search,
    ST_Distance(place_centroid, centroid) as distance
    FROM location_area
    WHERE ST_Contains(area, place_centroid) and location_area.rank_search < NEW.rank_search
    ORDER BY ST_Distance(place_centroid, centroid) ASC
  LOOP

--RAISE WARNING '  AREA: %',location.keywords;

    IF NEW.country_code IS NULL THEN
      NEW.country_code := location.country_code;
    ELSEIF NEW.country_code != location.country_code THEN
      search_country_code_conflict := true;
    END IF;

    -- Add it to the list of search terms
    nameaddress_vector := array_merge(nameaddress_vector, location.keywords::integer[]);

    INSERT INTO place_addressline VALUES (NEW.place_id, location.location_id, true, location.distance); 

  END LOOP;

--RAISE WARNING '% AREA LOCATIONS, %', numLocations, NEW.rank_search;

  search_diameter := 0.0;
  search_maxrank := NEW.rank_search;
  -- 16 = city, anything larger tends to be an area so don't continue
  WHILE search_diameter < 1 AND search_maxrank > 16 LOOP

    search_prevdiameter := search_diameter;
    IF search_diameter = 0 THEN
      search_diameter := 0.001;
    ELSE
      search_diameter := search_diameter * 2;
    END IF;

--numLocations := 0;

  -- Try nearest
  FOR location IN EXECUTE 'SELECT location_id, name, keywords, country_code, rank_address, rank_search,'||
    'ST_Distance('||place_geometry_text||', centroid) as distance'||
    ' FROM location_point_'||(case when search_maxrank > 26 THEN 26 ELSE search_maxrank end)||
    ' WHERE ST_DWithin('||place_geometry_text||', centroid, '||search_diameter||') '||
    '  and ST_Distance('||place_geometry_text||', centroid) > '||search_prevdiameter||
    ' ORDER BY ST_Distance('||place_geometry_text||', centroid) ASC'
  LOOP

--numLocations := numLocations + 1;

    IF NEW.country_code IS NULL THEN
      NEW.country_code := location.country_code;
    ELSEIF NEW.country_code != location.country_code THEN
      search_country_code_conflict := true;
    END IF;

    -- Find search words
    IF (location.distance < search_maxdistance[location.rank_search]) THEN
--RAISE WARNING '  POINT: %', location.keywords;

      -- Add it to the list of search terms, de-duplicate
--RAISE WARNING '% %', nameaddress_vector, location.keywords;
      nameaddress_vector := array_merge(nameaddress_vector, location.keywords::integer[]);

      iMax := (location.distance*1.5)::float;
      FOR i IN location.rank_search..28 LOOP
        IF iMax < search_maxdistance[i] THEN
          IF iMax > search_mindistance[i] THEN
            search_maxdistance[i] := iMax;
          ELSE
            search_maxdistance[i] := search_mindistance[i];
          END IF;
        END IF;
      END LOOP;

      INSERT INTO place_addressline VALUES (NEW.place_id, location.location_id, false, location.distance); 

    ELSE
--RAISE WARNING '  Stopped: % % % %', location.rank_search, location.distance, search_maxdistance[location.rank_search], location.name;
      IF search_maxrank > location.rank_search THEN
        search_maxrank := location.rank_search;
      END IF;
    END IF;

  END LOOP;

--RAISE WARNING '  % POINT LOCATIONS, % %', numLocations, search_maxrank, search_diameter;

  END LOOP; --WHILE

      IF search_country_code_conflict OR NEW.country_code IS NULL THEN
        NEW.country_code := get_country(place_centroid);
      END IF;

      INSERT INTO search_name values (NEW.place_id,NEW.rank_search, NEW.country_code, 
        name_vector, nameaddress_vector, place_centroid);

    END IF;

  END IF;

  return NEW;
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION get_address_by_language(address_place_id BIGINT, languagepref TEXT[]) RETURNS TEXT
  AS $$
DECLARE
  result TEXT;
  resultseperator TEXT;
  search TEXT[];
  found INTEGER;
  location RECORD;
  search_place_id BIGINT;
  address_name TEXT;
  address_street_place_id BIGINT;
  address_country_code VARCHAR(2);
  address_geometry GEOMETRY;
  address_class TEXT;
  address_type TEXT;
BEGIN

  found := 1000;
  search := languagepref;
  resultseperator := '';
  result := '';

  search_place_id := address_place_id;

  SELECT name::text,class,type,street_place_id,country_code,geometry 
    FROM placex 
    WHERE place_id = search_place_id
    INTO address_name,address_class,address_type,address_street_place_id,address_country_code,address_geometry;

  IF address_name = '{}' THEN

    result := resultseperator || address_type;
    resultseperator := ', ';

    IF address_street_place_id IS NULL THEN
      -- 0.001 ~ 100 meters
      -- 26 = road/highway
      FOR location IN SELECT place_id FROM placex
        WHERE ST_DWithin(address_geometry, geometry, 0.001) and rank_search = 26
        ORDER BY ST_distance(address_geometry, geometry) ASC limit 1
      LOOP
        UPDATE placex set street_place_id = location.place_id WHERE place_id = address_place_id;
        address_street_place_id := location.place_id;
      END LOOP;
    END IF;
    IF address_street_place_id IS NULL THEN
      RETURN result;
    END IF;

    search_place_id := address_street_place_id;

    SELECT name::text,class,type,street_place_id,country_code,geometry 
      FROM placex 
      WHERE place_id = search_place_id
      INTO address_name,address_class,address_type,address_street_place_id,address_country_code,address_geometry;

  END IF;

  FOR location IN select rank_address,name,distance,length(name::text) as namelength 
    from place_addressline join location_point using (location_id) 
    where place_addressline.place_id = search_place_id and country_code = address_country_code and rank_address > 0
    UNION ALL select rank_address,name,0 as distance,length(name::text) as namelength 
    from placex where placex.place_id = search_place_id order by rank_address desc,distance asc,namelength desc
  LOOP
    IF array_upper(search, 1) IS NOT NULL AND array_upper(location.name, 1) IS NOT NULL THEN
      FOR j IN 1..array_upper(search, 1) LOOP
        FOR k IN 1..array_upper(location.name, 1) LOOP
          IF (found > location.rank_address AND location.name[k].key = search[j] AND location.name[k].value != '') THEN
            result := result || resultseperator || trim(location.name[k].value);
            resultseperator := ', ';
            found := location.rank_address;
          END IF;
        END LOOP;
      END LOOP;
    END IF;
  END LOOP;

  RETURN result;
END;
$$
LANGUAGE plpgsql;

-- placex is just a mockup of place to avoid having to re-import for testing
drop table placex;
CREATE TABLE placex (
  place_id bigint NOT NULL,
  osm_type char(1),
  osm_id bigint,
  class TEXT NOT NULL,
  type TEXT NOT NULL,
  name keyvalue[],
  admin_level integer,
  housenumber TEXT,
  street TEXT,
  postcode TEXT,
  country_code varchar(2),
  street_place_id bigint,
  rank_address INTEGER,
  rank_search INTEGER,
  indexed BOOLEAN
  );
SELECT AddGeometryColumn('placex', 'geometry', 4326, 'GEOMETRY', 2);
CREATE UNIQUE INDEX idx_place_id ON placex USING BTREE (place_id);
CREATE INDEX idx_placex_osmid ON placex USING BTREE (osm_type, osm_id);
CREATE INDEX idx_placex_rank_search ON placex USING BTREE (rank_search);
CREATE INDEX idx_placex_rank_address ON placex USING BTREE (rank_address);
CREATE INDEX idx_placex_geometry ON placex USING GIST (geometry);
CREATE INDEX idx_placex_postcodesector ON placex USING BTREE (substring(upper(postcode) from '^([A-Z][A-Z]?[0-9][0-9A-Z]? [0-9])[A-Z][A-Z]$'));
DROP SEQUENCE seq_place;
CREATE SEQUENCE seq_place start 1;
GRANT SELECT on placex to "www-data" ;
GRANT SELECT ON search_name to "www-data" ;
GRANT UPDATE ON placex to "www-data" ;
GRANT SELECT on place_addressline to "www-data" ;
GRANT SELECT on location_point to "www-data" ;

-- insert creates the location tagbles, optionall creates location indexes if indexed == true
CREATE TRIGGER placex_before_insert BEFORE INSERT ON placex
    FOR EACH ROW EXECUTE PROCEDURE place_insert();

-- update insert creates the location tables
CREATE TRIGGER placex_before_update BEFORE UPDATE ON placex
    FOR EACH ROW EXECUTE PROCEDURE place_update();

select 'now'::timestamp;

insert into placex select null,'E',null,'place','county',ARRAY[ROW('name',county)::keyvalue],100,null,null,null,'US',null,null,null,null,ST_Transform(geometryn(the_geom, generate_series(1, numgeometries(the_geom))), 4326) from usstatecounty;
insert into placex select null,'E',null,'place','state',ARRAY[ROW('ref',state)::keyvalue],100,null,null,null,'US',null,null,null,null,ST_Transform(geometryn(the_geom, generate_series(1, numgeometries(the_geom))), 4326) from usstatecounty;
insert into placex select null,'E',null,'place','state',ARRAY[ROW('name',state)::keyvalue],100,null,null,null,'US',null,null,null,null,ST_Transform(geometryn(the_geom, generate_series(1, numgeometries(the_geom))), 4326) from usstate;
insert into placex select null,'E',nextval,'place','postcode','{}',100,null,null,postcode,countrycode,null,null,null,null,geometry from ukpostcodedata;
insert into placex select null,'E',nextval,'place','postcode','{}',100,null,null,substring(postcode from '^([A-Z][A-Z]?[0-9][0-9A-Z]?) [0-9]$'),countrycode,null,null,null,null,geometry from ukpostcodedata where postcode ~ '^[A-Z][A-Z]?[0-9][0-9A-Z]? [0-9]$' and ST_GeometryType(geometry) = 'ST_Polygon';
insert into placex select * from place;

select 'now'::timestamp;

update placex set indexed = true where indexed = false;

select 'now'::timestamp;


