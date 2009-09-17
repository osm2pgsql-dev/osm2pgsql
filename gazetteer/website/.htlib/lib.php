<?php

	function fail($sError, $sUserError = false)
	{
		if (!$sUserError) $sUserError = $sError;
		log('ERROR:'.$sError);
		echo $sUserError;
		exit;
	}

	function getDBQuoted($s)
	{
		return "'".pg_escape_string($s)."'";
	}
	
	function byImportance($a, $b)
	{
		return ($a['importance'] == $b['importance']?0:($a['importance'] < $b['importance']?-1:1));
	}

	function getPrefferedLangauges()
	{
		// If we have been provided the value in $_GET it overrides browser value
		if (isset($_GET['accept-language']) && $_GET['accept-language'])
	  {
			$_SERVER["HTTP_ACCEPT_LANGUAGE"] = $_GET['accept-language'];
		}
		
		$aLanguages = array();
		if (preg_match_all('/([a-z]{1,8}(-[a-z]{1,8})?)\s*(;\s*q\s*=\s*(1|0\.[0-9]+))?/i', $_SERVER['HTTP_ACCEPT_LANGUAGE'], $aLanguagesParse, PREG_SET_ORDER))
		{
			foreach($aLanguagesParse as $aLanguage)
			{
				$aLanguages[$aLanguage[1]] = isset($aLanguage[4])?(float)$aLanguage[4]:1;
			}
			arsort($aLanguages);
		}
		if (!sizeof($aLanguages)) $aLanguages = array(CONST_Language_Default=>1);
		foreach($aLanguages as $sLangauge => $fLangauagePref)
		{
			$aLangPrefOrder[] = 'name:'.$sLangauge;
		}
		$aLangPrefOrder[] = 'name';
		$aLangPrefOrder[] = 'ref';
		$aLangPrefOrder[] = 'type';
		return $aLangPrefOrder;
	}

	function getWordSets($aWords)
	{
		$aResult = array(array(join(' ',$aWords)));
		$sFirstToken = '';
		while(sizeof($aWords) > 1)
		{
			$sWord = array_shift($aWords);
			$sFirstToken .= ($sFirstToken?' ':'').$sWord;
			$aRest = getWordSets($aWords);
			foreach($aRest as $aSet)
			{
				$aResult[] = array_merge(array($sFirstToken),$aSet);
			}
		}
		return $aResult;
	}

	function getTokensFromSets($aSets)
	{
		$aTokens = array();
		foreach($aSets as $aSet)
		{
			foreach($aSet as $sWord)
			{
				$aTokens[' '.$sWord] = ' '.$sWord;
				if (!strpos($sWord,' ')) $aTokens[$sWord] = $sWord;
			}
		}
		return $aTokens;
	}

	/*
		GB Postcode functions
	*/

	function gbPostcodeAlphaDifference($s1, $s2)
	{
		$aValues = array(
			'A'=>0,
			'B'=>1,
			'D'=>2,
			'E'=>3,
			'F'=>4,
			'G'=>5,
			'H'=>6,
			'J'=>7,
			'L'=>8,
			'N'=>9,
			'O'=>10,
			'P'=>11,
			'Q'=>12,
			'R'=>13,
			'S'=>14,
			'T'=>15,
			'U'=>16,
			'W'=>17,
			'X'=>18,
			'Y'=>19,
			'Z'=>20);
		return abs(($aValues[$s1[0]]*21+$aValues[$s1[1]]) - ($aValues[$s2[0]]*21+$aValues[$s2[1]]));
	}
	
	function gbPostcodeCalculate($sPostcode)
	{
		$aNearPostcodes = $oDB->getAll('select substring(upper(postcode) from \'^[A-Z][A-Z]?[0-9][0-9A-Z]? [0-9]([A-Z][A-Z])$\'),ST_X(ST_Centroid(geometry)) as lon,ST_Y(ST_Centroid(geometry)) as lat from placex where substring(upper(postcode) from \'^([A-Z][A-Z]?[0-9][0-9A-Z]? [0-9])[A-Z][A-Z]$\') = \''.$sPostcode.'\'');
		$fTotalLat = 0;
		$fTotalLon = 0;
		$fTotalFac = 0;
		foreach($aNearPostcodes as $aPostcode)
		{
			$iDiff = postcodeAlphaDifference($aData[2], $aPostcode['substring']);
			if ($iDiff == 0)
				$fFac = 1;
			else
				$fFac = 1/($iDiff*$iDiff);
			
			$fTotalFac += $fFac;
			$fTotalLat += $aPostcode['lat'] * $fFac;
			$fTotalLon += $aPostcode['lon'] * $fFac;
		}
		if ($fTotalFac)
		{
			$fLat = $fTotalLat / $fTotalFac;
			$fLon = $fTotalLon / $fTotalFac;
			$fRadius = 0.1 / $fTotalFac;
			return array(array('lat' => $fLat, 'lon' => $fLon, 'radius' => $fRadius));
		}
		return false;

		/*
			$fTotalFac is a suprisingly good indicator of accuracy
			$iZoom = 18 + round(log($fTotalFac,32));
			$iZoom = max(13,min(18,$iZoom));
		*/
	}

	function getClassTypes()
	{
		return array(
 'place:country' => array('label'=>'Country','frequency'=>0,'icon'=>'','defzoom'=>6,),
 'place:state' => array('label'=>'State','frequency'=>0,'icon'=>'','defzoom'=>10,),
 'place:city' => array('label'=>'City','frequency'=>66,'icon'=>'poi_place_city','defzoom'=>12,),
 'boundary:adminitrative' => array('label'=>'Adminitrative','frequency'=>413,'icon'=>'',),
 'place:town' => array('label'=>'Town','frequency'=>1497,'icon'=>'poi_place_town','defzoom'=>14,),
 'place:village' => array('label'=>'Village','frequency'=>11230,'icon'=>'poi_place_village','defzoom'=>15,),
 'place:hamlet' => array('label'=>'Hamlet','frequency'=>7075,'icon'=>'poi_place_village','defzoom'=>15,),
 'place:suburb' => array('label'=>'Suburb','frequency'=>2528,'icon'=>'poi_place_village',),
 'place:locality' => array('label'=>'Locality','frequency'=>4113,'icon'=>'poi_place_village',),
 'landuse:farm' => array('label'=>'Farm','frequency'=>1201,'icon'=>'',),
 'place:farm' => array('label'=>'Farm','frequency'=>1162,'icon'=>'',),

 'highway:motorway_junction' => array('label'=>'Motorway Junction','frequency'=>1126,'icon'=>'',),
 'highway:motorway' => array('label'=>'Motorway','frequency'=>4627,'icon'=>'',),
 'highway:trunk' => array('label'=>'Trunk','frequency'=>23084,'icon'=>'',),
 'highway:primary' => array('label'=>'Primary','frequency'=>32138,'icon'=>'',),
 'highway:secondary' => array('label'=>'Secondary','frequency'=>25807,'icon'=>'',),
 'highway:tertiary' => array('label'=>'Tertiary','frequency'=>29829,'icon'=>'',),
 'highway:residential' => array('label'=>'Residential','frequency'=>361498,'icon'=>'',),
 'highway:unclassified' => array('label'=>'Unclassified','frequency'=>66441,'icon'=>'',),
 'highway:living_street' => array('label'=>'Living Street','frequency'=>710,'icon'=>'',),
 'highway:service' => array('label'=>'Service','frequency'=>9963,'icon'=>'',),
 'highway:track' => array('label'=>'Track','frequency'=>2565,'icon'=>'',),
 'highway:road' => array('label'=>'Road','frequency'=>591,'icon'=>'',),
 'highway:bridleway' => array('label'=>'Bridleway','frequency'=>1556,'icon'=>'',),
 'highway:cycleway' => array('label'=>'Cycleway','frequency'=>2419,'icon'=>'',),
 'highway:pedestrian' => array('label'=>'Pedestrian','frequency'=>2757,'icon'=>'',),
 'highway:footway' => array('label'=>'Footway','frequency'=>15008,'icon'=>'',),
 'highway:steps' => array('label'=>'Steps','frequency'=>444,'icon'=>'',),
 'highway:motorway_link' => array('label'=>'Motorway Link','frequency'=>795,'icon'=>'',),
 'highway:trunk_link' => array('label'=>'Trunk Link','frequency'=>1258,'icon'=>'',),

 'landuse:industrial' => array('label'=>'Industrial','frequency'=>1062,'icon'=>'',),
 'landuse:residential' => array('label'=>'Residential','frequency'=>886,'icon'=>'',),
 'landuse:retail' => array('label'=>'Retail','frequency'=>754,'icon'=>'',),
 'landuse:commercial' => array('label'=>'Commercial','frequency'=>657,'icon'=>'',),

 'place:airport' => array('label'=>'Airport','frequency'=>36,'icon'=>'transport_airport2',),
 'railway:station' => array('label'=>'Station','frequency'=>3431,'icon'=>'transport_train_station2',),
 'amenity:place_of_worship' => array('label'=>'Place Of Worship','frequency'=>9049,'icon'=>'place_of_worship3',),
 'amenity:pub' => array('label'=>'Pub','frequency'=>18969,'icon'=>'food_pub',),
 'amenity:bar' => array('label'=>'Bar','frequency'=>164,'icon'=>'food_bar',),
 'amenity:university' => array('label'=>'University','frequency'=>607,'icon'=>'education_university',),
 'tourism:museum' => array('label'=>'Museum','frequency'=>543,'icon'=>'tourist_museum',),
 'amenity:arts_centre' => array('label'=>'Arts Centre','frequency'=>136,'icon'=>'tourist_art_gallery2',),
 'tourism:zoo' => array('label'=>'Zoo','frequency'=>47,'icon'=>'tourist_zoo',),
 'tourism:theme_park' => array('label'=>'Theme Park','frequency'=>24,'icon'=>'',),
 'tourism:attraction' => array('label'=>'Attraction','frequency'=>1463,'icon'=>'poi_point_of_interest',),
 'leisure:golf_course' => array('label'=>'Golf Course','frequency'=>712,'icon'=>'sport_golf',),
 'historic:castle' => array('label'=>'Castle','frequency'=>316,'icon'=>'tourist_castle',),
 'amenity:hospital' => array('label'=>'Hospital','frequency'=>879,'icon'=>'health_hospital',),
 'amenity:school' => array('label'=>'School','frequency'=>8192,'icon'=>'education_school',),
 'amenity:theatre' => array('label'=>'Theatre','frequency'=>371,'icon'=>'tourist_theatre',),
 'amenity:public_building' => array('label'=>'Public Building','frequency'=>985,'icon'=>'',),
 'amenity:library' => array('label'=>'Library','frequency'=>794,'icon'=>'amenity_library',),
 'amenity:townhall' => array('label'=>'Townhall','frequency'=>242,'icon'=>'',),
 'amenity:community_centre' => array('label'=>'Community Centre','frequency'=>157,'icon'=>'',),
 'amenity:fire_station' => array('label'=>'Fire Station','frequency'=>221,'icon'=>'',),
 'amenity:bank' => array('label'=>'Bank','frequency'=>1248,'icon'=>'money_bank2',),
 'amenity:post_office' => array('label'=>'Post Office','frequency'=>859,'icon'=>'amenity_post_office',),
 'leisure:park' => array('label'=>'Park','frequency'=>2378,'icon'=>'',),
 'amenity:park' => array('label'=>'Park','frequency'=>53,'icon'=>'',),
 'landuse:park' => array('label'=>'Park','frequency'=>50,'icon'=>'',),
 'landuse:recreation_ground' => array('label'=>'Recreation Ground','frequency'=>517,'icon'=>'',),
 'tourism:hotel' => array('label'=>'Hotel','frequency'=>2150,'icon'=>'',),
 'tourism:motel' => array('label'=>'Motel','frequency'=>43,'icon'=>'',),
 'amenity:cinema' => array('label'=>'Cinema','frequency'=>277,'icon'=>'',),
 'tourism:information' => array('label'=>'Information','frequency'=>224,'icon'=>'',),
 'tourism:artwork' => array('label'=>'Artwork','frequency'=>171,'icon'=>'art_gallery2',),
 'historic:archaeological_site' => array('label'=>'Archaeological Site','frequency'=>407,'icon'=>'',),
 'amenity:doctors' => array('label'=>'Doctors','frequency'=>581,'icon'=>'',),
 'leisure:sports_centre' => array('label'=>'Sports Centre','frequency'=>767,'icon'=>'',),
 'leisure:swimming_pool' => array('label'=>'Swimming Pool','frequency'=>24,'icon'=>'',),
 'shop:supermarket' => array('label'=>'Supermarket','frequency'=>2673,'icon'=>'shopping_supermarket',),
 'shop:convenience' => array('label'=>'Convenience','frequency'=>1469,'icon'=>'shopping_convenience',),
 'amenity:restaurant' => array('label'=>'Restaurant','frequency'=>3179,'icon'=>'',),
 'amenity:fast_food' => array('label'=>'Fast Food','frequency'=>2289,'icon'=>'',),
 'amenity:cafe' => array('label'=>'Cafe','frequency'=>1780,'icon'=>'food_cafe',),
 'tourism:guest_house' => array('label'=>'Guest House','frequency'=>223,'icon'=>'',),
 'amenity:pharmacy' => array('label'=>'Pharmacy','frequency'=>733,'icon'=>'',),
 'amenity:fuel' => array('label'=>'Fuel','frequency'=>1308,'icon'=>'',),
 'natural:peak' => array('label'=>'Peak','frequency'=>3212,'icon'=>'poi_peak',),
 'waterway:waterfall' => array('label'=>'Waterfall','frequency'=>24,'icon'=>'',),
 'natural:wood' => array('label'=>'Wood','frequency'=>1845,'icon'=>'',),
 'natural:water' => array('label'=>'Water','frequency'=>1790,'icon'=>'',),
 'landuse:forest' => array('label'=>'Forest','frequency'=>467,'icon'=>'',),
 'landuse:cemetery' => array('label'=>'Cemetery','frequency'=>463,'icon'=>'',),
 'landuse:allotments' => array('label'=>'Allotments','frequency'=>408,'icon'=>'',),
 'landuse:farmyard' => array('label'=>'Farmyard','frequency'=>397,'icon'=>'',),
 'railway:rail' => array('label'=>'Rail','frequency'=>4894,'icon'=>'',),
 'waterway:canal' => array('label'=>'Canal','frequency'=>1723,'icon'=>'',),
 'waterway:river' => array('label'=>'River','frequency'=>4089,'icon'=>'',),
 'waterway:stream' => array('label'=>'Stream','frequency'=>2684,'icon'=>'',),
 'shop:bicycle' => array('label'=>'Bicycle','frequency'=>349,'icon'=>'shopping_bicycle',),
 'shop:clothes' => array('label'=>'Clothes','frequency'=>315,'icon'=>'shopping_clothes',),
 'shop:hairdresser' => array('label'=>'Hairdresser','frequency'=>312,'icon'=>'',),
 'shop:doityourself' => array('label'=>'Doityourself','frequency'=>247,'icon'=>'shopping_diy',),
 'shop:estate_agent' => array('label'=>'Estate Agent','frequency'=>162,'icon'=>'',),
 'shop:car' => array('label'=>'Car','frequency'=>159,'icon'=>'',),
 'shop:garden_centre' => array('label'=>'Garden Centre','frequency'=>143,'icon'=>'shopping_garden_centre',),
 'shop:car_repair' => array('label'=>'Car Repair','frequency'=>141,'icon'=>'',),
 'shop:newsagent' => array('label'=>'Newsagent','frequency'=>132,'icon'=>'',),
 'shop:bakery' => array('label'=>'Bakery','frequency'=>129,'icon'=>'',),
 'shop:furniture' => array('label'=>'Furniture','frequency'=>124,'icon'=>'',),
 'shop:butcher' => array('label'=>'Butcher','frequency'=>105,'icon'=>'',),
 'shop:apparel' => array('label'=>'Apparel','frequency'=>98,'icon'=>'',),
 'shop:electronics' => array('label'=>'Electronics','frequency'=>96,'icon'=>'',),
 'shop:department_store' => array('label'=>'Department Store','frequency'=>86,'icon'=>'',),
 'shop:books' => array('label'=>'Books','frequency'=>85,'icon'=>'',),
 'shop:yes' => array('label'=>'Yes','frequency'=>68,'icon'=>'',),
 'shop:outdoor' => array('label'=>'Outdoor','frequency'=>67,'icon'=>'',),
 'shop:mall' => array('label'=>'Mall','frequency'=>63,'icon'=>'',),
 'shop:florist' => array('label'=>'Florist','frequency'=>61,'icon'=>'',),
 'shop:charity' => array('label'=>'Charity','frequency'=>60,'icon'=>'',),
 'shop:hardware' => array('label'=>'Hardware','frequency'=>59,'icon'=>'',),
 'shop:laundry' => array('label'=>'Laundry','frequency'=>51,'icon'=>'',),
 'shop:shoes' => array('label'=>'Shoes','frequency'=>49,'icon'=>'',),
 'shop:beverages' => array('label'=>'Beverages','frequency'=>48,'icon'=>'',),
 'shop:dry_cleaning' => array('label'=>'Dry Cleaning','frequency'=>46,'icon'=>'',),
 'shop:carpet' => array('label'=>'Carpet','frequency'=>45,'icon'=>'',),
 'shop:computer' => array('label'=>'Computer','frequency'=>44,'icon'=>'',),
 'shop:alcohol' => array('label'=>'Alcohol','frequency'=>44,'icon'=>'',),
 'shop:optician' => array('label'=>'Optician','frequency'=>55,'icon'=>'',),
 'shop:chemist' => array('label'=>'Chemist','frequency'=>42,'icon'=>'',),
 'shop:gallery' => array('label'=>'Gallery','frequency'=>38,'icon'=>'',),
 'shop:mobile_phone' => array('label'=>'Mobile Phone','frequency'=>37,'icon'=>'',),
 'shop:sports' => array('label'=>'Sports','frequency'=>37,'icon'=>'',),
 'shop:jewelry' => array('label'=>'Jewelry','frequency'=>32,'icon'=>'',),
 'shop:pet' => array('label'=>'Pet','frequency'=>29,'icon'=>'',),
 'shop:beauty' => array('label'=>'Beauty','frequency'=>28,'icon'=>'',),
 'shop:stationery' => array('label'=>'Stationery','frequency'=>25,'icon'=>'',),
 'shop:shopping_centre' => array('label'=>'Shopping Centre','frequency'=>25,'icon'=>'',),
 'shop:general' => array('label'=>'General','frequency'=>25,'icon'=>'',),
 'shop:electrical' => array('label'=>'Electrical','frequency'=>25,'icon'=>'',),
 'shop:toys' => array('label'=>'Toys','frequency'=>23,'icon'=>'',),
 'shop:jeweller' => array('label'=>'Jeweller','frequency'=>23,'icon'=>'',),
 'shop:betting' => array('label'=>'Betting','frequency'=>23,'icon'=>'',),
 'shop:household' => array('label'=>'Household','frequency'=>21,'icon'=>'',),
 'shop:travel_agency' => array('label'=>'Travel Agency','frequency'=>21,'icon'=>'',),
 'shop:hifi' => array('label'=>'Hifi','frequency'=>21,'icon'=>'',),
 'amenity:shop' => array('label'=>'Shop','frequency'=>61,'icon'=>'',),

 'place:house' => array('label'=>'House','frequency'=>2086,'icon'=>'','defzoom'=>18,),

//

 'leisure:pitch' => array('label'=>'Pitch','frequency'=>762,'icon'=>'',),
 'highway:unsurfaced' => array('label'=>'Unsurfaced','frequency'=>492,'icon'=>'',),
 'historic:ruins' => array('label'=>'Ruins','frequency'=>483,'icon'=>'',),
 'amenity:college' => array('label'=>'College','frequency'=>473,'icon'=>'',),
 'historic:monument' => array('label'=>'Monument','frequency'=>470,'icon'=>'',),
 'railway:subway' => array('label'=>'Subway','frequency'=>385,'icon'=>'',),
 'historic:memorial' => array('label'=>'Memorial','frequency'=>382,'icon'=>'',),
 'highway:byway' => array('label'=>'Byway','frequency'=>346,'icon'=>'',),
 'leisure:nature_reserve' => array('label'=>'Nature Reserve','frequency'=>342,'icon'=>'',),
 'amenity:police' => array('label'=>'Police','frequency'=>334,'icon'=>'',),
 'leisure:common' => array('label'=>'Common','frequency'=>322,'icon'=>'',),
 'waterway:lock_gate' => array('label'=>'Lock Gate','frequency'=>321,'icon'=>'',),
 'highway:primary_link' => array('label'=>'Primary Link','frequency'=>313,'icon'=>'',),
 'natural:fell' => array('label'=>'Fell','frequency'=>308,'icon'=>'',),
 'amenity:nightclub' => array('label'=>'Nightclub','frequency'=>292,'icon'=>'',),
 'place:island' => array('label'=>'Island','frequency'=>288,'icon'=>'',),
 'highway:path' => array('label'=>'Path','frequency'=>287,'icon'=>'',),
 'leisure:garden' => array('label'=>'Garden','frequency'=>285,'icon'=>'',),
 'landuse:reservoir' => array('label'=>'Reservoir','frequency'=>276,'icon'=>'',),
 'leisure:playground' => array('label'=>'Playground','frequency'=>264,'icon'=>'',),
 'leisure:stadium' => array('label'=>'Stadium','frequency'=>212,'icon'=>'',),
 'historic:mine' => array('label'=>'Mine','frequency'=>193,'icon'=>'',),
 'natural:cliff' => array('label'=>'Cliff','frequency'=>193,'icon'=>'',),
 'tourism:caravan_site' => array('label'=>'Caravan Site','frequency'=>183,'icon'=>'',),
 'amenity:bus_station' => array('label'=>'Bus Station','frequency'=>181,'icon'=>'',),
 'amenity:kindergarten' => array('label'=>'Kindergarten','frequency'=>179,'icon'=>'',),
 'highway:construction' => array('label'=>'Construction','frequency'=>176,'icon'=>'',),
 'amenity:atm' => array('label'=>'Atm','frequency'=>172,'icon'=>'',),
 'amenity:emergency_phone' => array('label'=>'Emergency Phone','frequency'=>164,'icon'=>'',),
 'waterway:lock' => array('label'=>'Lock','frequency'=>146,'icon'=>'',),
 'waterway:riverbank' => array('label'=>'Riverbank','frequency'=>143,'icon'=>'',),
 'natural:coastline' => array('label'=>'Coastline','frequency'=>142,'icon'=>'',),
 'tourism:viewpoint' => array('label'=>'Viewpoint','frequency'=>140,'icon'=>'',),
 'tourism:hostel' => array('label'=>'Hostel','frequency'=>140,'icon'=>'',),
 'tourism:bed_and_breakfast' => array('label'=>'Bed And Breakfast','frequency'=>140,'icon'=>'',),
 'railway:halt' => array('label'=>'Halt','frequency'=>135,'icon'=>'',),
 'railway:platform' => array('label'=>'Platform','frequency'=>134,'icon'=>'',),
 'railway:tram' => array('label'=>'Tram','frequency'=>130,'icon'=>'',),
 'amenity:courthouse' => array('label'=>'Courthouse','frequency'=>129,'icon'=>'',),
 'amenity:recycling' => array('label'=>'Recycling','frequency'=>126,'icon'=>'',),
 'amenity:dentist' => array('label'=>'Dentist','frequency'=>124,'icon'=>'',),
 'natural:beach' => array('label'=>'Beach','frequency'=>121,'icon'=>'',),
 'place:moor' => array('label'=>'Moor','frequency'=>118,'icon'=>'',),
 'amenity:grave_yard' => array('label'=>'Grave Yard','frequency'=>110,'icon'=>'',),
 'waterway:derelict_canal' => array('label'=>'Derelict Canal','frequency'=>109,'icon'=>'',),
 'waterway:drain' => array('label'=>'Drain','frequency'=>108,'icon'=>'',),
 'place:county' => array('label'=>'County','frequency'=>108,'icon'=>'',),
 'landuse:grass' => array('label'=>'Grass','frequency'=>106,'icon'=>'',),
 'landuse:village_green' => array('label'=>'Village Green','frequency'=>106,'icon'=>'',),
 'natural:bay' => array('label'=>'Bay','frequency'=>102,'icon'=>'',),
 'railway:tram_stop' => array('label'=>'Tram Stop','frequency'=>101,'icon'=>'transport_tram_stop',),
 'leisure:marina' => array('label'=>'Marina','frequency'=>98,'icon'=>'',),
 'highway:stile' => array('label'=>'Stile','frequency'=>97,'icon'=>'',),
 'natural:moor' => array('label'=>'Moor','frequency'=>95,'icon'=>'',),
 'railway:light_rail' => array('label'=>'Light Rail','frequency'=>91,'icon'=>'',),
 'railway:narrow_gauge' => array('label'=>'Narrow Gauge','frequency'=>90,'icon'=>'',),
 'natural:land' => array('label'=>'Land','frequency'=>86,'icon'=>'',),
 'amenity:village_hall' => array('label'=>'Village Hall','frequency'=>82,'icon'=>'',),
 'waterway:dock' => array('label'=>'Dock','frequency'=>80,'icon'=>'',),
 'amenity:veterinary' => array('label'=>'Veterinary','frequency'=>79,'icon'=>'',),
 'landuse:brownfield' => array('label'=>'Brownfield','frequency'=>77,'icon'=>'',),
 'leisure:track' => array('label'=>'Track','frequency'=>76,'icon'=>'',),
 'railway:historic_station' => array('label'=>'Historic Station','frequency'=>74,'icon'=>'',),
 'landuse:construction' => array('label'=>'Construction','frequency'=>72,'icon'=>'',),
 'amenity:prison' => array('label'=>'Prison','frequency'=>71,'icon'=>'',),
 'landuse:quarry' => array('label'=>'Quarry','frequency'=>71,'icon'=>'',),
 'amenity:telephone' => array('label'=>'Telephone','frequency'=>70,'icon'=>'',),
 'highway:traffic_signals' => array('label'=>'Traffic Signals','frequency'=>66,'icon'=>'',),
 'natural:heath' => array('label'=>'Heath','frequency'=>62,'icon'=>'',),
 'historic:house' => array('label'=>'House','frequency'=>61,'icon'=>'',),
 'amenity:social_club' => array('label'=>'Social Club','frequency'=>61,'icon'=>'',),
 'landuse:military' => array('label'=>'Military','frequency'=>61,'icon'=>'',),
 'amenity:health_centre' => array('label'=>'Health Centre','frequency'=>59,'icon'=>'',),
 'historic:building' => array('label'=>'Building','frequency'=>58,'icon'=>'',),
 'amenity:clinic' => array('label'=>'Clinic','frequency'=>57,'icon'=>'',),
 'highway:services' => array('label'=>'Services','frequency'=>56,'icon'=>'',),
 'amenity:ferry_terminal' => array('label'=>'Ferry Terminal','frequency'=>55,'icon'=>'',),
 'natural:marsh' => array('label'=>'Marsh','frequency'=>55,'icon'=>'',),
 'natural:hill' => array('label'=>'Hill','frequency'=>54,'icon'=>'',),
 'highway:raceway' => array('label'=>'Raceway','frequency'=>53,'icon'=>'',),
 'amenity:taxi' => array('label'=>'Taxi','frequency'=>47,'icon'=>'',),
 'amenity:take_away' => array('label'=>'Take Away','frequency'=>45,'icon'=>'',),
 'amenity:car_rental' => array('label'=>'Car Rental','frequency'=>44,'icon'=>'',),
 'place:islet' => array('label'=>'Islet','frequency'=>44,'icon'=>'',),
 'amenity:nursery' => array('label'=>'Nursery','frequency'=>44,'icon'=>'',),
 'amenity:nursing_home' => array('label'=>'Nursing Home','frequency'=>43,'icon'=>'',),
 'amenity:toilets' => array('label'=>'Toilets','frequency'=>38,'icon'=>'',),
 'amenity:hall' => array('label'=>'Hall','frequency'=>38,'icon'=>'',),
 'waterway:boatyard' => array('label'=>'Boatyard','frequency'=>36,'icon'=>'',),
 'highway:mini_roundabout' => array('label'=>'Mini Roundabout','frequency'=>35,'icon'=>'',),
 'historic:manor' => array('label'=>'Manor','frequency'=>35,'icon'=>'',),
 'tourism:chalet' => array('label'=>'Chalet','frequency'=>34,'icon'=>'',),
 'amenity:bicycle_parking' => array('label'=>'Bicycle Parking','frequency'=>34,'icon'=>'',),
 'amenity:hotel' => array('label'=>'Hotel','frequency'=>34,'icon'=>'',),
 'waterway:weir' => array('label'=>'Weir','frequency'=>33,'icon'=>'',),
 'natural:wetland' => array('label'=>'Wetland','frequency'=>33,'icon'=>'',),
 'natural:cave_entrance' => array('label'=>'Cave Entrance','frequency'=>32,'icon'=>'',),
 'amenity:crematorium' => array('label'=>'Crematorium','frequency'=>31,'icon'=>'',),
 'tourism:picnic_site' => array('label'=>'Picnic Site','frequency'=>31,'icon'=>'',),
 'landuse:wood' => array('label'=>'Wood','frequency'=>30,'icon'=>'',),
 'landuse:basin' => array('label'=>'Basin','frequency'=>30,'icon'=>'',),
 'natural:tree' => array('label'=>'Tree','frequency'=>30,'icon'=>'',),
 'leisure:slipway' => array('label'=>'Slipway','frequency'=>29,'icon'=>'',),
 'landuse:meadow' => array('label'=>'Meadow','frequency'=>29,'icon'=>'',),
 'landuse:piste' => array('label'=>'Piste','frequency'=>28,'icon'=>'',),
 'amenity:care_home' => array('label'=>'Care Home','frequency'=>28,'icon'=>'',),
 'amenity:club' => array('label'=>'Club','frequency'=>28,'icon'=>'',),
 'amenity:medical_centre' => array('label'=>'Medical Centre','frequency'=>27,'icon'=>'',),
 'historic:roman_road' => array('label'=>'Roman Road','frequency'=>27,'icon'=>'',),
 'historic:fort' => array('label'=>'Fort','frequency'=>26,'icon'=>'',),
 'railway:subway_entrance' => array('label'=>'Subway Entrance','frequency'=>26,'icon'=>'',),
 'historic:yes' => array('label'=>'Yes','frequency'=>25,'icon'=>'',),
 'highway:gate' => array('label'=>'Gate','frequency'=>25,'icon'=>'',),
 'leisure:fishing' => array('label'=>'Fishing','frequency'=>24,'icon'=>'',),
 'historic:museum' => array('label'=>'Museum','frequency'=>24,'icon'=>'',),
 'amenity:car_wash' => array('label'=>'Car Wash','frequency'=>24,'icon'=>'',),
 'railway:level_crossing' => array('label'=>'Level Crossing','frequency'=>23,'icon'=>'',),
 'leisure:bird_hide' => array('label'=>'Bird Hide','frequency'=>23,'icon'=>'',),
 'natural:headland' => array('label'=>'Headland','frequency'=>21,'icon'=>'',),
 'tourism:apartments' => array('label'=>'Apartments','frequency'=>21,'icon'=>'',),
 'amenity:shopping' => array('label'=>'Shopping','frequency'=>21,'icon'=>'',),
 'natural:scrub' => array('label'=>'Scrub','frequency'=>20,'icon'=>'',),
 'natural:fen' => array('label'=>'Fen','frequency'=>20,'icon'=>'',),

 'amenity:parking' => array('label'=>'Parking','frequency'=>3157,'icon'=>'',),
 'highway:bus_stop' => array('label'=>'Bus Stop','frequency'=>35777,'icon'=>'transport_bus_stop2',),
 'place:postcode' => array('label'=>'Postcode','frequency'=>27267,'icon'=>'',),
 'amenity:post_box' => array('label'=>'Post Box','frequency'=>9613,'icon'=>'',),

 'place:houses' => array('label'=>'Houses','frequency'=>85,'icon'=>'',),
 'railway:preserved' => array('label'=>'Preserved','frequency'=>227,'icon'=>'',),
 'waterway:derelict canal' => array('label'=>'Derelict Canal','frequency'=>21,'icon'=>'',),
 'amenity:dead_pub' => array('label'=>'Dead Pub','frequency'=>20,'icon'=>'',),
 'railway:disused_station' => array('label'=>'Disused Station','frequency'=>114,'icon'=>'',),
 'railway:abandoned' => array('label'=>'Abandoned','frequency'=>641,'icon'=>'',),
 'railway:disused' => array('label'=>'Disused','frequency'=>72,'icon'=>'',),
			);		
	}
	
	function getClassTypesWithImportance()
	{
		$aOrders = getClassTypes();
		$i = 1;
		foreach($aOrders as $sID => $a)
		{
			$aOrders[$sID]['importance'] = $i++;
		}
		return $aOrders;
	}
	
	
        function javascript_isarray($xVal)
        {
                if (!is_array($xVal)) return false;
                for($i = 0; $i < sizeof($xVal); $i++)
                {
                        if (!array_key_exists($i, $xVal)) return false;
                }
                return true;
        }

        function javascript_renderData($xVal, $bForceHash = false)
        {
                if (is_array($xVal))
                {
                        $aVals = array();
                        if (javascript_isarray($xVal) && !$bForceHash)
                        {
                                foreach($xVal as $sKey => $xData)
                                {
                                        $aVals[] = javascript_renderData($xData);
                                }
                                return '['.join(',',$aVals).']';
                        }
                        else
                        {
                                foreach($xVal as $sKey => $xData)
                                {
                                        $aVals[] = "'".addslashes($sKey)."'".':'.javascript_renderData($xData);
                                }
                                return '{'.join(',',$aVals).'}';
                        }
                }
                else
                {
                        if (is_bool($xVal)) return $xVal?'true':'false';
                        return "'".str_replace('>','\\>',str_replace(array("\n","\r"),'\\n',str_replace(array("\n\r","\r\n"),'\\n',addslashes($xVal))))."'";
                }
        }

