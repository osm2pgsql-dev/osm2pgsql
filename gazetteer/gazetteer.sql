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

out := replace(out, ' bwy ',' broadway ');
out := replace(out, ' blvd ',' boulevard ');
out := replace(out, ' bvd ',' boulevard ');
out := replace(out, ' bld ',' boulevard ');
out := replace(out, ' bd ',' boulevard ');

out := replace(out, ' cl ',' close ');
out := replace(out, ' cres ',' crescent ');
out := replace(out, ' est ',' estate ');
out := replace(out, ' gdn ',' garden ');
out := replace(out, ' gdns ',' gardens ');
out := replace(out, ' gro ',' grove ');
out := replace(out, ' hwy ',' highway ');
out := replace(out, ' sq ',' square ');

out := replace(out, ' n ',' north ');
out := replace(out, ' s ',' south ');
out := replace(out, ' e ',' east ');
out := replace(out, ' w ',' west ');

out := replace(out, ' ne ',' north east ');
out := replace(out, ' nw ',' north west ');
out := replace(out, ' se ',' south east ');
out := replace(out, ' sw ',' south west ');

out := replace(out, ' north-east ',' north east ');
out := replace(out, ' north-west ',' north west ');
out := replace(out, ' south-east ',' south east ');
out := replace(out, ' south-west ',' south west ');
out := replace(out, ' northeast ',' north east ');
out := replace(out, ' northwest ',' north west ');
out := replace(out, ' southeast ',' south east ');
out := replace(out, ' southwest ',' south west ');

out := replace(out, ' the ',' ');
out := replace(out, ' and ',' ');

out := replace(out, ' und ',' ');
out := replace(out, ' der ',' ');
out := replace(out, ' die ',' ');
out := replace(out, ' das ',' ');

-- out := regexp_replace(out,E'\\mcir\\M','circle','g');

return trim(out);
END;
$$
LANGUAGE 'plpgsql' IMMUTABLE;

CREATE OR REPLACE FUNCTION make_name_token(name TEXT) RETURNS TEXT
  AS $$
DECLARE
BEGIN
  return 'zzz'||regexp_replace(make_standard_name(name),' ','zzz','g');
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
  SELECT word_id FROM word WHERE word_token = lookup_token and class is null and type is null into return_word_id;
  IF return_word_id IS NULL THEN
    return_word_id := nextval('seq_word');
    INSERT INTO word VALUES (return_word_id, lookup_token, lookup_word, null, null, 0, null);
  END IF;
  RETURN return_word_id;
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION getorcreate_housenumber_id(lookup_word TEXT)
  RETURNS INTEGER
  AS $$
DECLARE
  lookup_token TEXT;
  return_word_id INTEGER;
BEGIN
  lookup_token := ' '||trim(lookup_word);
  SELECT word_id FROM word WHERE word_token = lookup_token and class='place' and type='house' into return_word_id;
  IF return_word_id IS NULL THEN
    return_word_id := nextval('seq_word');
    INSERT INTO word VALUES (return_word_id, lookup_token, lookup_word, 'place', 'house', 0, null);
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
  SELECT word_id FROM word WHERE word_token = lookup_token and class is null and type is null into return_word_id;
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
  SELECT word_id FROM word WHERE word_token = lookup_token and class is null and type is null into return_word_id;
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
  SELECT word_id FROM word WHERE word_token = lookup_token and class is null and type is null into return_word_id;
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

    IF (ST_GeometryType(geometry) in ('ST_Polygon','ST_MultiPolygon') AND ST_IsValid(geometry)) THEN
      INSERT INTO location_area values (place_id,country_code,name,keywords,
        rank_search,rank_address,ST_Centroid(geometry),geometry);
    END IF;
      INSERT INTO location_point values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 26 THEN
        INSERT INTO location_point_26 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 25 THEN
        INSERT INTO location_point_25 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 24 THEN
        INSERT INTO location_point_24 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 23 THEN
        INSERT INTO location_point_23 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 22 THEN
        INSERT INTO location_point_22 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 21 THEN
        INSERT INTO location_point_21 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 20 THEN
        INSERT INTO location_point_20 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 19 THEN
        INSERT INTO location_point_19 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 18 THEN
        INSERT INTO location_point_18 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 17 THEN
        INSERT INTO location_point_17 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 16 THEN
        INSERT INTO location_point_16 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 15 THEN
        INSERT INTO location_point_15 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 14 THEN
        INSERT INTO location_point_14 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 13 THEN
        INSERT INTO location_point_13 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 12 THEN
        INSERT INTO location_point_12 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 11 THEN
        INSERT INTO location_point_11 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 10 THEN
        INSERT INTO location_point_10 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 9 THEN
        INSERT INTO location_point_9 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 8 THEN
        INSERT INTO location_point_8 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 7 THEN
        INSERT INTO location_point_7 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 6 THEN
        INSERT INTO location_point_6 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 5 THEN
        INSERT INTO location_point_5 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 4 THEN
        INSERT INTO location_point_4 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 3 THEN
        INSERT INTO location_point_3 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 2 THEN
        INSERT INTO location_point_2 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      IF rank_search < 1 THEN
        INSERT INTO location_point_1 values (place_id,country_code,name,keywords,rank_search,rank_address,ST_Centroid(geometry));
      END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;
      END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;
      END IF;END IF;END IF;END IF;END IF;END IF;
    RETURN true;
  END IF;
  RETURN false;
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION create_interpolation(wayid BIGINT, interpolationtype TEXT) RETURNS INTEGER
  AS $$
