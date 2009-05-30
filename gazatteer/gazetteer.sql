CREATE OR REPLACE FUNCTION transliteration_to_words(src stringlanguagetype[]) RETURNS TEXT[]
  AS $$
DECLARE
  result TEXT[];
  result_pos INTEGER;
  i INTEGER;
  j INTEGER;
  k INTEGER;
  s TEXT;
  w TEXT;
  notfound BOOLEAN;
BEGIN
  result_pos := 0;  
  IF NOT array_upper(src, 1)IS NULL THEN


--RAISE WARNING '%',src;
    FOR i IN 1..array_upper(src, 1) LOOP


--RAISE WARNING '% %',src,i;
      s := transliteration(src[i].string);


      FOR j IN 1..20 LOOP


        w := trim(split_part(s, ' ', j));


        if (w != '') THEN
          notfound := true;
          FOR k IN 1..result_pos LOOP
            IF (result[k] = w) THEN
              notfound := false;
            END IF;
          END LOOP;
          IF (notfound) THEN
            result_pos := result_pos + 1;
            result[result_pos] := w;
          END IF;
        END IF;
      END LOOP;
    END LOOP;
  END IF;
  RETURN result;
END;
$$
LANGUAGE plpgsql IMMUTABLE;



CREATE OR REPLACE FUNCTION get_address_language(address stringlanguagetype[][], languagepref TEXT[]) RETURNS TEXT
  AS $$
DECLARE
  result TEXT;
  resultseperator TEXT;
  search TEXT[];
  found BOOLEAN;
BEGIN

  IF (array_upper(address, 1) is null OR array_upper(address, 2) is null) THEN
    return '';
  END IF;

  search := languagepref || '{name}'::TEXT[];
  result := '';
  resultseperator := '';

  FOR i IN 1..array_upper(address, 2) LOOP
    found := false;
    FOR j IN 1..array_upper(search, 1) LOOP
      FOR k IN 1..array_upper(address, 1) LOOP
        IF (NOT found AND address[k][i].type = search[j] AND address[k][i].string != '') THEN
          result := result || resultseperator || trim(address[k][i].string);
          resultseperator := ', ';
          found := true;
        END IF;     
      END LOOP;
    END LOOP;
  END LOOP;

  RETURN result;
END;
$$
LANGUAGE plpgsql IMMUTABLE;

DROP TRIGGER IF EXISTS place_before_update on place;

delete from place where st_astext(ST_Centroid(geometry)) = 'POINT(-inf -inf)';
delete from place where st_astext(ST_Centroid(geometry)) = 'POINT(inf -inf)';
delete from place where st_astext(ST_Centroid(geometry)) = 'POINT(-inf inf)';
delete from place where st_astext(ST_Centroid(geometry)) = 'POINT(inf inf)';

--update place set geometry = snaptogrid(geometry,0.00001);

drop table importantarea;
CREATE TABLE importantarea (
  name stringlanguagetype[],
  transname TEXT[],
  rank_address INTEGER NOT NULL,
  rank_search INTEGER NOT NULL
  );
SELECT AddGeometryColumn('importantarea', 'centroid', 4326, 'POINT', 2);
SELECT AddGeometryColumn('importantarea', 'area', 4326, 'POLYGON', 2);
insert into importantarea select name, transliteration_to_words(name), rank_address, rank_search, ST_Centroid(geometry), geometry from place where rank_search > 1 and ST_GeometryType(geometry) = 'ST_Polygon';
CREATE INDEX importantarea_centroid_idx ON importantarea USING GIST (centroid);
CREATE INDEX importantarea_area_idx ON importantarea USING GIST (area);

drop table importantplace;
CREATE TABLE importantplace (
  name stringlanguagetype[],
  transname TEXT[],
  rank_address INTEGER NOT NULL,
  rank_search INTEGER NOT NULL
  );
SELECT AddGeometryColumn('importantplace', 'centroid', 4326, 'POINT', 2);
insert into importantplace select name, transliteration_to_words(name), rank_address, rank_search, ST_Centroid(geometry) from place where rank_search > 1 and class = 'place';
CREATE INDEX importantplace_centroid_idx ON importantplace USING GIST (centroid);

CREATE OR REPLACE FUNCTION place_update() RETURNS TRIGGER
  AS $$
DECLARE

  i INTEGER;
  j INTEGER;
  k INTEGER;
  l INTEGER;
  w TEXT;

  nearplace RECORD;
  notfound BOOLEAN;

  search_transname TEXT[];

  address_part stringlanguagetype[][];
  address_part_max INTEGER[];
  address_part_maxtypes INTEGER;
  address_maxdistance INTEGER[];

  search_maxdistance INTEGER[];
  search_string TEXT;
  search_scores wordscore[];
  search_scores_pos integer;
