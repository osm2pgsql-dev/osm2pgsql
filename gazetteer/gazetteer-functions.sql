--DROP TRIGGER IF EXISTS place_before_insert on placex;
--DROP TRIGGER IF EXISTS place_before_update on placex;
--CREATE TYPE addresscalculationtype AS (
--  word text,
--  score integer
--);


CREATE OR REPLACE FUNCTION isbrokengeometry(place geometry) RETURNS BOOLEAN
  AS $$
DECLARE
  NEWgeometry geometry;
BEGIN
  NEWgeometry := place;
  IF ST_IsEmpty(NEWgeometry) OR NOT ST_IsValid(NEWgeometry) OR ST_X(ST_Centroid(NEWgeometry))::text in ('NaN','Infinity','-Infinity') OR ST_Y(ST_Centroid(NEWgeometry))::text in ('NaN','Infinity','-Infinity') THEN  
    RETURN true;
  END IF;
  RETURN false;
END;
$$
LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION clean_geometry(place geometry) RETURNS geometry
  AS $$
DECLARE
  NEWgeometry geometry;
BEGIN
  NEWgeometry := place;
  IF ST_IsEmpty(NEWgeometry) OR NOT ST_IsValid(NEWgeometry) OR ST_X(ST_Centroid(NEWgeometry))::text in ('NaN','Infinity','-Infinity') OR ST_Y(ST_Centroid(NEWgeometry))::text in ('NaN','Infinity','-Infinity') THEN  
    NEWgeometry := ST_buffer(NEWgeometry,0);
    IF ST_IsEmpty(NEWgeometry) OR NOT ST_IsValid(NEWgeometry) OR ST_X(ST_Centroid(NEWgeometry))::text in ('NaN','Infinity','-Infinity') OR ST_Y(ST_Centroid(NEWgeometry))::text in ('NaN','Infinity','-Infinity') THEN  
      RETURN ST_SetSRID(ST_Point(0,0),4326);
    END IF;
  END IF;
  RETURN NEWgeometry;
END;
$$
LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION geometry_sector(place geometry) RETURNS INTEGER
  AS $$
DECLARE
  NEWgeometry geometry;
BEGIN
--  RAISE WARNING '%',place;
  NEWgeometry := place;
  IF ST_IsEmpty(NEWgeometry) OR NOT ST_IsValid(NEWgeometry) OR ST_X(ST_Centroid(NEWgeometry))::text in ('NaN','Infinity','-Infinity') OR ST_Y(ST_Centroid(NEWgeometry))::text in ('NaN','Infinity','-Infinity') THEN  
    NEWgeometry := ST_buffer(NEWgeometry,0);
    IF ST_IsEmpty(NEWgeometry) OR NOT ST_IsValid(NEWgeometry) OR ST_X(ST_Centroid(NEWgeometry))::text in ('NaN','Infinity','-Infinity') OR ST_Y(ST_Centroid(NEWgeometry))::text in ('NaN','Infinity','-Infinity') THEN  
      RETURN NULL;
    END IF;
  END IF;
  RETURN (500-ST_X(ST_Centroid(NEWgeometry))::integer)*1000 + (500-ST_Y(ST_Centroid(NEWgeometry))::integer);
END;
$$
LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION debug_geometry_sector(osmid integer, place geometry) RETURNS INTEGER
  AS $$
DECLARE
  NEWgeometry geometry;
BEGIN
  RAISE WARNING '%',osmid;
  IF osmid = 61315 THEN
    return null;
  END IF;
  NEWgeometry := place;
  IF ST_IsEmpty(NEWgeometry) OR NOT ST_IsValid(NEWgeometry) OR ST_X(ST_Centroid(NEWgeometry))::text in ('NaN','Infinity','-Infinity') OR ST_Y(ST_Centroid(NEWgeometry))::text in ('NaN','Infinity','-Infinity') THEN  
    NEWgeometry := ST_buffer(NEWgeometry,0);
    IF ST_IsEmpty(NEWgeometry) OR NOT ST_IsValid(NEWgeometry) OR ST_X(ST_Centroid(NEWgeometry))::text in ('NaN','Infinity','-Infinity') OR ST_Y(ST_Centroid(NEWgeometry))::text in ('NaN','Infinity','-Infinity') THEN  
      RETURN NULL;
    END IF;
  END IF;
  RETURN (500-ST_X(ST_Centroid(NEWgeometry))::integer)*1000 + (500-ST_Y(ST_Centroid(NEWgeometry))::integer);
END;
$$
LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION geometry_index(place geometry,indexed BOOLEAN,name keyvalue[]) RETURNS INTEGER
  AS $$