DECLARE
  
  newpoints INTEGER;
  waynodes integer[];
  nodeid INTEGER;
  prevnode RECORD;
  nextnode RECORD;
  startnumber INTEGER;
  endnumber INTEGER;
  stepsize INTEGER;
  orginalstartnumber INTEGER;
  originalnumberrange INTEGER;
  housenum INTEGER;
  linegeo GEOMETRY;

  havefirstpoint BOOLEAN;
  linestr TEXT;
BEGIN
  newpoints := 0;
  IF interpolationtype = 'odd' OR interpolationtype = 'even' OR interpolationtype = 'all' THEN
    select nodes from planet_osm_ways where id = wayid INTO waynodes;
    IF array_upper(waynodes, 1) IS NOT NULL THEN

      havefirstpoint := false;

      FOR nodeidpos in 1..array_upper(waynodes, 1) LOOP

        select * from placex where osm_type = 'N' and osm_id = waynodes[nodeidpos]::bigint limit 1 INTO nextnode;

        IF nextnode.geometry IS NULL THEN
          select ST_SetSRID(ST_Point(lon,lat),4326) from planet_osm_nodes where id = waynodes[nodeidpos] INTO nextnode.geometry;
        END IF;
      
        IF havefirstpoint THEN

          -- add point to the line string
          linestr := linestr||','||ST_X(nextnode.geometry)||' '||ST_Y(nextnode.geometry);
          startnumber := ('0'||substring(prevnode.housenumber,'[0-9]+'))::integer;
          endnumber := ('0'||substring(nextnode.housenumber,'[0-9]+'))::integer;

          IF startnumber IS NOT NULL and startnumber > 0 AND endnumber IS NOT NULL and endnumber > 0 THEN

            IF startnumber != endnumber THEN

              linestr := linestr || ')';
              linegeo := ST_GeomFromText(linestr,4326);
              linestr := 'LINESTRING('||ST_X(nextnode.geometry)||' '||ST_Y(nextnode.geometry);
              IF (startnumber > endnumber) THEN
                housenum := endnumber;
                endnumber := startnumber;
                startnumber := housenum;
                linegeo := ST_Reverse(linegeo);
              END IF;
              orginalstartnumber := startnumber;
              originalnumberrange := endnumber - startnumber;