BEGIN

RAISE WARNING '%',NEW;

  address_part := '{{"(,)","(,)","(,)","(,)","(,)","(,)","(,)","(,)"}}';
  FOR i IN 1..100 LOOP
    address_part := address_part || '{{"(,)","(,)","(,)","(,)","(,)","(,)","(,)","(,)"}}'::stringlanguagetype[][];
  END LOOP;

  search_scores_pos := 0;
  address_part_maxtypes := 0;
  FOR i IN 0..8 LOOP
    address_maxdistance[i] := 100000000;
    search_maxdistance[i] := 100000000;
    address_part_max[i] := 0;
  END LOOP;

  -- Part 1 is just the actual name (or should this be address_part[NEW.address_rank]?)
  FOR k IN 1..array_upper(NEW.name,1) LOOP
    address_part[k][1] := NEW.name[k];
    IF (k > address_part_maxtypes) THEN
      address_part_maxtypes := k;
      address_part_max[1] := k;
    END IF;
  END LOOP;

  -- add in the local name (if it has one?)
  search_transname := transliteration_to_words(NEW.name);
  IF (array_upper(search_transname, 1) is not null) THEN
    FOR i IN 1..array_upper(search_transname, 1) LOOP
      search_scores_pos := search_scores_pos + 1;
      search_scores[search_scores_pos] := ROW(search_transname[i], 0)::wordscore;
    END LOOP;
  END IF;

  -- put in the class||type in as a special tag
  search_scores_pos := search_scores_pos + 1;
  search_scores[search_scores_pos] := ROW('tag'||NEW.class||NEW.type, 0)::wordscore;

  -- this is the search name - store it  
  NEW.search_name := '';
  FOR j IN 1..search_scores_pos LOOP
    NEW.search_name := NEW.search_name || ' ' || search_scores[j].word;
  END LOOP;
  
  -- Try for an area match
  FOR nearplace IN SELECT 
    name,
    transname,
    rank_address, 
    rank_search, 
    (ST_distance_spheroid(ST_Centroid(new.geometry),centroid,'SPHEROID["WGS 84",6378137,298.257223563,AUTHORITY["EPSG","7030"]]')/10)::integer as distance
    FROM importantarea
    WHERE ST_Intersects(area, new.geometry) and importantarea.rank_search > NEW.rank_search
    ORDER BY ST_distance_spheroid(ST_Centroid(new.geometry),centroid,'SPHEROID["WGS 84",6378137,298.257223563,AUTHORITY["EPSG","7030"]]') ASC
  LOOP

--RAISE WARNING '%',nearplace;

    -- If we don't have this rank/level of address in these languages yet put them on the list
    IF (nearplace.rank_address > NEW.rank_address) THEN
      FOR k IN 1..array_upper(nearplace.name,1) LOOP

        notfound := true;
        FOR l in 1..address_part_max[nearplace.rank_address] LOOP
          IF (address_part[l][nearplace.rank_address].type = nearplace.name[k].type) THEN
            notfound := false;
          END IF;
        END LOOP;

        IF (notfound) THEN
          address_part_max[nearplace.rank_address] := address_part_max[nearplace.rank_address] + 1;
          address_part[address_part_max[nearplace.rank_address]][nearplace.rank_address] := nearplace.name[k];
          IF (address_part_max[nearplace.rank_address] > address_part_maxtypes) THEN
            address_part_maxtypes := address_part_max[nearplace.rank_address];
          END IF;
        END IF;

      END LOOP;
    END IF;

    -- Add it to the list of search terms, de-duplicate
    FOR i IN 1..array_upper(nearplace.transname, 1) LOOP
      w := nearplace.transname[i];
      notfound := true;
      FOR j IN 1..search_scores_pos LOOP
        IF (search_scores[j].word = w) THEN
          notfound := false;
          IF (search_scores[j].score > nearplace.distance) THEN
            search_scores[j] := ROW(w, nearplace.distance);
          END IF;
        END IF;
      END LOOP;
      IF (notfound) THEN
        search_scores_pos := search_scores_pos + 1;
        search_scores[search_scores_pos] := ROW(w, nearplace.distance);
      END IF;
    END LOOP;

  END LOOP;

  -- Try nearest

  FOR nearplace IN SELECT 
    name,
    transname,
    rank_address, 
    rank_search, 
    ST_distance_spheroid(ST_Centroid(new.geometry),centroid,'SPHEROID["WGS 84",6378137,298.257223563,AUTHORITY["EPSG","7030"]]')::integer as distance
    FROM importantplace
    WHERE ST_DWithin(new.geometry, centroid, 0.3) and importantplace.rank_search > NEW.rank_search
    ORDER BY ST_distance_spheroid(ST_Centroid(new.geometry),centroid,'SPHEROID["WGS 84",6378137,298.257223563,AUTHORITY["EPSG","7030"]]') ASC
  LOOP