BEGIN
IF indexed THEN RETURN NULL; END IF;
IF name is null THEN RETURN NULL; END IF;
RETURN geometry_sector(place);
END;
$$
LANGUAGE plpgsql IMMUTABLE;

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
      'rank_address INTEGER NOT NULL,'||
      'is_area BOOLEAN NOT NULL'||
      ')';
   EXECUTE 'SELECT AddGeometryColumn(''location_point'||postfix||''', ''centroid'', 4326, ''POINT'', 2)';
   EXECUTE 'CREATE INDEX idx_location_point'||postfix||'_centroid ON location_point'||postfix||' USING GIST (centroid)';
   EXECUTE 'CREATE INDEX idx_location_point'||postfix||'_sector ON location_point'||postfix||' USING BTREE (geometry_sector(centroid))';
   EXECUTE 'CREATE INDEX idx_location_point'||postfix||'_place_id ON location_point'||postfix||' USING BTREE (place_id)';
   EXECUTE 'CLUSTER location_point'||postfix||' USING idx_location_point'||postfix||'_sector';
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

-- postgresql regex replace seems very inefficient so avoid
--out := replace(out, ' rd ',' road ');
--out := replace(out, ' street ',' st ');
--out := replace(out, ' saint ',' st ');
--out := replace(out, ' dr ',' drive ');
--out := replace(out, ' drv ',' drive ');
--out := replace(out, ' av ',' avenue ');
--out := replace(out, ' ave ',' avenue ');
--out := replace(out, ' ln ',' lane ');
--out := replace(out, ' ct ',' court ');
--out := replace(out, ' pl ',' place ');

--out := replace(out, ' bwy ',' broadway ');
--out := replace(out, ' blvd ',' boulevard ');
--out := replace(out, ' bvd ',' boulevard ');
--out := replace(out, ' bld ',' boulevard ');
--out := replace(out, ' bd ',' boulevard ');

--out := replace(out, ' cl ',' close ');
--out := replace(out, ' cres ',' crescent ');
--out := replace(out, ' est ',' estate ');
--out := replace(out, ' gdn ',' garden ');
--out := replace(out, ' gdns ',' gardens ');
--out := replace(out, ' gro ',' grove ');
--out := replace(out, ' hwy ',' highway ');
--out := replace(out, ' sq ',' square ');

--out := replace(out, ' n ',' north ');
--out := replace(out, ' s ',' south ');
--out := replace(out, ' e ',' east ');
--out := replace(out, ' w ',' west ');

--out := replace(out, ' ne ',' north east ');
--out := replace(out, ' nw ',' north west ');
--out := replace(out, ' se ',' south east ');
--out := replace(out, ' sw ',' south west ');

out := replace(out, ' north east ',' ne ');
out := replace(out, ' north west ',' nw ');
out := replace(out, ' south east ',' se ');
out := replace(out, ' south west ',' sw ');
out := replace(out, ' north-east ',' ne ');
out := replace(out, ' north-west ',' nw ');
out := replace(out, ' south-east ',' se ');
out := replace(out, ' south-west ',' sw ');

--out := replace(out, ' northeast ',' north east ');
--out := replace(out, ' northwest ',' north west ');
--out := replace(out, ' southeast ',' south east ');
--out := replace(out, ' southwest ',' south west ');

out := replace(out, ' acceso ',' acces ');
out := replace(out, ' acequia ',' aceq ');
out := replace(out, ' air force base ',' afb ');
out := replace(out, ' air national guard base ',' angb ');
out := replace(out, ' al ',' ale ');
out := replace(out, ' alameda ',' alam ');
out := replace(out, ' alea ',' ale ');
out := replace(out, ' aleea ',' ale ');
out := replace(out, ' aleja ',' al ');
out := replace(out, ' alejach ',' al ');
out := replace(out, ' aleje ',' al ');
out := replace(out, ' aleji ',' al ');
out := replace(out, ' all ',' al ');
out := replace(out, ' allee ',' all ');
out := replace(out, ' alley ',' al ');
out := replace(out, ' alqueria ',' alque ');
out := replace(out, ' alue ',' al ');
out := replace(out, ' aly ',' al ');
out := replace(out, ' am ',' a ');
out := replace(out, ' an der ',' a d ');
out := replace(out, ' andador ',' andad ');
out := replace(out, ' angosta ',' angta ');
out := replace(out, ' apartamentos ',' aptos ');
out := replace(out, ' apartments ',' apts ');
out := replace(out, ' apeadero ',' apdro ');
out := replace(out, ' app ',' apch ');
out := replace(out, ' approach ',' apch ');
out := replace(out, ' arboleda ',' arb ');
out := replace(out, ' arrabal ',' arral ');
out := replace(out, ' arroyo ',' arry ');
out := replace(out, ' auf der ',' a d ');
out := replace(out, ' aukio ',' auk ');
out := replace(out, ' autopista ',' auto ');
out := replace(out, ' autovia ',' autov ');
out := replace(out, ' avd ',' av ');
out := replace(out, ' avda ',' av ');
out := replace(out, ' ave ',' av ');
out := replace(out, ' avenida ',' av ');
out := replace(out, ' avenue ',' av ');
out := replace(out, ' avinguda ',' av ');
out := replace(out, ' b dul ',' bd ');
out := replace(out, ' back ',' bk ');
out := replace(out, ' bad ',' b ');
out := replace(out, ' bahnhof ',' bhf ');
out := replace(out, ' bajada ',' bjada ');
out := replace(out, ' balneario ',' balnr ');
out := replace(out, ' banda ',' b ');
out := replace(out, ' barranco ',' branc ');
out := replace(out, ' barranquil ',' bqllo ');
out := replace(out, ' barriada ',' barda ');
out := replace(out, ' barrio ',' bo ');
out := replace(out, ' barro ',' bo ');
out := replace(out, ' bda ',' b ');
out := replace(out, ' bdul ',' bd ');
out := replace(out, ' berg ',' bg ');
out := replace(out, ' bf ',' bhf ');
out := replace(out, ' bldngs ',' bldgs ');
out := replace(out, ' blok ',' bl ');
out := replace(out, ' bloque ',' blque ');
out := replace(out, ' blv ',' bd ');
out := replace(out, ' blvd ',' bd ');
out := replace(out, ' boulevard ',' blvd ');
out := replace(out, ' boundary ',' bdy ');
out := replace(out, ' brazal ',' brzal ');
out := replace(out, ' bri ',' brdg ');
out := replace(out, ' bridge ',' brdg ');
out := replace(out, ' broadway ',' bway ');
out := replace(out, ' broeder ',' br ');
out := replace(out, ' brucke ',' br ');
out := replace(out, ' buildings ',' bldgs ');
out := replace(out, ' bul ',' bd ');
out := replace(out, ' bulev ',' bd ');
out := replace(out, ' bulevar ',' bulev ');
out := replace(out, ' bulevard ',' bd ');
out := replace(out, ' bulevardu ',' bd ');
out := replace(out, ' bulevardul ',' bd ');
out := replace(out, ' bulievard ',' bul ');
out := replace(out, ' bulvar ',' bl ');
out := replace(out, ' bulvari ',' bl ');
out := replace(out, ' burg ',' bg ');
out := replace(out, ' burgemeester ',' burg ');
out := replace(out, ' business ',' bus ');
out := replace(out, ' buu dien ',' bd ');
out := replace(out, ' bvd ',' blvd ');
out := replace(out, ' bypass ',' byp ');
out := replace(out, ' c le ',' c ');
out := replace(out, ' cadde ',' cd ');
out := replace(out, ' caddesi ',' cd ');
out := replace(out, ' calle ',' c ');
out := replace(out, ' calleja ',' cllja ');
out := replace(out, ' callejon ',' callej ');
out := replace(out, ' callejuela ',' cjla ');
out := replace(out, ' callizo ',' cllzo ');
out := replace(out, ' calzada ',' czada ');
out := replace(out, ' camino ',' cno ');
out := replace(out, ' camino hondo ',' c h ');
out := replace(out, ' camino nuevo ',' c n ');
out := replace(out, ' camino viejo ',' c v ');
out := replace(out, ' camping ',' campg ');
out := replace(out, ' campo ',' c po ');
out := replace(out, ' can cu khong quan ',' cckq ');
out := replace(out, ' cantera ',' cantr ');
out := replace(out, ' cantina ',' canti ');
out := replace(out, ' canton ',' cant ');
out := replace(out, ' cao dang ',' cd ');
out := replace(out, ' caravan ',' cvn ');
out := replace(out, ' carrer ',' c ');
out := replace(out, ' carrera ',' cra ');
out := replace(out, ' carrero ',' cro ');
out := replace(out, ' carretera ',' ctra ');
out := replace(out, ' carreterin ',' ctrin ');
out := replace(out, ' carretil ',' crtil ');
out := replace(out, ' carril ',' crril ');
out := replace(out, ' caserio ',' csrio ');
out := replace(out, ' cau ldhc bo ',' clb ');
out := replace(out, ' causeway ',' cswy ');
out := replace(out, ' center ',' cen ');
out := replace(out, ' centre ',' cen ');
out := replace(out, ' cesta ',' c ');
out := replace(out, ' chalet ',' chlet ');
out := replace(out, ' che ',' ch ');
out := replace(out, ' chemin ',' ch ');
out := replace(out, ' cinturon ',' cint ');
out := replace(out, ' circle ',' cir ');
out := replace(out, ' circuit ',' cct ');
out := replace(out, ' circunvalacion ',' ccvcn ');
out := replace(out, ' city ',' cty ');
out := replace(out, ' cjon ',' callej ');
out := replace(out, ' cl ',' c ');
out := replace(out, ' cllon ',' callej ');
out := replace(out, ' close ',' cl ');
out := replace(out, ' cmno ',' cno ');
out := replace(out, ' cobertizo ',' cbtiz ');
out := replace(out, ' colonia ',' col ');
out := replace(out, ' commandant ',' cmdt ');
out := replace(out, ' common ',' comm ');
out := replace(out, ' community ',' comm ');
out := replace(out, ' complejo ',' compj ');
out := replace(out, ' cong truong ',' ct ');
out := replace(out, ' cong ty ',' cty ');
out := replace(out, ' cong ty co phyn ',' ctcp ');
out := replace(out, ' cong vien ',' cv ');
out := replace(out, ' cong vien van hoa ',' cvvh ');
out := replace(out, ' conjunto ',' cjto ');
out := replace(out, ' convento ',' cnvto ');
out := replace(out, ' cooperativa ',' coop ');
out := replace(out, ' corral ',' crral ');
out := replace(out, ' corralillo ',' crrlo ');
out := replace(out, ' corredor ',' crrdo ');
out := replace(out, ' corso ',' c so ');
out := replace(out, ' corte ',' c te ');
out := replace(out, ' cortijo ',' crtjo ');
out := replace(out, ' costanilla ',' cstan ');
out := replace(out, ' costera ',' coste ');
out := replace(out, ' cottages ',' cotts ');
out := replace(out, ' county ',' co ');
out := replace(out, ' county route ',' cr ');
out := replace(out, ' cours ',' crs ');
out := replace(out, ' court ',' crt ');
out := replace(out, ' creek ',' cr ');
out := replace(out, ' crescent ',' cres ');
out := replace(out, ' crk ',' cr ');
out := replace(out, ' croft ',' cft ');
out := replace(out, ' crossing ',' xing ');
out := replace(out, ' ct ',' crt ');
out := replace(out, ' ctr ',' cen ');
out := replace(out, ' cty cp ',' ctcp ');
out := replace(out, ' cuadra ',' cuadr ');
out := replace(out, ' cuesta ',' custa ');
out := replace(out, ' cway ',' cswy ');
out := replace(out, ' ddhi hoc ',' dh ');
out := replace(out, ' ddhi lo ',' dl ');
out := replace(out, ' dehesa ',' dhsa ');
out := replace(out, ' demarcacion ',' demar ');
out := replace(out, ' diagonal ',' diag ');
out := replace(out, ' diseminado ',' disem ');
out := replace(out, ' doctor ',' dr ');
out := replace(out, ' dokter ',' dr ');
out := replace(out, ' doktor ',' d r ');
out := replace(out, ' dolna ',' dln ');
out := replace(out, ' dolne ',' dln ');
out := replace(out, ' dolny ',' dln ');
out := replace(out, ' dominee ',' ds ');
out := replace(out, ' dorf ',' df ');
out := replace(out, ' dotsient ',' dots ');
out := replace(out, ' drive ',' dr ');
out := replace(out, ' druga ',' 2 ');
out := replace(out, ' drugi ',' 2 ');
out := replace(out, ' drugie ',' 2 ');
out := replace(out, ' duong ',' d ');
out := replace(out, ' duong sat ',' ds ');
out := replace(out, ' duza ',' dz ');
out := replace(out, ' duze ',' dz ');
out := replace(out, ' duzy ',' dz ');
out := replace(out, ' east ',' e ');
out := replace(out, ' edificio ',' edifc ');
out := replace(out, ' empresa ',' empr ');
out := replace(out, ' entrada ',' entd ');
out := replace(out, ' escalera ',' esca ');
out := replace(out, ' escalinata ',' escal ');
out := replace(out, ' espalda ',' eslda ');
out := replace(out, ' esplanade ','  ');
out := replace(out, ' estacion ',' estcn ');
out := replace(out, ' estate ',' est ');
out := replace(out, ' estrada ',' estda ');
out := replace(out, ' explanada ',' expla ');
out := replace(out, ' expressway ',' expy ');
out := replace(out, ' extramuros ',' extrm ');
out := replace(out, ' extrarradio ',' extrr ');
out := replace(out, ' fabrica ',' fca ');
out := replace(out, ' faubourg ',' fg ');
out := replace(out, ' fbrca ',' fca ');
out := replace(out, ' ferry ',' fy ');
out := replace(out, ' fondamenta ',' f ta ');
out := replace(out, ' fort ',' ft ');
out := replace(out, ' freeway ',' frwy ');
out := replace(out, ' fundacul ',' fdc ');
out := replace(out, ' fundatura ',' fnd ');
out := replace(out, ' fwy ',' frwy ');
out := replace(out, ' g ',' gt ');
out := replace(out, ' galeria ',' gale ');
out := replace(out, ' gamla ',' gla ');
out := replace(out, ' gardens ',' gdn ');
out := replace(out, ' gata ',' gt ');
out := replace(out, ' gatan ',' g ');
out := replace(out, ' gate ',' ga ');
out := replace(out, ' gaten ',' gt ');
out := replace(out, ' gebroeders ',' gebr ');
out := replace(out, ' generaal ',' gen ');
out := replace(out, ' gienieral ',' ghien ');
out := replace(out, ' glorieta ',' gta ');
out := replace(out, ' gorna ',' grn ');
out := replace(out, ' gorne ',' grn ');
out := replace(out, ' gorny ',' grn ');
out := replace(out, ' gracht ',' gr ');
out := replace(out, ' grad ',' ghr ');
out := replace(out, ' gran via ',' g v ');
out := replace(out, ' grand ',' gr ');
out := replace(out, ' granden ',' gr ');
out := replace(out, ' granja ',' granj ');
out := replace(out, ' grosse ',' gr ');
out := replace(out, ' grosser ',' gr ');
out := replace(out, ' grosses ',' gr ');
out := replace(out, ' grove ',' gro ');
out := replace(out, ' gt ',' ga ');
out := replace(out, ' hauptbahnhof ',' hbf ');
out := replace(out, ' heights ',' hgts ');
out := replace(out, ' heiligen ',' hl ');
out := replace(out, ' high school ',' hs ');
out := replace(out, ' highway ',' hwy ');
out := replace(out, ' hipodromo ',' hipod ');
out := replace(out, ' hospital ',' hosp ');
out := replace(out, ' house ',' ho ');
out := replace(out, ' hse ',' ho ');
out := replace(out, ' hts ',' hgts ');
out := replace(out, ' im ',' i ');
out := replace(out, ' impasse ',' imp ');
out := replace(out, ' in ',' i ');
out := replace(out, ' in der ',' i d ');
out := replace(out, ' industrial ',' ind ');
out := replace(out, ' ingenieur ',' ir ');
out := replace(out, ' international ',' intl ');
out := replace(out, ' intr ',' int ');
out := replace(out, ' intrarea ',' int ');
out := replace(out, ' island ',' is ');
out := replace(out, ' jardin ',' jdin ');
out := replace(out, ' jonkheer ',' jhr ');
out := replace(out, ' k s ',' ks ');
out := replace(out, ' kaari ',' kri ');
out := replace(out, ' kanunnik ',' kan ');
out := replace(out, ' kapitan ',' kap ');
out := replace(out, ' kardinaal ',' kard ');
out := replace(out, ' katu ',' k ');
out := replace(out, ' khach sdhn ',' ks ');
out := replace(out, ' khu cong nghiep ',' kcn ');
out := replace(out, ' khu du lich ',' kdl ');
out := replace(out, ' khu nghi mat ',' knm ');
out := replace(out, ' kleine ',' kl ');
out := replace(out, ' kleiner ',' kl ');
out := replace(out, ' kleines ',' kl ');
out := replace(out, ' kolo ',' k ');
out := replace(out, ' kolonel ',' kol ');
out := replace(out, ' kolonia ',' kol ');
out := replace(out, ' koning ',' kon ');
out := replace(out, ' koningin ',' kon ');
out := replace(out, ' kort e ',' kte ');
out := replace(out, ' kuja ',' kj ');
out := replace(out, ' kvartal ',' kv ');
out := replace(out, ' kyla ',' kl ');
out := replace(out, ' laan ',' ln ');
out := replace(out, ' ladera ',' ldera ');
out := replace(out, ' lane ',' ln ');
out := replace(out, ' lange ',' l ');
out := replace(out, ' largo ',' l go ');
out := replace(out, ' lille ',' ll ');
out := replace(out, ' line ',' ln ');
out := replace(out, ' little ',' lit ');
out := replace(out, ' llanura ',' llnra ');
out := replace(out, ' lower ',' low ');
out := replace(out, ' luitenant ',' luit ');
out := replace(out, ' lwr ',' low ');
out := replace(out, ' m te ',' m tele ');
out := replace(out, ' maantee ',' mnt ');
out := replace(out, ' mala ',' ml ');
out := replace(out, ' male ',' ml ');
out := replace(out, ' malecon ',' malec ');
out := replace(out, ' maly ',' ml ');
out := replace(out, ' manor ',' mnr ');
out := replace(out, ' mansions ',' mans ');
out := replace(out, ' market ',' mkt ');
out := replace(out, ' markt ',' mkt ');
out := replace(out, ' mazowiecka ',' maz ');
out := replace(out, ' mazowiecki ',' maz ');
out := replace(out, ' mazowieckie ',' maz ');
out := replace(out, ' meadows ',' mdws ');
out := replace(out, ' medical ',' med ');
out := replace(out, ' meester ',' mr ');
out := replace(out, ' mercado ',' merc ');
out := replace(out, ' mevrouw ',' mevr ');
out := replace(out, ' mews ',' m ');
out := replace(out, ' miasto ',' m ');
out := replace(out, ' middle ',' mid ');
out := replace(out, ' middle school ',' ms ');
out := replace(out, ' mile ',' mi ');
out := replace(out, ' military ',' mil ');
out := replace(out, ' mirador ',' mrdor ');
out := replace(out, ' mitropolit ',' mit ');
out := replace(out, ' mnt ',' m tele ');
out := replace(out, ' monasterio ',' mtrio ');
out := replace(out, ' monseigneur ',' mgr ');
out := replace(out, ' mount ',' mt ');
out := replace(out, ' mountain ',' mtn ');
out := replace(out, ' mt ',' m tele ');
out := replace(out, ' muelle ',' muell ');
out := replace(out, ' municipal ',' mun ');
out := replace(out, ' muntele ',' m tele ');
out := replace(out, ' museum ',' mus ');
out := replace(out, ' na ',' n ');
out := replace(out, ' namesti ',' nam ');
out := replace(out, ' namestie ',' nam ');
out := replace(out, ' national park ',' np ');
out := replace(out, ' national recreation area ',' nra ');
out := replace(out, ' national wildlife refuge area ',' nwra ');
out := replace(out, ' nha hat ',' nh ');
out := replace(out, ' nha thi dzu ',' ntd ');
out := replace(out, ' nha tho ',' nt ');
out := replace(out, ' nordre ',' ndr ');
out := replace(out, ' norra ',' n ');
out := replace(out, ' north ',' n ');
out := replace(out, ' northeast ',' ne ');
out := replace(out, ' northwest ',' nw ');
out := replace(out, ' nowa ',' nw ');
out := replace(out, ' nowe ',' nw ');
out := replace(out, ' nowy ',' nw ');
out := replace(out, ' nucleo ',' ncleo ');
out := replace(out, ' oa ',' o ');
out := replace(out, ' ob ',' o ');
out := replace(out, ' obere ',' ob ');
out := replace(out, ' oberer ',' ob ');
out := replace(out, ' oberes ',' ob ');
out := replace(out, ' olv ',' o l v ');
out := replace(out, ' onze lieve vrouw e ',' o l v ');
out := replace(out, ' osiedle ',' os ');
out := replace(out, ' osiedlu ',' os ');
out := replace(out, ' ostra ',' o ');
out := replace(out, ' p k ',' pk ');
out := replace(out, ' p ta ',' pta ');
out := replace(out, ' p zza ',' p za ');
out := replace(out, ' palacio ',' palac ');
out := replace(out, ' pantano ',' pant ');
out := replace(out, ' paraje ',' praje ');
out := replace(out, ' park ',' pk ');
out := replace(out, ' parkway ',' pkwy ');
out := replace(out, ' parque ',' pque ');
out := replace(out, ' particular ',' parti ');
out := replace(out, ' partida ',' ptda ');
out := replace(out, ' pasadizo ',' pzo ');
out := replace(out, ' pasaje ',' psaje ');
out := replace(out, ' paseo ',' po ');
out := replace(out, ' paseo maritimo ',' psmar ');
out := replace(out, ' pasillo ',' psllo ');
out := replace(out, ' pass ',' pas ');
out := replace(out, ' passage ',' pas ');
out := replace(out, ' passatge ',' ptge ');
out := replace(out, ' passeig ',' pg ');
out := replace(out, ' pastoor ',' past ');
out := replace(out, ' penger ',' pgr ');
out := replace(out, ' pfad ',' p ');
out := replace(out, ' ph ',' p ');
out := replace(out, ' phi truong ',' pt ');
out := replace(out, ' phuong ',' p ');
out := replace(out, ' piata ',' pta ');
out := replace(out, ' piazza ',' p za ');
out := replace(out, ' piazzale ',' p le ');
out := replace(out, ' piazzetta ',' p ta ');
out := replace(out, ' pierwsza ',' 1 ');
out := replace(out, ' pierwsze ',' 1 ');
out := replace(out, ' pierwszy ',' 1 ');
out := replace(out, ' pike ',' pk ');
out := replace(out, ' pky ',' pkwy ');
out := replace(out, ' plac ',' pl ');
out := replace(out, ' placa ',' pl ');
out := replace(out, ' place ',' pl ');
out := replace(out, ' placem ',' pl ');
out := replace(out, ' placu ',' pl ');
out := replace(out, ' plass ',' pl ');
out := replace(out, ' plassen ',' pl ');
out := replace(out, ' plats ',' pl ');
out := replace(out, ' platsen ',' pl ');
out := replace(out, ' platz ',' pl ');
out := replace(out, ' plaza ',' pl ');
out := replace(out, ' plazoleta ',' pzta ');
out := replace(out, ' plazuela ',' plzla ');
out := replace(out, ' plein ',' pln ');
out := replace(out, ' ploshchad ',' pl ');
out := replace(out, ' plz ',' pl ');
out := replace(out, ' plza ',' pl ');
out := replace(out, ' poblado ',' pbdo ');
out := replace(out, ' point ',' pt ');
out := replace(out, ' poligono ',' polig ');
out := replace(out, ' poligono industrial ',' pgind ');
out := replace(out, ' polku ',' p ');
out := replace(out, ' ponte ',' p te ');
out := replace(out, ' porta ',' p ta ');
out := replace(out, ' portal ',' prtal ');
out := replace(out, ' portico ',' prtco ');
out := replace(out, ' portillo ',' ptilo ');
out := replace(out, ' prazuela ',' przla ');
out := replace(out, ' precinct ',' pct ');
out := replace(out, ' president ',' pres ');
out := replace(out, ' prins ',' pr ');
out := replace(out, ' prinses ',' pr ');
out := replace(out, ' professor ',' prof ');
out := replace(out, ' profiesor ',' prof ');
out := replace(out, ' prolongacion ',' prol ');
out := replace(out, ' promenade ',' prom ');
out := replace(out, ' pueblo ',' pblo ');
out := replace(out, ' puente ',' pnte ');
out := replace(out, ' puerta ',' pta ');
out := replace(out, ' puerto ',' pto ');
out := replace(out, ' puistikko ',' pko ');
out := replace(out, ' puisto ',' ps ');
out := replace(out, ' punto kilometrico ',' pk ');
out := replace(out, ' pza ',' pl ');
out := replace(out, ' quai ',' qu ');
out := replace(out, ' quan ',' q ');
out := replace(out, ' qucyng truong ',' qt ');
out := replace(out, ' quelle ',' qu ');
out := replace(out, ' quoc lo ',' ql ');
out := replace(out, ' raitti ',' r ');
out := replace(out, ' rambla ',' rbla ');
out := replace(out, ' rampla ',' rampa ');
out := replace(out, ' ranta ',' rt ');
out := replace(out, ' rdhp hat ',' rh ');
out := replace(out, ' reservation ',' res ');
out := replace(out, ' reservoir ',' res ');
out := replace(out, ' residencial ',' resid ');
out := replace(out, ' rhein ',' rh ');
out := replace(out, ' ribera ',' rbra ');
out := replace(out, ' rincon ',' rcon ');
out := replace(out, ' rinconada ',' rcda ');
out := replace(out, ' rinne ',' rn ');
out := replace(out, ' rise ',' ri ');
out := replace(out, ' riv ',' r ');
out := replace(out, ' river ',' r ');
out := replace(out, ' road ',' rd ');
out := replace(out, ' rotonda ',' rtda ');
out := replace(out, ' route ',' rt ');
out := replace(out, ' rte ',' rt ');
out := replace(out, ' rue ',' r ');
out := replace(out, ' sa ',' s ');
out := replace(out, ' saint ',' st ');
out := replace(out, ' sainte ',' ste ');
out := replace(out, ' salizada ',' s da ');
out := replace(out, ' san bay ',' sb ');
out := replace(out, ' san bay quoc te ',' sbqt ');
out := replace(out, ' san van dong ',' svd ');
out := replace(out, ' sanatorio ',' sanat ');
out := replace(out, ' sankt ',' st ');
out := replace(out, ' santuario ',' santu ');
out := replace(out, ' sdla ',' str la ');
out := replace(out, ' sector ',' sect ');
out := replace(out, ' sendera ',' sedra ');
out := replace(out, ' sendero ',' send ');
out := replace(out, ' sielo ',' s ');
out := replace(out, ' sint ',' st ');
out := replace(out, ' sodra ',' s ');
out := replace(out, ' sok ',' sk ');
out := replace(out, ' sokagi ',' sk ');
out := replace(out, ' sokak ',' sk ');
out := replace(out, ' sondre ',' sdr ');
out := replace(out, ' soseaua ',' sos ');
out := replace(out, ' south ',' s ');
out := replace(out, ' southeast ',' se ');
out := replace(out, ' southwest ',' sw ');
out := replace(out, ' spl ',' sp ');
out := replace(out, ' splaiul ',' sp ');
out := replace(out, ' spodnja ',' sp ');
out := replace(out, ' spodnje ',' sp ');
out := replace(out, ' spodnji ',' sp ');
out := replace(out, ' square ',' sq ');
out := replace(out, ' srednja ',' sr ');
out := replace(out, ' srednje ',' sr ');
out := replace(out, ' srednji ',' sr ');
out := replace(out, ' stara ',' st ');
out := replace(out, ' stare ',' st ');
out := replace(out, ' stary ',' st ');
out := replace(out, ' state highway ',' sh ');
out := replace(out, ' state route ',' sr ');
out := replace(out, ' station ',' stn ');
out := replace(out, ' stazione ',' staz ');
out := replace(out, ' steenweg ',' stwg ');
out := replace(out, ' sth ',' s ');
out := replace(out, ' stig ',' st ');
out := replace(out, ' stigen ',' st ');
out := replace(out, ' store ',' st ');
out := replace(out, ' str ',' strada ');
out := replace(out, ' stra ',' strada ');
out := replace(out, ' straat ',' str ');
out := replace(out, ' strada comunale ',' sc ');
out := replace(out, ' strada provinciale ',' sp ');
out := replace(out, ' strada regionale ',' sr ');
out := replace(out, ' strada statale ',' ss ');
out := replace(out, ' stradela ',' str la ');
out := replace(out, ' strasse ',' str ');
out := replace(out, ' street ',' st ');
out := replace(out, ' subida ',' sbida ');
out := replace(out, ' sveta ',' sv ');
out := replace(out, ' sveti ',' sv ');
out := replace(out, ' svieti ',' sv ');
out := replace(out, ' taival ',' tvl ');
out := replace(out, ' tanav ',' tn ');
out := replace(out, ' tct ',' tcty ');
out := replace(out, ' terr ',' ter ');
out := replace(out, ' terrace ',' ter ');
out := replace(out, ' thanh pho ',' tp ');
out := replace(out, ' thi trzn ',' tt ');
out := replace(out, ' thi xa ',' tx ');
out := replace(out, ' tie ',' t ');
out := replace(out, ' tieu hoc ',' th ');
out := replace(out, ' tinh lo ',' tl ');
out := replace(out, ' tong cong ty ',' tcty ');
out := replace(out, ' tori ',' tr ');
out := replace(out, ' torrente ',' trrnt ');
out := replace(out, ' towers ',' twrs ');
out := replace(out, ' township ',' twp ');
out := replace(out, ' tpke ',' tpk ');
out := replace(out, ' trail ',' trl ');
out := replace(out, ' transito ',' trans ');
out := replace(out, ' transversal ',' trval ');
out := replace(out, ' trasera ',' tras ');
out := replace(out, ' travesia ',' trva ');
out := replace(out, ' trung hoc co so ',' thcs ');
out := replace(out, ' trung hoc pho thong ',' thpt ');
out := replace(out, ' trung tam ',' tt ');
out := replace(out, ' trung tam thuong mdhi ',' tttm ');
out := replace(out, ' trzeci ',' 3 ');
out := replace(out, ' trzecia ',' 3 ');
out := replace(out, ' trzecie ',' 3 ');
out := replace(out, ' tunnel ',' tun ');
out := replace(out, ' turnpike ',' tpk ');
out := replace(out, ' ulica ',' ul ');
out := replace(out, ' ulice ',' ul ');
out := replace(out, ' ulicy ',' ul ');
out := replace(out, ' ulitsa ',' ul ');
out := replace(out, ' university ',' univ ');
out := replace(out, ' untere ',' u ');
out := replace(out, ' unterer ',' u ');
out := replace(out, ' unteres ',' u ');
out := replace(out, ' upper ',' up ');
out := replace(out, ' upr ',' up ');
out := replace(out, ' urbanizacion ',' urb ');
out := replace(out, ' utca ',' u ');
out := replace(out, ' va ',' v ');
out := replace(out, ' vag ',' v ');
out := replace(out, ' vagen ',' v ');
out := replace(out, ' vale ',' va ');
out := replace(out, ' van ',' v ');
out := replace(out, ' van de ',' v d ');
out := replace(out, ' varf ',' vf ');
out := replace(out, ' varful ',' vf ');
out := replace(out, ' vastra ',' v ');
out := replace(out, ' vayla ',' vla ');
out := replace(out, ' vd ',' v d ');
out := replace(out, ' vecindario ',' vecin ');
out := replace(out, ' vei ',' v ');
out := replace(out, ' veien ',' v ');
out := replace(out, ' velika ',' v ');
out := replace(out, ' velike ',' v ');
out := replace(out, ' veliki ',' v ');
out := replace(out, ' veliko ',' v ');
out := replace(out, ' vereda ',' vreda ');
out := replace(out, ' via ',' v ');
out := replace(out, ' viaduct ',' via ');
out := replace(out, ' viaducto ',' vcto ');
out := replace(out, ' viale ',' v le ');
out := replace(out, ' vien bcyo tang ',' vbt ');
out := replace(out, ' view ',' vw ');
out := replace(out, ' virf ',' vf ');
out := replace(out, ' virful ',' vf ');
out := replace(out, ' viviendas ',' vvdas ');
out := replace(out, ' vkhod ',' vkh ');
out := replace(out, ' vliet ',' vlt ');
out := replace(out, ' vuon quoc gia ',' vqg ');
out := replace(out, ' walk ',' wlk ');
out := replace(out, ' way ',' wy ');
out := replace(out, ' west ',' w ');
out := replace(out, ' wielka ',' wlk ');
out := replace(out, ' wielki ',' wlk ');
out := replace(out, ' wielkie ',' wlk ');
out := replace(out, ' wielkopolska ',' wlkp ');
out := replace(out, ' wielkopolski ',' wlkp ');
out := replace(out, ' wielkopolskie ',' wlkp ');
out := replace(out, ' wojewodztwie ',' woj ');
out := replace(out, ' wojewodztwo ',' woj ');
out := replace(out, ' yard ',' yd ');
out := replace(out, ' zgornja ',' zg ');
out := replace(out, ' zgornje ',' zg ');
out := replace(out, ' zgornji ',' zg ');
out := replace(out, ' zhilishchien komplieks ',' zh k ');
out := replace(out, ' zum ',' z ');

out := replace(out, ' and ',' ');
out := replace(out, ' und ',' ');
out := replace(out, ' en ',' ');
out := replace(out, ' et ',' ');
out := replace(out, ' e ',' ');
out := replace(out, ' y ',' ');

out := replace(out, ' the ',' ');
out := replace(out, ' der ',' ');
out := replace(out, ' den ',' ');
out := replace(out, ' die ',' ');
out := replace(out, ' das ',' ');

out := replace(out, ' la ',' ');
out := replace(out, ' le ',' ');
out := replace(out, ' el ',' ');
out := replace(out, ' il ',' ');
out := replace(out, ' ال ',' ');

out := replace(out, 'ae','a');
out := replace(out, 'oe','o');
out := replace(out, 'ue','u');

-- out := regexp_replace(out,E'([^0-9])\\1+',E'\\1','g');
-- out := regexp_replace(out,E'\\mcir\\M','circle','g');

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
  SELECT word_id FROM word WHERE word_token = lookup_token and class is null and type is null into return_word_id;
  IF return_word_id IS NULL THEN
    return_word_id := nextval('seq_word');
    INSERT INTO word VALUES (return_word_id, lookup_token, regexp_replace(lookup_token,E'([^0-9])\\1+',E'\\1','g'), null, null, null, null, 0, null);
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
    INSERT INTO word VALUES (return_word_id, lookup_token, null, null, 'place', 'house', null, 0, null);
  END IF;
  RETURN return_word_id;
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION getorcreate_country(lookup_word TEXT, lookup_country_code varchar(2))
  RETURNS INTEGER
  AS $$
DECLARE
  lookup_token TEXT;
  return_word_id INTEGER;
BEGIN
  lookup_token := ' '||trim(lookup_word);
  SELECT word_id FROM word WHERE word_token = lookup_token and country_code=lookup_country_code into return_word_id;
  IF return_word_id IS NULL THEN
    return_word_id := nextval('seq_word');
    INSERT INTO word VALUES (return_word_id, lookup_token, null, null, null, null, lookup_country_code, 0, null);
  END IF;
  RETURN return_word_id;
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION getorcreate_amenity(lookup_word TEXT, lookup_class text, lookup_type text)
  RETURNS INTEGER
  AS $$
DECLARE
  lookup_token TEXT;
  return_word_id INTEGER;
BEGIN
  lookup_token := ' '||trim(lookup_word);
  SELECT word_id FROM word WHERE word_token = lookup_token and class=lookup_class and type = lookup_type into return_word_id;
  IF return_word_id IS NULL THEN
    return_word_id := nextval('seq_word');
    INSERT INTO word VALUES (return_word_id, lookup_token, null, null, lookup_class, lookup_type, null, 0, null);
  END IF;
  RETURN return_word_id;
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION getorcreate_name_id(lookup_word TEXT, src_word TEXT) 
  RETURNS INTEGER
  AS $$
DECLARE
  lookup_token TEXT;
  nospace_lookup_token TEXT;
  return_word_id INTEGER;
BEGIN
  lookup_token := ' '||trim(lookup_word);
  SELECT word_id FROM word WHERE word_token = lookup_token and class is null and type is null into return_word_id;
  IF return_word_id IS NULL THEN
    return_word_id := nextval('seq_word');
    INSERT INTO word VALUES (return_word_id, lookup_token, regexp_replace(lookup_token,E'([^0-9])\\1+',E'\\1','g'), src_word, null, null, null, 0, null);
    nospace_lookup_token := replace(replace(lookup_token, '-',''), ' ','');
    IF ' '||nospace_lookup_token != lookup_token THEN
      INSERT INTO word VALUES (return_word_id, '-'||nospace_lookup_token, null, src_word, null, null, null, 0, null);
    END IF;
  END IF;
  RETURN return_word_id;
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION getorcreate_name_id(lookup_word TEXT) 
  RETURNS INTEGER
  AS $$
DECLARE
BEGIN
  RETURN getorcreate_name_id(lookup_word, '');
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
  IF array_upper(a, 1) IS NULL THEN
    RETURN b;
  END IF;
  IF array_upper(b, 1) IS NULL THEN
    RETURN a;
  END IF;
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

      w := getorcreate_name_id(s, src[i].value);
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
  FOR nearcountry IN select country_code from location_area where ST_Contains(area, ST_Centroid(place)) and country_code is not null order by rank_address asc limit 1
  LOOP
    RETURN lower(nearcountry.country_code);
  END LOOP;
  -- Are we in a country? Ignore the error condition of being in multiple countries
  FOR nearcountry IN select distinct country_code from country where ST_Contains(country.geometry, ST_Centroid(place)) limit 1
  LOOP
    RETURN lower(nearcountry.country_code);
  END LOOP;
  -- Not in a country - try nearest withing 12 miles of a country
  FOR nearcountry IN select country_code from country where st_distance(country.geometry, ST_Centroid(place)) < 0.5 order by st_distance(country.geometry, place) asc limit 1
  LOOP
    RETURN lower(nearcountry.country_code);
  END LOOP;
  RETURN NULL;
END;
$$
LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION get_country_language_code(search_country_code VARCHAR(2)) RETURNS TEXT
  AS $$
DECLARE
  nearcountry RECORD;
BEGIN
  FOR nearcountry IN select distinct country_default_language_code from country where country_code = search_country_code limit 1
  LOOP
    RETURN lower(nearcountry.country_default_language_code);
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
  isarea BOOLEAN;
BEGIN
  -- 26 = street/highway
  IF rank_search < 26 THEN
    keywords := make_keywords(name);
    IF place_country_code IS NULL THEN
      country_code := lower(get_country_code(geometry));
    ELSE
      country_code := lower(place_country_code);
    END IF;

    isarea := false;
    IF (ST_GeometryType(geometry) in ('ST_Polygon','ST_MultiPolygon') AND ST_IsValid(geometry)) THEN
      INSERT INTO location_area values (place_id,country_code,name,keywords,
        rank_search,rank_address,ST_Centroid(geometry),geometry);
      isarea := true;
    END IF;

    INSERT INTO location_point values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF not isarea THEN
    IF rank_search < 26 THEN
      INSERT INTO location_point_26 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 25 THEN
      INSERT INTO location_point_25 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 24 THEN
      INSERT INTO location_point_24 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 23 THEN
      INSERT INTO location_point_23 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 22 THEN
      INSERT INTO location_point_22 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 21 THEN
      INSERT INTO location_point_21 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 20 THEN
      INSERT INTO location_point_20 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 19 THEN
      INSERT INTO location_point_19 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 18 THEN
      INSERT INTO location_point_18 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 17 THEN
      INSERT INTO location_point_17 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 16 THEN
      INSERT INTO location_point_16 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 15 THEN
      INSERT INTO location_point_15 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 14 THEN
      INSERT INTO location_point_14 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 13 THEN
      INSERT INTO location_point_13 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 12 THEN
      INSERT INTO location_point_12 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 11 THEN
      INSERT INTO location_point_11 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 10 THEN
      INSERT INTO location_point_10 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 9 THEN
      INSERT INTO location_point_9 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 8 THEN
      INSERT INTO location_point_8 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 7 THEN
      INSERT INTO location_point_7 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 6 THEN
      INSERT INTO location_point_6 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 5 THEN
      INSERT INTO location_point_5 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 4 THEN
      INSERT INTO location_point_4 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 3 THEN
      INSERT INTO location_point_3 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 2 THEN
      INSERT INTO location_point_2 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    IF rank_search < 1 THEN
      INSERT INTO location_point_1 values (place_id,country_code,name,keywords,rank_search,rank_address,isarea,ST_Centroid(geometry));
    END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;
    END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;END IF;
    END IF;END IF;END IF;END IF;END IF;END IF;END IF;
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
                -- this should really copy postcodes but it puts a huge burdon on the system for no big benefit
                -- ideally postcodes should move up to the way
                insert into placex values (null,'N',prevnode.osm_id,prevnode.class,prevnode.type,NULL,prevnode.admin_level,housenum,prevnode.street,prevnode.isin,null,prevnode.country_code,prevnode.street_place_id,prevnode.rank_address,prevnode.rank_search,false,ST_Line_Interpolate_Point(linegeo, (housenum::float-orginalstartnumber)/originalnumberrange));
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

CREATE OR REPLACE FUNCTION placex_insert() RETURNS TRIGGER
  AS $$
DECLARE
  i INTEGER;
  postcode TEXT;
  result BOOLEAN;
  country_code VARCHAR(2);
  diameter FLOAT;
BEGIN
  RAISE WARNING '%',NEW;

  -- just block these
  IF NEW.class = 'highway' and NEW.type in ('turning_circle','traffic_signals','mini_roundabout','noexit','crossing') THEN
    RETURN null;
  END IF;
  IF NEW.class in ('landuse','natural') and NEW.name is null THEN
    RETURN null;
  END IF;

  IF ST_IsEmpty(NEW.geometry) OR NOT ST_IsValid(NEW.geometry) OR ST_X(ST_Centroid(NEW.geometry))::text in ('NaN','Infinity','-Infinity') OR ST_Y(ST_Centroid(NEW.geometry))::text in ('NaN','Infinity','-Infinity') THEN  
    NEW.geometry := ST_buffer(NEW.geometry,0);
    IF ST_IsEmpty(NEW.geometry) OR NOT ST_IsValid(NEW.geometry) OR ST_X(ST_Centroid(NEW.geometry))::text in ('NaN','Infinity','-Infinity') OR ST_Y(ST_Centroid(NEW.geometry))::text in ('NaN','Infinity','-Infinity') THEN  
      RAISE WARNING 'Invalid geometary, rejecting: % %', NEW.osm_type, NEW.osm_id;
      RETURN NULL;
    END IF;
  END IF;

  NEW.place_id := nextval('seq_place');
  NEW.indexed := false;
  NEW.country_code := lower(NEW.country_code);

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
      IF NEW.type in ('continent') THEN
        NEW.rank_search := 2;
        NEW.rank_address := NEW.rank_search;
      ELSEIF NEW.type in ('sea') THEN
        NEW.rank_search := 2;
        NEW.rank_address := 0;
      ELSEIF NEW.type in ('country') THEN
        NEW.rank_search := 4;
        NEW.rank_address := NEW.rank_search;
      ELSEIF NEW.type in ('state') THEN
        NEW.rank_search := 8;
        NEW.rank_address := NEW.rank_search;
      ELSEIF NEW.type in ('region') THEN
        NEW.rank_search := 10;
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
      ELSEIF NEW.type in ('village','hamlet','municipality','districy','unincorporated_area','borough','airport') THEN
        NEW.rank_search := 18;
        NEW.rank_address := 17;
      ELSEIF NEW.type in ('moor') THEN
        NEW.rank_search := 17;
        NEW.rank_address := 0;
      ELSEIF NEW.type in ('national_park') THEN
        NEW.rank_search := 18;
        NEW.rank_address := 18;
      ELSEIF NEW.type in ('suburb','croft','subdivision') THEN
        NEW.rank_search := 20;
        NEW.rank_address := NEW.rank_search;
      ELSEIF NEW.type in ('farm','locality','islet') THEN
        NEW.rank_search := 20;
        NEW.rank_address := 0;
      ELSEIF NEW.type in ('hall_of_residence','neighbourhood','housing_estate') THEN
        NEW.rank_search := 22;
        NEW.rank_address := 22;
      ELSEIF NEW.type in ('postcode') THEN

        -- Postcode processing is very country dependant
        IF NEW.country_code IS NULL THEN
          NEW.country_code := get_country_code(NEW.geometry);
        END IF;

        IF NEW.country_code = 'gb' THEN
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

        ELSEIF NEW.country_code = 'de' THEN
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
    ELSEIF NEW.class = 'landuse' THEN
      NEW.rank_search := 22;
      NEW.rank_address := NEW.rank_search;
    ELSEIF NEW.class = 'highway' AND NEW.name is NULL AND 
           NEW.type in ('service','cycleway','path','footway','steps','bridleway','track','byway','motorway_link','primary_link','trunk_link','secondary_link','tertiary_link') THEN
      RETURN NULL;
    ELSEIF NEW.class = 'railway' AND NEW.type in ('rail') THEN
      RETURN NULL;
    ELSEIF NEW.class = 'waterway' AND NEW.name is NULL THEN
      RETURN NULL;
    ELSEIF NEW.class = 'waterway' THEN
      NEW.rank_address := 17;
    ELSEIF NEW.class = 'highway' AND NEW.osm_type != 'N' AND NEW.type in ('service','cycleway','path','footway','steps','bridleway','motorway_link','primary_link','trunk_link','secondary_link','tertiary_link') THEN
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
  -- The following is not needed until doing diff updates, and slows the main index process down

  IF (ST_GeometryType(NEW.geometry) in ('ST_Polygon','ST_MultiPolygon') AND ST_IsValid(NEW.geometry)) THEN
    -- mark items within the geometry for re-indexing
--    RAISE WARNING 'placex poly insert: % % % %',NEW.osm_type,NEW.osm_id,NEW.class,NEW.type;
    update placex set indexed = false where indexed and (ST_Contains(NEW.geometry, placex.geometry) OR ST_Intersects(NEW.geometry, placex.geometry)) AND rank_search > NEW.rank_search;
  ELSE
    -- mark nearby items for re-indexing, where 'nearby' depends on the features rank_search and is a complete guess :(
    diameter := 0;
    -- 16 = city, anything higher than city is effectively ignored (polygon required!)
    IF NEW.rank_search < 16 THEN
      diameter := 0;
    ELSEIF NEW.rank_search < 18 THEN
      diameter := 0.2;
    ELSEIF NEW.rank_search < 20 THEN
      diameter := 0.1;
    ELSEIF NEW.rank_search < 24 THEN
      diameter := 0.02;
    ELSEIF NEW.rank_search < 26 THEN
      diameter := 0.002; -- 100 to 200 meters
    ELSEIF NEW.rank_search < 28 THEN
      diameter := 0.001; -- 50 to 100 meters
    END IF;
    IF diameter > 0 THEN
--      RAISE WARNING 'placex point insert: % % % % %',NEW.osm_type,NEW.osm_id,NEW.class,NEW.type,diameter;
      update placex set indexed = false where indexed and rank_search > NEW.rank_search and ST_DWithin(placex.geometry, NEW.geometry, diameter);
    END IF;

  END IF;

IF NEW.rank_search < 18 THEN
  RAISE WARNING 'placex insert end: % % % %',NEW.osm_type,NEW.osm_id,NEW.class,NEW.type;
END IF;

  RETURN NEW;

END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION placex_update() RETURNS 
TRIGGER
  AS $$
DECLARE

  place_centroid GEOMETRY;
  place_geometry_text TEXT;

  search_maxdistance FLOAT[];
  search_mindistance FLOAT[];
  address_havelevel BOOLEAN[];
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
  address_maxrank INTEGER;
  address_street_word_id INTEGER;
  street_place_id_count INTEGER;
  isin TEXT[];

  bPointCountryCode BOOLEAN;

  name_vector INTEGER[];
  nameaddress_vector INTEGER[];

BEGIN

--RAISE WARNING '%', NEW;

  IF NEW.class = 'place' AND NEW.type = 'postcodearea' THEN
    -- Silently do nothing
    RETURN NEW;
  END IF;

  IF NEW.indexed and NOT OLD.indexed THEN

--RAISE WARNING 'PROCESSING: % %', NEW.place_id, NEW.name;

    search_country_code_conflict := false;

    DELETE FROM search_name WHERE place_id = NEW.place_id;
    DELETE FROM place_addressline WHERE place_id = NEW.place_id;

    -- Adding ourselves to the list simplifies address calculations later
    INSERT INTO place_addressline VALUES (NEW.place_id, NEW.place_id, true, true, 0, NEW.rank_address); 

    -- What level are we searching from
    search_maxrank := NEW.rank_search;

    -- Default max/min distances to look for a location
    FOR i IN 1..28 LOOP
      search_maxdistance[i] := 1;
      search_mindistance[i] := 0.0;
      address_havelevel[i] := false;
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

    -- Speed up searches - just use the centroid of the feature
    -- cheaper but less acurate
    place_centroid := ST_Centroid(NEW.geometry);
    place_geometry_text := 'ST_GeomFromText('''||ST_AsText(NEW.geometry)||''','||ST_SRID(NEW.geometry)||')';

    -- copy the building number to the name
    -- done here rather than on insert to avoid initial indexing
    -- TODO: This might be a silly thing to do
    --IF (NEW.name IS NULL OR array_upper(NEW.name,1) IS NULL) AND NEW.housenumber IS NOT NULL THEN
    --  NEW.name := ARRAY[ROW('ref',NEW.housenumber)::keyvalue];
    --END IF;

    --Temp hack to prevent need to re-index
    IF NEW.name::text = '{"(ref,'||NEW.housenumber||')"}' THEN
      NEW.name := NULL;
    END IF;

    --IF (NEW.name IS NULL OR array_upper(NEW.name,1) IS NULL) AND NEW.type IS NOT NULL THEN
    --  NEW.name := ARRAY[ROW('type',NEW.type)::keyvalue];
    --END IF;

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
      	address_street_word_id := get_name_id(make_standard_name(NEW.street));
        IF address_street_word_id IS NOT NULL THEN
          FOR location IN SELECT place_id,ST_distance(NEW.geometry, search_name.centroid) as distance 
            FROM search_name WHERE search_name.name_vector @> ARRAY[address_street_word_id]
            AND ST_DWithin(NEW.geometry, search_name.centroid, 0.01) and search_rank = 26 
            ORDER BY ST_distance(NEW.geometry, search_name.centroid) ASC limit 1
          LOOP
--RAISE WARNING 'streetname found nearby %',location;
            NEW.street_place_id := location.place_id;
          END LOOP;
        END IF;
        -- Failed, fall back to nearest - don't just stop
        IF NEW.street_place_id IS NULL THEN
--RAISE WARNING 'unable to find streetname nearby % %',NEW.street,address_street_word_id;
--          RETURN null;
        END IF;
      END IF;

--RAISE WARNING 'x4';

      search_diameter := 0.00005;
      WHILE NEW.street_place_id IS NULL AND search_diameter < 0.1 LOOP
--RAISE WARNING '% %', search_diameter,ST_AsText(ST_Centroid(NEW.geometry));
        FOR location IN SELECT place_id FROM placex
          WHERE ST_DWithin(place_centroid, placex.geometry, search_diameter) and rank_search = 26
          ORDER BY ST_distance(NEW.geometry, placex.geometry) ASC limit 1
        LOOP
--RAISE WARNING 'using nearest street,% % %',search_diameter,NEW.street,location;
          NEW.street_place_id := location.place_id;
        END LOOP;
        search_diameter := search_diameter * 2;
      END LOOP;

--RAISE WARNING 'x6 %',NEW.street_place_id;

      -- If we didn't find any road give up, should be VERY rare
      IF NEW.street_place_id IS NULL THEN
        return NEW;
      END IF;

      -- Some unnamed roads won't have been indexed, index now if needed
      select count(*) from place_addressline where place_id = NEW.street_place_id INTO street_place_id_count;
      IF street_place_id_count = 0 THEN
        UPDATE placex set indexed = true where indexed = false and place_id = NEW.street_place_id;
      END IF;

      -- Add the street to the address as zero distance to force to front of list
      INSERT INTO place_addressline VALUES (NEW.place_id, NEW.street_place_id, true, true, 0, 26);
      address_havelevel[26] := true;

      -- Import address details from parent, reclculating distance in process
      INSERT INTO place_addressline select NEW.place_id, x.address_place_id, x.fromarea, x.isaddress, ST_distance(NEW.geometry, placex.geometry), placex.rank_address
        from place_addressline as x join placex on (address_place_id = placex.place_id)
        where x.place_id = NEW.street_place_id and x.address_place_id != NEW.street_place_id;

      -- Get the details of the parent road
      select * from search_name where place_id = NEW.street_place_id INTO location;
      NEW.country_code := location.country_code;

--RAISE WARNING '%', NEW.name;
      -- If there is no name it isn't searchable, don't bother to create a search record
      IF NEW.name is NULL THEN
        return NEW;
      END IF;

      -- Merge address from parent
      nameaddress_vector := array_merge(nameaddress_vector, location.nameaddress_vector);

      -- Performance, it would be more acurate to do all the rest of the import process but it takes too long
      -- Just be happy with inheriting from parent road only
      INSERT INTO search_name values (NEW.place_id, NEW.rank_search, NEW.rank_address, NEW.country_code,
        name_vector, nameaddress_vector, place_centroid);

      return NEW;

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
      ELSEIF NEW.country_code != location.country_code and location.rank_search > 3 THEN
        search_country_code_conflict := true;
      END IF;
  
      -- Add it to the list of search terms
      nameaddress_vector := array_merge(nameaddress_vector, location.keywords::integer[]);
      INSERT INTO place_addressline VALUES (NEW.place_id, location.place_id, true, NOT address_havelevel[location.rank_address], location.distance, location.rank_address); 
      address_havelevel[location.rank_address] := true;

    END LOOP;

    -- try using the isin value to find parent places
    address_maxrank := search_maxrank;
    IF NEW.isin IS NOT NULL THEN
      isin := regexp_split_to_array(NEW.isin, E'[;,]');
      FOR i IN 1..array_upper(isin, 1) LOOP
        address_street_word_id := get_name_id(make_standard_name(isin[i]));
        IF address_street_word_id IS NOT NULL THEN
--RAISE WARNING '  search: %',address_street_word_id;
          FOR location IN SELECT place_id,keywords,rank_search,location_point.country_code,rank_address,
            ST_Distance(place_centroid, search_name.centroid) as distance
            FROM search_name join location_point using (place_id)
            WHERE search_name.name_vector @> ARRAY[address_street_word_id]
            AND rank_search < NEW.rank_search
            ORDER BY ST_distance(NEW.geometry, search_name.centroid) ASC limit 1
          LOOP

            IF NEW.country_code IS NULL THEN
              NEW.country_code := location.country_code;
            ELSEIF NEW.country_code != location.country_code and location.rank_search > 3 THEN
              search_country_code_conflict := true;
            END IF;

--RAISE WARNING '  found: %',location.place_id;
            nameaddress_vector := array_merge(nameaddress_vector, location.keywords::integer[]);
            INSERT INTO place_addressline VALUES (NEW.place_id, location.place_id, false, NOT address_havelevel[location.rank_address], location.distance, location.rank_address);

            IF address_maxrank > location.rank_address THEN
              address_maxrank := location.rank_address;
            END IF;
          END LOOP;
        END IF;
      END LOOP;
      FOR i IN address_maxrank..28 LOOP
        address_havelevel[i] := true;
      END LOOP;
    END IF;

    -- If we have got a consistent country code from the areas and/or isin then we don't care about points (too inacurate)
    bPointCountryCode := NEW.country_code IS NULL;

    IF true THEN
    -- full search using absolute position

    search_diameter := 0;
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
        '  AND ST_Distance('||place_geometry_text||', centroid) > '||search_prevdiameter||
        ' ORDER BY ST_Distance('||place_geometry_text||', centroid) ASC'
      LOOP

        IF bPointCountryCode THEN      
          IF NEW.country_code IS NULL THEN
            NEW.country_code := location.country_code;
          ELSEIF NEW.country_code != location.country_code THEN
            search_country_code_conflict := true;
          END IF;
        END IF;
    
        -- Find search words
--RAISE WARNING 'IF % % % %', location.name, location.distance, location.rank_search, search_maxdistance;
        IF (location.distance < search_maxdistance[location.rank_search]) THEN
--RAISE WARNING '  POINT: % % % %', location.name, location.rank_search, location.place_id, location.distance;
    
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

          INSERT INTO place_addressline VALUES (NEW.place_id, location.place_id, false, NOT address_havelevel[location.rank_address], location.distance, location.rank_address); 
          address_havelevel[location.rank_address] := true;
  
        ELSE
--RAISE WARNING '  Stopped: % % % %', location.rank_search, location.distance, search_maxdistance[location.rank_search], location.name;
          IF search_maxrank > location.rank_search THEN
            search_maxrank := location.rank_search;
          END IF;
        END IF;
    
      END LOOP;
  
--RAISE WARNING '  POINT LOCATIONS, % %', search_maxrank, search_diameter;
  
    END LOOP; --WHILE

    ELSE
      -- Cascading search using nearest parent
    END IF;

    IF search_country_code_conflict OR NEW.country_code IS NULL THEN
      NEW.country_code := get_country_code(place_centroid);
    END IF;

    INSERT INTO search_name values (NEW.place_id, NEW.rank_search, NEW.rank_search, NEW.country_code, 
      name_vector, nameaddress_vector, place_centroid);

  END IF;

  return NEW;
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION placex_delete() RETURNS TRIGGER
  AS $$
DECLARE
BEGIN

--RAISE WARNING 'delete %',OLD.place_id;

  -- mark everything linked to this place for re-indexing
  UPDATE placex set indexed = false from place_addressline where address_place_id = OLD.place_id and placex.place_id = place_addressline.place_id and indexed;

  -- do the actual delete
  DELETE FROM location_area where place_id = OLD.place_id;
  DELETE FROM location_point where place_id = OLD.place_id;
  DELETE FROM location_point_0 where place_id = OLD.place_id;
  DELETE FROM location_point_1 where place_id = OLD.place_id;
  DELETE FROM location_point_2 where place_id = OLD.place_id;
  DELETE FROM location_point_3 where place_id = OLD.place_id;
  DELETE FROM location_point_4 where place_id = OLD.place_id;
  DELETE FROM location_point_5 where place_id = OLD.place_id;
  DELETE FROM location_point_6 where place_id = OLD.place_id;
  DELETE FROM location_point_7 where place_id = OLD.place_id;
  DELETE FROM location_point_8 where place_id = OLD.place_id;
  DELETE FROM location_point_9 where place_id = OLD.place_id;
  DELETE FROM location_point_10 where place_id = OLD.place_id;
  DELETE FROM location_point_11 where place_id = OLD.place_id;
  DELETE FROM location_point_12 where place_id = OLD.place_id;
  DELETE FROM location_point_13 where place_id = OLD.place_id;
  DELETE FROM location_point_14 where place_id = OLD.place_id;
  DELETE FROM location_point_15 where place_id = OLD.place_id;
  DELETE FROM location_point_16 where place_id = OLD.place_id;
  DELETE FROM location_point_17 where place_id = OLD.place_id;
  DELETE FROM location_point_18 where place_id = OLD.place_id;
  DELETE FROM location_point_19 where place_id = OLD.place_id;
  DELETE FROM location_point_20 where place_id = OLD.place_id;
  DELETE FROM location_point_21 where place_id = OLD.place_id;
  DELETE FROM location_point_22 where place_id = OLD.place_id;
  DELETE FROM location_point_23 where place_id = OLD.place_id;
  DELETE FROM location_point_24 where place_id = OLD.place_id;
  DELETE FROM location_point_25 where place_id = OLD.place_id;
  DELETE FROM location_point_26 where place_id = OLD.place_id;
  DELETE FROM search_name where place_id = OLD.place_id;
  DELETE FROM place_addressline where place_id = OLD.place_id;
  DELETE FROM place_addressline where address_place_id = OLD.place_id;

  RETURN OLD;

END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION place_delete() RETURNS TRIGGER
  AS $$
DECLARE
  placeid INTEGER;
BEGIN

--  RAISE WARNING 'delete: % % % %',OLD.osm_type,OLD.osm_id,OLD.class,OLD.type;
  delete from placex where osm_type = OLD.osm_type and osm_id = OLD.osm_id and class = OLD.class;
  RETURN OLD;

END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION place_insert() RETURNS TRIGGER
  AS $$
DECLARE
  existing RECORD;
  existinggeometry GEOMETRY;
  existingplace_id INTEGER;
BEGIN

  -- Just block these - lots and pointless
  IF NEW.class = 'highway' and NEW.type in ('turning_circle','traffic_signals','mini_roundabout','noexit','crossing') THEN
    RETURN null;
  END IF;
  IF NEW.class in ('landuse','natural') and NEW.name is null THEN
    RETURN null;
  END IF;
    
  -- Have we already done this place?
  select * from place where osm_type = NEW.osm_type and osm_id = NEW.osm_id and class = NEW.class and type = NEW.type INTO existing;

  -- Handle a place changing type by removing the old data
  IF existing IS NULL THEN
    DELETE FROM place where osm_type = NEW.osm_type and osm_id = NEW.osm_id and class = NEW.class;
  END IF;

IF existing.rank_search < 26 THEN
  RAISE WARNING 'existing: % % % % %',NEW.osm_type,NEW.osm_id,NEW.class,NEW.type,existing;
END IF;

  -- To paraphrase, if there isn't an existing item, OR if the admin level has changed, OR if it is a major change in geometry
  IF existing IS NULL OR coalesce(existing.admin_level, 100) != coalesce(NEW.admin_level, 100) 
     OR (existing.geometry != NEW.geometry AND ST_Distance(ST_Centroid(existing.geometry),ST_Centroid(NEW.geometry)) > 0.01 AND NOT
     (ST_GeometryType(existing.geometry) in ('ST_Polygon','ST_MultiPolygon') AND ST_GeometryType(NEW.geometry) in ('ST_Polygon','ST_MultiPolygon')))
     THEN

    IF existing IS NOT NULL THEN
      RAISE WARNING 'insert delete % % % %',NEW.osm_type,NEW.osm_id,NEW.class,NEW.type;
      DELETE FROM place where osm_type = NEW.osm_type and osm_id = NEW.osm_id and class = NEW.class and type = NEW.type;
    END IF;   

IF existing.rank_search < 26 THEN
    RAISE WARNING 'insert placex % % % %',NEW.osm_type,NEW.osm_id,NEW.class,NEW.type;
END IF;

    -- No - process it as a new insertion (hopefully of low rank or it will be slow)
    insert into placex values (NEW.place_id
        ,NEW.osm_type
        ,NEW.osm_id
        ,NEW.class
        ,NEW.type
        ,NEW.name
        ,NEW.admin_level
        ,NEW.housenumber
        ,NEW.street
        ,NEW.isin
        ,NEW.postcode
        ,NEW.country_code
        ,NEW.street_place_id
        ,NEW.rank_address,NEW.rank_search
        ,NEW.indexed
        ,NEW.geometry
        );

--    RAISE WARNING 'insert done % % % %',NEW.osm_type,NEW.osm_id,NEW.class,NEW.type;

    RETURN NEW;
  END IF;

  -- Special case for polygon shape changes because they tend to be large and we can be a bit clever about how we handle them
  IF existing IS NOT NULL AND existing.geometry != NEW.geometry THEN

    IF ST_GeometryType(existing.geometry) in ('ST_Polygon','ST_MultiPolygon')
     AND ST_GeometryType(NEW.geometry) in ('ST_Polygon','ST_MultiPolygon') THEN 

      -- Validate the new geometry or it can have weard results
      IF ST_IsEmpty(NEW.geometry) OR NOT ST_IsValid(NEW.geometry) OR ST_X(ST_Centroid(NEW.geometry))::text in ('NaN','Infinity','-Infinity') OR ST_Y(ST_Centroid(NEW.geometry))::text in ('NaN','Infinity','-Infinity') THEN  
        NEW.geometry := ST_buffer(NEW.geometry,0);
        IF ST_IsEmpty(NEW.geometry) OR NOT ST_IsValid(NEW.geometry) OR ST_X(ST_Centroid(NEW.geometry))::text in ('NaN','Infinity','-Infinity') OR ST_Y(ST_Centroid(NEW.geometry))::text in ('NaN','Infinity','-Infinity') THEN  
          RAISE WARNING 'Invalid geometary, rejecting: % %', NEW.osm_type, NEW.osm_id;
          RETURN NULL;
        END IF;
      END IF;

      -- Get the version of the geometry actually used (in placex table)
      select geometry from placex where osm_type = NEW.osm_type and osm_id = NEW.osm_id and class = NEW.class and type = NEW.type into existinggeometry;

      RAISE WARNING 'update polygon1 % % % %',NEW.osm_type,NEW.osm_id,NEW.class,NEW.type;

      -- re-index points that have moved in / out of the polygon, could be done as a single query but postgres gets the index usage wrong
      update placex set indexed = false where indexed and 
        (ST_Contains(NEW.geometry, placex.geometry) OR ST_Intersects(NEW.geometry, placex.geometry))
        AND NOT (ST_Contains(existinggeometry, placex.geometry) OR ST_Intersects(existinggeometry, placex.geometry))
        AND rank_search > NEW.rank_search;

      RAISE WARNING 'update polygon2 % % % %',NEW.osm_type,NEW.osm_id,NEW.class,NEW.type;

      update placex set indexed = false where indexed and 
        (ST_Contains(existinggeometry, placex.geometry) OR ST_Intersects(existinggeometry, placex.geometry))
        AND NOT (ST_Contains(NEW.geometry, placex.geometry) OR ST_Intersects(NEW.geometry, placex.geometry))
        AND rank_search > NEW.rank_search;

       RAISE WARNING 'update place % % % %',NEW.osm_type,NEW.osm_id,NEW.class,NEW.type;

      update place set 
        geometry = NEW.geometry
        where osm_type = NEW.osm_type and osm_id = NEW.osm_id and class = NEW.class and type = NEW.type;

       RAISE WARNING 'update placex % % % %',NEW.osm_type,NEW.osm_id,NEW.class,NEW.type;

      update placex set 
        geometry = NEW.geometry
        where osm_type = NEW.osm_type and osm_id = NEW.osm_id and class = NEW.class and type = NEW.type;
    ELSE

--      RAISE WARNING 'update minor % % % %',NEW.osm_type,NEW.osm_id,NEW.class,NEW.type;

      -- minor geometery change, update the point itself, but don't bother to reindex anything else (performance)
      update place set 
        geometry = NEW.geometry
        where osm_type = NEW.osm_type and osm_id = NEW.osm_id and class = NEW.class and type = NEW.type;

      update placex set 
        geometry = NEW.geometry
        where osm_type = NEW.osm_type and osm_id = NEW.osm_id and class = NEW.class and type = NEW.type;

    END IF;
  END IF;

  IF coalesce(existing.name::text, '') != coalesce(NEW.name::text, '')
    OR coalesce(existing.housenumber, '') != coalesce(NEW.housenumber, '')
    OR coalesce(existing.street, '') != coalesce(NEW.street, '')
--    OR coalesce(existing.isin, '') != coalesce(NEW.isin, '')
    OR coalesce(existing.postcode, '') != coalesce(NEW.postcode, '')
    OR coalesce(existing.country_code, '') != coalesce(NEW.country_code, '') THEN

IF existing.rank_search < 26 THEN
  IF coalesce(existing.name::text, '') != coalesce(NEW.name::text, '') THEN
    RAISE WARNING 'update details, name: % % % %',NEW.osm_type,NEW.osm_id,existing.name::text,NEW.name::text;
  END IF;
  IF coalesce(existing.housenumber, '') != coalesce(NEW.housenumber, '') THEN
    RAISE WARNING 'update details, housenumber: % % % %',NEW.osm_type,NEW.osm_id,existing.housenumber,NEW.housenumber;
  END IF;
  IF coalesce(existing.street, '') != coalesce(NEW.street, '') THEN
    RAISE WARNING 'update details, street: % % % %',NEW.osm_type,NEW.osm_id,existing.street,NEW.street;
  END IF;
--  IF coalesce(existing.isin, '') != coalesce(NEW.isin, '') THEN
--    RAISE WARNING 'update details, isin: % % % %',NEW.osm_type,NEW.osm_id,existing.isin,NEW.isin;
--  END IF;
  IF coalesce(existing.postcode, '') != coalesce(NEW.postcode, '') THEN
    RAISE WARNING 'update details, postcode: % % % %',NEW.osm_type,NEW.osm_id,existing.postcode,NEW.postcode;
  END IF;
  IF coalesce(existing.country_code, '') != coalesce(NEW.country_code, '') THEN
    RAISE WARNING 'update details, country_code: % % % %',NEW.osm_type,NEW.osm_id,existing.country_code,NEW.country_code;
  END IF;
  END IF;

--    RAISE WARNING 'update details % % %',NEW.osm_type,NEW.osm_id,existing.place_id;

    update place set 
      name = NEW.name,
      housenumber  = NEW.housenumber,
      street = NEW.street,
      isin = NEW.isin,
      postcode = NEW.postcode,
      country_code = null,
      street_place_id = null
      where osm_type = NEW.osm_type and osm_id = NEW.osm_id and class = NEW.class and type = NEW.type;

--    RAISE WARNING 'update placex % % %',NEW.osm_type,NEW.osm_id,existing.place_id;

    update placex set 
      name = NEW.name,
      housenumber  = NEW.housenumber,
      street = NEW.street,
      isin = NEW.isin,
      postcode = NEW.postcode,
      country_code = null,
      street_place_id = null
      where osm_type = NEW.osm_type and osm_id = NEW.osm_id and class = NEW.class and type = NEW.type;

--    RAISE WARNING 'update children % % %',NEW.osm_type,NEW.osm_id,existing.place_id;

    select place_id from placex where osm_type = NEW.osm_type and osm_id = NEW.osm_id and class = NEW.class and type = NEW.type into existingplace_id;

    UPDATE placex set indexed = false from place_addressline where address_place_id = existingplace_id and placex.place_id = place_addressline.place_id and indexed;
  END IF;

--  RAISE WARNING 'update end % % %',NEW.osm_type,NEW.osm_id,existing.place_id;

  -- Abort the add
  RETURN NULL;

END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION get_name_by_language(name keyvalue[], languagepref TEXT[]) RETURNS TEXT
  AS $$
DECLARE
  search TEXT[];
  found BOOLEAN;
BEGIN

  IF (array_upper(name, 1) is null) THEN
    return null;
  END IF;

  search := languagepref;

  FOR j IN 1..array_upper(search, 1) LOOP
    FOR k IN 1..array_upper(name, 1) LOOP
      IF (name[k].key = search[j] AND trim(name[k].value) != '') THEN
        return trim(name[k].value);
      END IF;
    END LOOP;
  END LOOP;

  RETURN null;
END;
$$
LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION get_connected_ways(way_ids INTEGER[]) RETURNS SETOF planet_osm_ways
  AS $$
DECLARE
  searchnodes INTEGER[];
  location RECORD;
  j INTEGER;
BEGIN

  searchnodes := '{}';
  FOR j IN 1..array_upper(way_ids, 1) LOOP
    FOR location IN 
      select nodes from planet_osm_ways where id = way_ids[j] LIMIT 1
    LOOP
      searchnodes := searchnodes | location.nodes;
    END LOOP;
  END LOOP;

  RETURN QUERY select * from planet_osm_ways where nodes && searchnodes and NOT ARRAY[id] <@ way_ids;
END;
$$
LANGUAGE plpgsql IMMUTABLE;

CREATE OR REPLACE FUNCTION get_address_postcode(for_place_id BIGINT) RETURNS TEXT
  AS $$
DECLARE
  result TEXT[];
  search TEXT[];
  for_postcode TEXT;
  found INTEGER;
  location RECORD;
BEGIN

  found := 1000;
  search := ARRAY['ref'];
  result := '{}';

  UPDATE placex set indexed = true where indexed = false and place_id = for_place_id;

  select postcode from placex where place_id = for_place_id limit 1 into for_postcode;

  FOR location IN 
    select rank_address,name,distance,length(name::text) as namelength 
      from place_addressline join placex on (address_place_id = placex.place_id) 
      where place_addressline.place_id = for_place_id and rank_address in (5,11)
      order by rank_address desc,rank_search desc,fromarea desc,distance asc,namelength desc
  LOOP
    IF array_upper(search, 1) IS NOT NULL AND array_upper(location.name, 1) IS NOT NULL THEN
      FOR j IN 1..array_upper(search, 1) LOOP
        FOR k IN 1..array_upper(location.name, 1) LOOP
          IF (found > location.rank_address AND location.name[k].key = search[j] AND location.name[k].value != '') AND NOT result && ARRAY[trim(location.name[k].value)] AND (for_postcode IS NULL OR location.name[k].value ilike for_postcode||'%') THEN
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

CREATE OR REPLACE FUNCTION get_address_by_language(for_place_id BIGINT, languagepref TEXT[]) RETURNS TEXT
  AS $$
DECLARE
  result TEXT[];
  search TEXT[];
  found INTEGER;
  location RECORD;
  searchcountrycode varchar(2);
  searchhousenumber TEXT;
BEGIN

  found := 1000;
  search := languagepref;
  result := '{}';

--  UPDATE placex set indexed = false where indexed = true and place_id = for_place_id;
  UPDATE placex set indexed = true where indexed = false and place_id = for_place_id;

  select country_code,housenumber from placex where place_id = for_place_id into searchcountrycode,searchhousenumber;

  FOR location IN 
    select CASE WHEN address_place_id = for_place_id AND rank_address = 0 THEN 100 ELSE rank_address END as rank_address,
      name,distance,length(name::text) as namelength 
      from place_addressline join placex on (address_place_id = placex.place_id) 
      where place_addressline.place_id = for_place_id and (rank_address > 0 OR address_place_id = for_place_id)
      and (placex.country_code IS NULL OR searchcountrycode IS NULL OR placex.country_code = searchcountrycode OR rank_address < 4)
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

  IF searchhousenumber IS NOT NULL AND result[(100 - 28)] IS NULL THEN
    result[(100 - 28)] := searchhousenumber;
  END IF;

  -- No country polygon - add it from the country_code
  IF found > 4 THEN
    select get_name_by_language(country_name.name,languagepref) as name from placex join country_name using (country_code) 
      where place_id = for_place_id limit 1 INTO location;
    IF location IS NOT NULL THEN
      result[(100 - 4)] := trim(location.name);
    END IF;
  END IF;

  RETURN array_to_string(result,', ');
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION get_addressdata_by_language(for_place_id BIGINT, languagepref TEXT[]) RETURNS TEXT[]
  AS $$
DECLARE
  result TEXT[];
  search TEXT[];
  found INTEGER;
  location RECORD;
  searchcountrycode varchar(2);
  searchhousenumber TEXT;
BEGIN

  found := 1000;
  search := languagepref;
  result := '{}';

--  UPDATE placex set indexed = false where indexed = true and place_id = for_place_id;
  UPDATE placex set indexed = true where indexed = false and place_id = for_place_id;

  select country_code,housenumber from placex where place_id = for_place_id into searchcountrycode,searchhousenumber;

  FOR location IN 
    select CASE WHEN address_place_id = for_place_id AND rank_address = 0 THEN 100 ELSE rank_address END as rank_address,
      name,distance,length(name::text) as namelength 
      from place_addressline join placex on (address_place_id = placex.place_id) 
      where place_addressline.place_id = for_place_id and (rank_address > 0 OR address_place_id = for_place_id)
      and (placex.country_code IS NULL OR searchcountrycode IS NULL OR placex.country_code = searchcountrycode OR rank_address < 4)
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

  IF searchhousenumber IS NOT NULL AND result[(100 - 28)] IS NULL THEN
    result[(100 - 28)] := searchhousenumber;
  END IF;

  -- No country polygon - add it from the country_code
  IF found > 4 THEN
    select get_name_by_language(country_name.name,languagepref) as name from placex join country_name using (country_code) 
      where place_id = for_place_id limit 1 INTO location;
    IF location IS NOT NULL THEN
      result[(100 - 4)] := trim(location.name);
    END IF;
  END IF;

  RETURN result;
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION get_place_boundingbox(search_place_id INTEGER) RETURNS place_boundingbox
  AS $$
DECLARE
  result place_boundingbox;
  numfeatures integer;
BEGIN
  select * from place_boundingbox into result where place_id = search_place_id;
  IF result IS NULL THEN
    select count(*) from place_addressline where address_place_id = search_place_id into numfeatures;
    insert into place_boundingbox select place_id,
             ST_Y(ST_PointN(ExteriorRing(ST_Box2D(area)),4)),ST_Y(ST_PointN(ExteriorRing(ST_Box2D(area)),2)),
             ST_X(ST_PointN(ExteriorRing(ST_Box2D(area)),1)),ST_X(ST_PointN(ExteriorRing(ST_Box2D(area)),3)),
             numfeatures, ST_Area(area),
             area from location_area where place_id = search_place_id;
    select * from place_boundingbox into result where place_id = search_place_id;
  END IF;
  IF result IS NULL THEN
-- TODO 0.0001
    insert into place_boundingbox select address_place_id,
             min(ST_Y(ST_Centroid(geometry))) as minlon,max(ST_Y(ST_Centroid(geometry))) as maxlon,
             min(ST_X(ST_Centroid(geometry))) as minlat,max(ST_X(ST_Centroid(geometry))) as maxlat,
             count(*), ST_Area(ST_Buffer(ST_Convexhull(ST_Collect(geometry)),0.0001)) as area,
             ST_Buffer(ST_Convexhull(ST_Collect(geometry)),0.0001) as boundary 
             from place_addressline join placex using (place_id) 
             where address_place_id = search_place_id and (st_length(geometry) < 0.01 or place_id = search_place_id)
             group by address_place_id limit 1;
    select * from place_boundingbox into result where place_id = search_place_id;
  END IF;
  return result;
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION update_place(search_place_id INTEGER) RETURNS BOOLEAN
  AS $$
DECLARE
  result place_boundingbox;
  numfeatures integer;
BEGIN
  update placex set indexed = false where place_id = search_place_id and indexed = true;
  update placex set indexed = true where place_id = search_place_id and indexed = false;
  return true;
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION get_searchrank_label(rank INTEGER) RETURNS TEXT
  AS $$
DECLARE
BEGIN
  IF rank < 2 THEN
    RETURN 'Continent';
  ELSEIF rank < 4 THEN
    RETURN 'Sea';
  ELSEIF rank < 8 THEN
    RETURN 'Country';
  ELSEIF rank < 12 THEN
    RETURN 'State';
  ELSEIF rank < 16 THEN
    RETURN 'County';
  ELSEIF rank = 16 THEN
    RETURN 'City';
  ELSEIF rank = 17 THEN
    RETURN 'Town / Island';
  ELSEIF rank = 18 THEN
    RETURN 'Village / Hamlet';
  ELSEIF rank = 20 THEN
    RETURN 'Suburb';
  ELSEIF rank = 21 THEN
    RETURN 'Postcode Area';
  ELSEIF rank = 22 THEN
    RETURN 'Croft / Farm / Locality / Islet';
  ELSEIF rank = 23 THEN
    RETURN 'Postcode Area';
  ELSEIF rank = 25 THEN
    RETURN 'Postcode Point';
  ELSEIF rank = 26 THEN
    RETURN 'Street / Major Landmark';
  ELSEIF rank = 27 THEN
    RETURN 'Minory Street / Path';
  ELSEIF rank = 28 THEN
    RETURN 'House / Building';
  ELSE
    RETURN 'Other: '||rank;
  END IF;
  
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION get_addressrank_label(rank INTEGER) RETURNS TEXT
  AS $$
DECLARE
BEGIN
  IF rank = 0 THEN
    RETURN 'None';
  ELSEIF rank < 2 THEN
    RETURN 'Continent';
  ELSEIF rank < 4 THEN
    RETURN 'Sea';
  ELSEIF rank = 5 THEN
    RETURN 'Postcode';
  ELSEIF rank < 8 THEN
    RETURN 'Country';
  ELSEIF rank < 12 THEN
    RETURN 'State';
  ELSEIF rank < 16 THEN
    RETURN 'County';
  ELSEIF rank = 16 THEN
    RETURN 'City';
  ELSEIF rank = 17 THEN
    RETURN 'Town / Village / Hamlet';
  ELSEIF rank = 20 THEN
    RETURN 'Suburb';
  ELSEIF rank = 21 THEN
    RETURN 'Postcode Area';
  ELSEIF rank = 22 THEN
    RETURN 'Croft / Farm / Locality / Islet';
  ELSEIF rank = 23 THEN
    RETURN 'Postcode Area';
  ELSEIF rank = 25 THEN
    RETURN 'Postcode Point';
  ELSEIF rank = 26 THEN
    RETURN 'Street / Major Landmark';
  ELSEIF rank = 27 THEN
    RETURN 'Minory Street / Path';
  ELSEIF rank = 28 THEN
    RETURN 'House / Building';
  ELSE
    RETURN 'Other: '||rank;
  END IF;
  
END;
$$
LANGUAGE plpgsql;