-- Too much broken data worldwide for this test to be worth using
--              IF originalnumberrange > 500 THEN
--                RAISE WARNING 'Number block of % while processing % %', originalnumberrange, prevnode, nextnode;
--              END IF;

              IF (interpolationtype = 'odd' AND startnumber%2 = 0) OR (interpolationtype = 'even' AND startnumber%2 = 1) THEN
                startnumber := startnumber + 1;
                stepsize := 2;
              ELSE
                IF (interpolationtype = 'odd' OR interpolationtype = 'even') THEN
                  startnumber := startnumber + 2;
                  stepsize := 2;
                ELSE
                  startnumber := startnumber + 1;
                  stepsize := 1;
                END IF;
              END IF;
              endnumber := endnumber - 1;
              FOR housenum IN startnumber..endnumber BY stepsize LOOP
                insert into placex values (null,'N',prevnode.osm_id,prevnode.class,prevnode.type,NULL,prevnode.admin_level,housenum,prevnode.street,prevnode.isin,prevnode.postcode,prevnode.country_code,prevnode.street_place_id,prevnode.rank_address,prevnode.rank_search,false,ST_Line_Interpolate_Point(linegeo, (housenum::float-orginalstartnumber)/originalnumberrange));
                newpoints := newpoints + 1;
              END LOOP;
            END IF;
            prevnode := nextnode;
          END IF;
        ELSE
          startnumber := ('0'||substring(nextnode.housenumber,'[0-9]+'))::integer;
          IF startnumber IS NOT NULL AND startnumber > 0 THEN
            havefirstpoint := true;
            linestr := 'LINESTRING('||ST_X(nextnode.geometry)||' '||ST_Y(nextnode.geometry);
            prevnode := nextnode;
          END IF;
        END IF;
      END LOOP;
    END IF;
  END IF;

  RETURN newpoints;
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

  IF ST_IsEmpty(NEW.geometry) OR NOT ST_IsValid(NEW.geometry) OR ST_X(ST_Centroid(NEW.geometry))::text in ('NaN','Infinity','-Infinity') OR ST_Y(ST_Centroid(NEW.geometry))::text in ('NaN','Infinity','-Infinity') THEN  
    -- This operation gives postgis an chance to fix the broken data
    -- Expensive operation so only do if needed
    NEW.geometry := ST_buffer(NEW.geometry,0);
    IF ST_IsEmpty(NEW.geometry) OR NOT ST_IsValid(NEW.geometry) OR ST_X(ST_Centroid(NEW.geometry))::text in ('NaN','Infinity','-Infinity') OR ST_Y(ST_Centroid(NEW.geometry))::text in ('NaN','Infinity','-Infinity') THEN  
      RAISE WARNING 'Invalid geometary, rejecting: % %', NEW.osm_type, NEW.osm_id;
      RETURN NULL;
    END IF;
  END IF;

  NEW.place_id := nextval('seq_place');
  NEW.indexed := false;

  IF NEW.housenumber IS NOT NULL THEN
    i := getorcreate_housenumber_id(make_standard_name(NEW.housenumber));
  END IF;

  IF NEW.osm_type = 'X' THEN
    -- E'X'ternal records should already be in the right format so do nothing
  ELSE
    NEW.rank_search := 30;
    NEW.rank_address := NEW.rank_search;

    -- By doing in postgres we have the country available to us - currently only used for postcode
    IF NEW.class = 'place' THEN

      IF NEW.type in ('state') THEN
        NEW.rank_search := 8;
        NEW.rank_address := NEW.rank_search;
      ELSEIF NEW.type in ('county') THEN
        NEW.rank_search := 12;
        NEW.rank_address := NEW.rank_search;
      ELSEIF NEW.type in ('city') THEN
        NEW.rank_search := 16;
        NEW.rank_address := NEW.rank_search;
      ELSEIF NEW.type in ('island') THEN
        NEW.rank_search := 17;
        NEW.rank_address := 0;
      ELSEIF NEW.type in ('town') THEN
        NEW.rank_search := 17;
        NEW.rank_address := NEW.rank_search;
      ELSEIF NEW.type in ('village','hamlet') THEN
        NEW.rank_search := 18;
        NEW.rank_address := 17;
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
        IF NEW.country_code IS NULL THEN
          NEW.country_code := get_country_code(NEW.geometry);
        END IF;

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
          IF upper(NEW.postcode) ~ '^[A-Z0-9]{1,5}$' THEN 
            -- Probably too short to be very local
            NEW.name := ARRAY[ROW('ref',NEW.postcode)::keyvalue];
            NEW.rank_search := 21;
            NEW.rank_address := 11;
          ELSE
            -- Does it look splitable into and area and local code?
            postcode := substring(upper(NEW.postcode) from '^([- :A-Z0-9]+)([- :][A-Z0-9]+)$');

            IF postcode IS NOT NULL THEN

              -- TODO: insert new line into location instead
              --result := add_location(NEW.place_id,NEW.country_code,ARRAY[ROW('ref',postcode)::keyvalue],21,11,NEW.geometry);

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
      ELSEIF NEW.type in ('house','building') THEN
        NEW.rank_search := 28;
        NEW.rank_address := NEW.rank_search;
      ELSEIF NEW.type in ('houses') THEN
        -- insert new point into place for each derived building
        i := create_interpolation(NEW.osm_id, NEW.housenumber);
      END IF;

    ELSEIF NEW.class = 'boundary' THEN
      NEW.country_code := get_country_code(NEW.geometry);
      NEW.rank_search := NEW.admin_level * 2;
      NEW.rank_address := NEW.rank_search;
    ELSEIF NEW.class = 'highway' AND NEW.name is NULL AND 
           NEW.type in ('service','cycleway','footway','bridleway','track','byway','motorway_link','primary_link','trunk_link','secondary_link','tertiary_link') THEN
      RETURN NULL;
    ELSEIF NEW.class = 'railway' AND NEW.type in ('rail') THEN
      RETURN NULL;
    ELSEIF NEW.class = 'waterway' AND NEW.name is NULL THEN
      RETURN NULL;
    ELSEIF NEW.class = 'highway' AND NEW.osm_type != 'N' AND NEW.type in ('cycleway','footway','bridleway','motorway_link','primary_link','trunk_link','secondary_link','tertiary_link') THEN
      NEW.rank_search := 27;
      NEW.rank_address := NEW.rank_search;
    ELSEIF NEW.class = 'highway' AND NEW.osm_type != 'N' THEN
      NEW.rank_search := 26;
      NEW.rank_address := NEW.rank_search;
    ELSEIF NEW.class = 'natural' and NEW.type = 'sea' THEN
      NEW.rank_search := 4;
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