--RAISE WARNING '% % %',nearplace,nearplace.distance,address_maxdistance[nearplace.rank_address];

    -- If we don't have this rank/level of address in these languages add them to the list
    IF (nearplace.rank_address > NEW.rank_address AND nearplace.distance < address_maxdistance[nearplace.rank_address]) THEN

--RAISE WARNING '%',nearplace;

      FOR k IN 1..array_upper(nearplace.name,1) LOOP

        notfound := true;
        FOR l in 1..address_part_max[nearplace.rank_address] LOOP
          IF (address_part[l][nearplace.rank_address].type = nearplace.name[k].type) THEN
            notfound := false;
          END IF;
        END LOOP;

        IF (notfound) THEN
          address_part_max[nearplace.rank_address] := address_part_max[nearplace.rank_address] + 1;
          address_part[address_part_max[nearplace.rank_address]][nearplace.rank_address] := nearplace.name[k];
          IF (address_part_max[nearplace.rank_address] > address_part_maxtypes) THEN
            address_part_maxtypes := address_part_max[nearplace.rank_address];
          END IF;
        END IF;

      END LOOP;

      FOR i IN REVERSE nearplace.rank_address..1 LOOP
        IF (nearplace.distance*1.5 < address_maxdistance[i]) THEN
          address_maxdistance[i] := (nearplace.distance*1.5)::integer;
        END IF;
      END LOOP;
    END IF;

    -- Find search words
    IF (nearplace.distance < search_maxdistance[nearplace.rank_search]) THEN

      -- Add it to the list of search terms, de-duplicate
      FOR i IN 1..array_upper(nearplace.transname, 1) LOOP
        w := nearplace.transname[i];
        notfound := true;
        FOR j IN 1..search_scores_pos LOOP
          IF (search_scores[j].word = w) THEN
            notfound := false;
            IF (search_scores[j].score > nearplace.distance) THEN
              search_scores[j] := ROW(w, nearplace.distance);
            END IF;
          END IF;
        END LOOP;
        IF (notfound) THEN
          search_scores_pos := search_scores_pos + 1;
          search_scores[search_scores_pos] := ROW(w, nearplace.distance);
        END IF;
      END LOOP;

      FOR i IN REVERSE nearplace.rank_search..1 LOOP
        IF (nearplace.distance*1.5 < search_maxdistance[i]) THEN
          search_maxdistance[i] := (nearplace.distance*1.5)::integer;
        END IF;
      END LOOP;
    END IF;

  END LOOP;

--RAISE WARNING '%',address_part;

  NEW.address := address_part[1:address_part_maxtypes][1:8];

  -- search_address includes search_name (redundant or optimisation? need to test)
  NEW.search_address := '';
  FOR j IN 1..search_scores_pos LOOP
    NEW.search_address := NEW.search_address || ' ' || search_scores[j].word;
  END LOOP;

  NEW.search_scores := search_scores;

  RETURN NEW;
END;
$$
LANGUAGE plpgsql IMMUTABLE;

CREATE TRIGGER place_before_update BEFORE INSERT OR UPDATE ON place 
    FOR EACH ROW EXECUTE PROCEDURE place_update();

--update place set rank_search=rank_search where osm_type = 'W' and osm_id = 24398446;
--select address from place where osm_type = 'W' and osm_id = 24398446;
--select get_address_language(address,'{}') from place where osm_type = 'W' and osm_id = 24398446;
--select get_address_language(address,'{cy}') from place where osm_type = 'W' and osm_id = 24398446;
--update place set rank_search=rank_search where osm_type = 'N' and osm_id = 27460258;
--select get_address_language(address,'{}') from place where osm_type = 'N' and osm_id = 27460258;
--select * from place where osm_type = 'N' and osm_id = 27460258;
--select * from place where osm_type = 'W' and osm_id = 24398446;
select count(*) from place; select 'now'::timestamp; update place set osm_id = osm_id where osm_type in ('N','W'); select 'now'::timestamp;
--update place set osm_id = osm_id where search_name is null and osm_id in (select osm_id from place order by osm_id limit 100);
--51781
--update place set osm_id = osm_id where osm_id = 77904 and osm_type = 'R';
--update place set osm_id = osm_id where osm_id = 22927378;
--update place set osm_id = osm_id where osm_id = 4255355;
--select rank_address,address,get_address_language(address,'{es}') from place where osm_id = 4255355;