CREATE OR REPLACE FUNCTION place_update() RETURNS 
TRIGGER
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
  iMax FLOAT;
  location RECORD;
  relation RECORD;
  search_diameter FLOAT;
  search_prevdiameter FLOAT;
  search_maxrank INTEGER;
  address_street_word_id INTEGER;
  street_place_id_count INTEGER;

  name_vector INTEGER[];
  nameaddress_vector INTEGER[];

BEGIN

  IF NEW.class = 'place' AND NEW.type = 'postcodearea' THEN
    -- Silently do nothing, this is a horrible hack
    -- TODO: add an extra 'class' of data to tag this properly
    RETURN NEW;
  END IF;

  IF NEW.indexed and NOT OLD.indexed THEN

    search_country_code_conflict := false;

    DELETE FROM search_name WHERE place_id = NEW.place_id;
    DELETE FROM place_addressline WHERE place_id = NEW.place_id;

    -- Adding ourselves to the list simplifies address calculations later
    INSERT INTO place_addressline VALUES (NEW.place_id, NEW.place_id, true, 0); 

    -- What level are we searching from
    search_maxrank := NEW.rank_search;

    -- Default max/min distances to look for a location
    FOR i IN 1..28 LOOP
      search_maxdistance[i] := 1;
      search_mindistance[i] := 0.0;
    END LOOP;
    -- Min/Max size to search
    -- Prevents complete idiocy if it hits broken data
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

    -- Speed up searches - just use the centroid of the feature
    -- cheaper but less acurate
    place_centroid := ST_Centroid(NEW.geometry);
    place_geometry_text := 'ST_GeomFromText('''||ST_AsText(NEW.geometry)||''','||ST_SRID(NEW.geometry)||')';

    -- copy the building number
    -- done here rather than on insert to avoid initial indexing of house numbers
    IF (NEW.name IS NULL OR array_upper(NEW.name,1) IS NULL) AND NEW.housenumber IS NOT NULL THEN
      NEW.name := ARRAY[ROW('ref',NEW.housenumber)::keyvalue];
    END IF;

    -- Initialise the name and address vectors using our name
    name_vector := make_keywords(NEW.name);
    nameaddress_vector := name_vector;

--RAISE WARNING '% %', NEW.place_id, NEW.rank_search;

    -- For low level elements we inherit from our parent road
    IF (NEW.rank_search > 26 OR (NEW.type = 'postcode' AND NEW.rank_search = 25)) THEN

--RAISE WARNING 'finding street for %', NEW;

      NEW.street_place_id := null;

      -- to do that we have to find our parent road
      -- Copy data from linked items (points on ways, addr:street links, relations)
      -- Note that addr:street links can only be indexed once the street itself is indexed
      IF NEW.street_place_id IS NULL AND NEW.osm_type = 'N' THEN

        -- Is this node part of a relation?
        FOR relation IN select * from planet_osm_rels where parts @> ARRAY[NEW.osm_id::integer] and members @> ARRAY['n'||NEW.osm_id]
        LOOP
          -- At the moment we only process one type of relation - associatedStreet
          IF relation.tags @> ARRAY['associatedStreet'] AND array_upper(relation.members, 1) IS NOT NULL THEN
            FOR i IN 1..array_upper(relation.members, 1) BY 2 LOOP
              IF NEW.street_place_id IS NULL AND relation.members[i+1] = 'street' THEN
--RAISE WARNING 'node in relation %',relation;
                SELECT place_id from placex where osm_type='W' and osm_id = substring(relation.members[i],2,200)::integer INTO NEW.street_place_id;
              END IF;
            END LOOP;
          END IF;
        END LOOP;      

--RAISE WARNING 'x1';
        -- Is this node part of a way?
        -- Limit 10 is to work round problem in query plan optimiser
        FOR location IN select * from placex where osm_type = 'W' 
          and osm_id in (select id from planet_osm_ways where nodes && ARRAY[NEW.osm_id::integer] limit 10)
        LOOP
--RAISE WARNING '%', location;
          -- Way IS a road then we are on it - that must be our road
          IF location.rank_search = 26 AND NEW.street_place_id IS NULL THEN
--RAISE WARNING 'node in way that is a street %',location;
            NEW.street_place_id := location.place_id;
          END IF;

          -- Is the WAY part of a relation
          FOR relation IN select * from planet_osm_rels where parts @> ARRAY[location.osm_id::integer] and members @> ARRAY['w'||location.osm_id]
          LOOP
            -- At the moment we only process one type of relation - associatedStreet
            IF relation.tags @> ARRAY['associatedStreet'] AND array_upper(relation.members, 1) IS NOT NULL THEN
              FOR i IN 1..array_upper(relation.members, 1) BY 2 LOOP
                IF NEW.street_place_id IS NULL AND relation.members[i+1] = 'street' THEN
--RAISE WARNING 'node in way that is in a relation %',relation;
                  SELECT place_id from placex where osm_type='W' and osm_id = substring(relation.members[i],2,200)::integer INTO NEW.street_place_id;
                END IF;
              END LOOP;
            END IF;
          END LOOP;
          
          -- If the way contains an explicit name of a street copy it
          IF NEW.street IS NULL AND location.street IS NOT NULL THEN
--RAISE WARNING 'node in way that has a streetname %',location;
            NEW.street := location.street;
          END IF;

          -- If this way is a street interpolation line then it is probably as good as we are going to get
          IF NEW.street_place_id IS NULL AND NEW.street IS NULL AND location.class = 'place' and location.type='houses' THEN
            -- Try and find a way that is close roughly parellel to this line
            FOR relation IN SELECT place_id FROM placex
              WHERE ST_DWithin(location.geometry, placex.geometry, 0.001) and placex.rank_search = 26
              ORDER BY (ST_distance(placex.geometry, ST_Line_Interpolate_Point(location.geometry,0))+
                        ST_distance(placex.geometry, ST_Line_Interpolate_Point(location.geometry,0.5))+
                        ST_distance(placex.geometry, ST_Line_Interpolate_Point(location.geometry,1))) ASC limit 1
            LOOP
--RAISE WARNING 'using nearest street to address interpolation line,0.001 %',relation;
              NEW.street_place_id := relation.place_id;
            END LOOP;
          END IF;

        END LOOP;
                
      END IF;

--RAISE WARNING 'x2';

      IF NEW.street_place_id IS NULL AND NEW.osm_type = 'W' THEN
        -- Is this way part of a relation?
        FOR relation IN select * from planet_osm_rels where parts @> ARRAY[NEW.osm_id::integer] and members @> ARRAY['w'||NEW.osm_id]
        LOOP
          -- At the moment we only process one type of relation - associatedStreet
          IF relation.tags @> ARRAY['associatedStreet'] AND array_upper(relation.members, 1) IS NOT NULL THEN
            FOR i IN 1..array_upper(relation.members, 1) BY 2 LOOP
              IF NEW.street_place_id IS NULL AND relation.members[i+1] = 'street' THEN
--RAISE WARNING 'way that is in a relation %',relation;
                SELECT place_id from placex where osm_type='W' and osm_id = substring(relation.members[i],2,200)::integer INTO NEW.street_place_id;
              END IF;
            END LOOP;
          END IF;
        END LOOP;
      END IF;
      
--RAISE WARNING 'x3';

      IF NEW.street_place_id IS NULL AND NEW.street IS NOT NULL THEN
      	address_street_word_id := getorcreate_name_id(make_standard_name(NEW.street));
        FOR location IN SELECT * FROM search_name WHERE search_name.name_vector @> ARRAY[address_street_word_id]
          AND ST_DWithin(NEW.geometry, search_name.centroid, 0.01) and search_rank = 26 
          ORDER BY ST_distance(NEW.geometry, search_name.centroid) ASC limit 1
        LOOP
--RAISE WARNING 'streetname found nearby %',location;
          NEW.street_place_id := location.place_id;
        END LOOP;
        -- Failed
        IF NEW.street_place_id IS NULL THEN
--RAISE WARNING 'unable to find streetname nearby % %',NEW.street,address_street_word_id;
          RETURN null;
        END IF;
      END IF;

--RAISE WARNING 'x4';

      IF NEW.street_place_id IS NULL THEN
        FOR location IN SELECT place_id FROM placex
          WHERE ST_DWithin(NEW.geometry, placex.geometry, 0.001) and placex.rank_search = 26
          ORDER BY ST_distance(NEW.geometry, placex.geometry) ASC limit 1
        LOOP
--RAISE WARNING 'using nearest street,0.001 %',location;
          NEW.street_place_id := location.place_id;
        END LOOP;
      END IF;

--RAISE WARNING 'x5';

      IF NEW.street_place_id IS NULL THEN
        FOR location IN SELECT place_id FROM placex
          WHERE ST_DWithin(NEW.geometry, placex.geometry, 0.01) and placex.rank_search = 26
          ORDER BY ST_distance(NEW.geometry, placex.geometry) ASC limit 1
        LOOP
--RAISE WARNING 'using nearest street, 0.01 %',location;
          NEW.street_place_id := location.place_id;
        END LOOP;
      END IF;

--RAISE WARNING 'x6';
      
      -- Some unnamed roads won't have been indexed, index now if needed
      select count(*) from place_addressline where place_id = NEW.street_place_id INTO street_place_id_count;
      IF street_place_id_count = 0 THEN
        UPDATE placex set indexed = true where indexed = false and place_id = NEW.street_place_id;
      END IF;
      INSERT INTO place_addressline VALUES (NEW.place_id, NEW.street_place_id, true, 0); 

      IF NEW.name IS NULL THEN
        INSERT INTO place_addressline select NEW.place_id, x.address_place_id, x.fromarea, ST_distance(NEW.geometry, ST_Centroid(placex.geometry)) from place_addressline as x join placex on (address_place_id = placex.place_id) where x.fromarea and x.place_id = NEW.street_place_id;
        INSERT INTO place_addressline select NEW.place_id, x.address_place_id, x.fromarea, ST_distance(NEW.geometry, placex.geometry) from place_addressline as x join placex on (address_place_id = placex.place_id) where not x.fromarea and  x.place_id = NEW.street_place_id;
        return NEW;
      END IF;

      IF NEW.street_place_id IS NOT NULL THEN
        select * from placex where place_id = NEW.street_place_id INTO location;
        IF location.name IS NOT NULL THEN
          nameaddress_vector := array_merge(nameaddress_vector, make_keywords(location.name));
--RAISE WARNING '% % %',nameaddress_vector, NEW.street_place_id, location;
        END IF;
      END IF;

    END IF;

--RAISE WARNING '  INDEXING: %',NEW;

    -- Process area matches (tend to be better quality)
    FOR location IN SELECT
      place_id,
      name,
      keywords,
      country_code,
      rank_address,
      rank_search,
      ST_Distance(place_centroid, centroid) as distance
      FROM location_area
      WHERE ST_Contains(area, place_centroid) and location_area.rank_search < search_maxrank
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
  
      INSERT INTO place_addressline VALUES (NEW.place_id, location.place_id, true, location.distance); 

    END LOOP;

    search_diameter := 0.0;
    -- 16 = city, anything larger tends to be an area so don't continue
    WHILE search_diameter < 1 AND search_maxrank > 16 LOOP

      search_prevdiameter := search_diameter;
      IF search_diameter = 0 THEN
        search_diameter := 0.001;
      ELSE
        search_diameter := search_diameter * 2;
      END IF;
        
      -- Try nearest
      FOR location IN EXECUTE 'SELECT place_id, name, keywords, country_code, rank_address, rank_search,'||
        'ST_Distance('||place_geometry_text||', centroid) as distance'||
        ' FROM location_point_'||(case when search_maxrank > 26 THEN 26 ELSE search_maxrank end)||
        ' WHERE ST_DWithin('||place_geometry_text||', centroid, '||search_diameter||') '||
        '  and ST_Distance('||place_geometry_text||', centroid) > '||search_prevdiameter||
        ' ORDER BY ST_Distance('||place_geometry_text||', centroid) ASC'
      LOOP
      
        IF NEW.country_code IS NULL THEN
          NEW.country_code := location.country_code;
        ELSEIF NEW.country_code != location.country_code THEN
          search_country_code_conflict := true;
        END IF;
    
        -- Find search words
        IF (location.distance < search_maxdistance[location.rank_search]) THEN
--RAISE WARNING '  POINT: % % %', location.keywords, location.place_id, location.distance;
    
          -- Add it to the list of search terms, de-duplicate
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
    
          INSERT INTO place_addressline VALUES (NEW.place_id, location.place_id, false, location.distance); 
    
        ELSE
--RAISE WARNING '  Stopped: % % % %', location.rank_search, location.distance, search_maxdistance[location.rank_search], location.name;
          IF search_maxrank > location.rank_search THEN
            search_maxrank := location.rank_search;
          END IF;
        END IF;
    
      END LOOP;
  
--RAISE WARNING '  POINT LOCATIONS, % %', search_maxrank, search_diameter;
  
    END LOOP; --WHILE

    IF search_country_code_conflict OR NEW.country_code IS NULL THEN
      NEW.country_code := get_country(place_centroid);
    END IF;

    INSERT INTO search_name values (NEW.place_id,NEW.rank_search, NEW.country_code, 
      name_vector, nameaddress_vector, place_centroid);

  END IF;

  return NEW;
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION get_address_by_language(for_place_id BIGINT, languagepref TEXT[]) RETURNS TEXT
  AS $$
DECLARE
  result TEXT[];
  search TEXT[];
  found INTEGER;
  location RECORD;
BEGIN

  found := 1000;
  search := languagepref;
  result := '{}';

  UPDATE placex set indexed = false where indexed = true and place_id = for_place_id;
  UPDATE placex set indexed = true where indexed = false and place_id = for_place_id;

  FOR location IN 
    select rank_address,name,distance,length(name::text) as namelength 
      from place_addressline join placex on (address_place_id = placex.place_id) 
      where place_addressline.place_id = for_place_id and rank_address > 0
      order by rank_address desc,rank_search desc,fromarea desc,distance asc,namelength desc
  LOOP
    IF array_upper(search, 1) IS NOT NULL AND array_upper(location.name, 1) IS NOT NULL THEN
      FOR j IN 1..array_upper(search, 1) LOOP
        FOR k IN 1..array_upper(location.name, 1) LOOP
          IF (found > location.rank_address AND location.name[k].key = search[j] AND location.name[k].value != '') AND NOT result && ARRAY[trim(location.name[k].value)] THEN
            result[(100 - location.rank_address)] := trim(location.name[k].value);
            found := location.rank_address;
          END IF;
        END LOOP;
      END LOOP;
    END IF;
  END LOOP;

  RETURN array_to_string(result,', ');
END;
$$
LANGUAGE plpgsql;
DROP SEQUENCE seq_location;
CREATE SEQUENCE seq_location start 1;

CREATE TYPE wordscore AS (
  word text,
  score float
);

--drop table IF EXISTS query_log;
CREATE TABLE query_log (
  starttime timestamp,
  query text
  );
GRANT INSERT ON query_log TO "www-data" ;

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
  place_id bigint,
  country_code varchar(2),
  name keyvalue[],
  keywords INTEGER[],
  rank_search INTEGER NOT NULL,
  rank_address INTEGER NOT NULL
  );
SELECT AddGeometryColumn('location_area', 'centroid', 4326, 'POINT', 2);
SELECT AddGeometryColumn('location_area', 'area', 4326, 'GEOMETRY', 2);
CREATE INDEX idx_location_area_centroid ON location_area USING GIST (centroid);
CREATE INDEX idx_location_area_area ON location_area USING GIST (area);
CREATE INDEX idx_location_area_place on location_area USING BTREE (place_id);

drop table IF EXISTS location_point;
CREATE TABLE location_point (
  place_id bigint,
  country_code varchar(2),
  name keyvalue[],
  keywords INTEGER[],
  rank_search INTEGER NOT NULL,
  rank_address INTEGER NOT NULL
  );
SELECT AddGeometryColumn('location_point', 'centroid', 4326, 'POINT', 2);
CREATE INDEX idx_location_point_centroid ON location_point USING GIST (centroid);
CREATE INDEX idx_location_point_place_id on location_point USING BTREE (place_id);

select create_tables_location_point();

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
SELECT AddGeometryColumn('search_name', 'centroid', 4326, 'GEOMETRY', 2);
CREATE INDEX idx_search_name_centroid ON search_name USING GIST (centroid);
CREATE INDEX idx_search_name_place_id ON search_name USING BTREE (place_id);

drop table IF EXISTS place_addressline;
CREATE TABLE place_addressline (
  place_id bigint,
  address_place_id bigint,
  fromarea boolean,
  distance float
  );
CREATE INDEX idx_place_addressline_place_id on place_addressline USING BTREE (place_id);
CREATE INDEX idx_place_addressline_address_place_id on place_addressline USING BTREE (address_place_id);

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
  isin TEXT,
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
GRANT UPDATE ON placex to "www-data" ;
GRANT SELECT ON search_name to "www-data" ;
GRANT DELETE on search_name to "www-data" ;
GRANT INSERT on search_name to "www-data" ;
GRANT SELECT on place_addressline to "www-data" ;
GRANT INSERT ON place_addressline to "www-data" ;
GRANT DELETE on place_addressline to "www-data" ;
GRANT SELECT on location_point to "www-data" ;
GRANT SELECT ON seq_word to "www-data" ;
GRANT UPDATE ON seq_word to "www-data" ;
GRANT INSERT ON word to "www-data" ;
GRANT SELECT ON planet_osm_ways to "www-data" ;
GRANT SELECT ON planet_osm_rels to "www-data" ;
GRANT SELECT on location_point to "www-data" ;
GRANT SELECT on location_area to "www-data" ;
GRANT SELECT on location_point_26 to "www-data" ;
GRANT SELECT on location_point_25 to "www-data" ;
GRANT SELECT on location_point_24 to "www-data" ;
GRANT SELECT on location_point_23 to "www-data" ;
GRANT SELECT on location_point_22 to "www-data" ;
GRANT SELECT on location_point_21 to "www-data" ;
GRANT SELECT on location_point_20 to "www-data" ;
GRANT SELECT on location_point_19 to "www-data" ;
GRANT SELECT on location_point_18 to "www-data" ;
GRANT SELECT on location_point_17 to "www-data" ;
GRANT SELECT on location_point_16 to "www-data" ;
GRANT SELECT on location_point_15 to "www-data" ;
GRANT SELECT on location_point_14 to "www-data" ;
GRANT SELECT on location_point_13 to "www-data" ;
GRANT SELECT on location_point_12 to "www-data" ;
GRANT SELECT on location_point_11 to "www-data" ;
GRANT SELECT on location_point_10 to "www-data" ;
GRANT SELECT on location_point_9 to "www-data" ;
GRANT SELECT on location_point_8 to "www-data" ;
GRANT SELECT on location_point_7 to "www-data" ;
GRANT SELECT on location_point_6 to "www-data" ;
GRANT SELECT on location_point_5 to "www-data" ;
GRANT SELECT on location_point_4 to "www-data" ;
GRANT SELECT on location_point_3 to "www-data" ;
GRANT SELECT on location_point_2 to "www-data" ;
GRANT SELECT on location_point_1 to "www-data" ;
GRANT SELECT on country to "www-data" ;

-- insert creates the location tables
CREATE TRIGGER placex_before_insert BEFORE INSERT ON placex
    FOR EACH ROW EXECUTE PROCEDURE place_insert();

-- update populates (or repopulates) the search_name table
CREATE TRIGGER placex_before_update BEFORE UPDATE ON placex
    FOR EACH ROW EXECUTE PROCEDURE place_update();

DROP SEQUENCE seq_place;
CREATE SEQUENCE seq_place start 1;

insert into placex select * from place where osm_type = 'N';
insert into placex select * from place where osm_type = 'W';
insert into placex select * from place where osm_type = 'R';
update placex set indexed = true where not indexed and rank_search <= 26 and name is not null;
update placex set indexed = true where not indexed and rank_search > 26 and name is not null;
