<?php

	require_once('DB.php');
	$oDB =& DB::connect('pgsql://www-data@/gazetteer', false);
	$oDB->setFetchMode(DB_FETCHMODE_ASSOC);
	$oDB->query("SET DateStyle TO 'sql,european'");
	$oDB->query("SET client_encoding TO 'utf-8'");

	$bDebug = false;

	// Log
	if ($_GET['q']) $oDB->query('insert into query_log values (\'now\','.getDBQuoted($_GET['q']).')');

  // Make sure we have a language
  // TODO: process Accept-language
  $aLangPrefOrder = array('name:en','name','ref');
	if (isset($_GET['language']) && $_GET['language'])
	{
		  $aLangPrefOrder = array('name:'.$_GET['language'],'name','ref');
	}
	else
	{
		$_GET['language'] = 'en';
	}

	if (isset($_GET['reverse']))
	{
		// TODO
		exit;
	}

	// Default possition
	$fLat = 51.508;
	$fLon = -0.118;
	$iZoom = 13;


	$sQueryOut = '';
	$aSearchResults = array();

	if (isset($_GET['q']))
	{
		$sQueryOut = $_GET['q'] = trim($_GET['q']);

		// is it just a pair of lat,lon?
		if (preg_match('/(-?[0-9.]+),(-?[0-9.]+)/', $_GET['q'], $aData))
		{
			$fLat = $aData[1];
			$fLon = $aData[2];
		}
		else
		{
			// If we have a view box create the sql
			$sViewboxSmallSQL = $sViewboxLargeSQL = false;
			if ($_GET['viewbox'])
			{
				$aCoOrdinates = explode(',',$_GET['viewbox']);
				$sViewboxSmallSQL = "ST_Contains(ST_SetSRID(ST_MakeBox2D(ST_Point(".(float)$aCoOrdinates[1].",".(float)$aCoOrdinates[0]."),ST_Point(".(float)$aCoOrdinates[3].",".(float)$aCoOrdinates[2].")),4326),geometry)";
				$fHeight = $aCoOrdinates[0]-$aCoOrdinates[2];
				$fWidth = $aCoOrdinates[1]-$aCoOrdinates[3];
				$aCoOrdinates[0] += $fHeight;
				$aCoOrdinates[2] -= $fHeight;
				$aCoOrdinates[1] += $fWidth;
				$aCoOrdinates[3] -= $fWidth;
				$sViewboxLargeSQL = "ST_Contains(ST_SetSRID(ST_MakeBox2D(ST_Point(".(float)$aCoOrdinates[1].",".(float)$aCoOrdinates[0]."),ST_Point(".(float)$aCoOrdinates[3].",".(float)$aCoOrdinates[2].")),4326),geometry)";
			}

			// Split query into phrases
			$aPhrases = explode(',',str_replace(array(' in ',' near '),', ',$_GET['q']));

			// Convert each phrase to standard form
			// Create a list of standard words
			// Get all 'sets' of words
			// Generate a complete list of all 
			$aTokens = array();
			foreach($aPhrases as $iPhrase => $sPhrase)
			{
				$aPhrases[$iPhrase] = $oDB->getRow("select make_standard_name('".pg_escape_string($sPhrase)."') as string");
				$aPhrases[$iPhrase]['words'] = explode(' ',$aPhrases[$iPhrase]['string']);
				$aPhrases[$iPhrase]['wordsets'] = getWordSets($aPhrases[$iPhrase]['words']);
				$aTokens = array_merge($aTokens,getTokensFromSets($aPhrases[$iPhrase]['wordsets']));
			}

			// Check which tokens we have, get the ID numbers			
			$sSQL = 'select * from word where word_token in ('.join(',',array_map("getDBQuoted",$aTokens)).')';
			$aValidTokens = array();
			foreach($oDB->getAll($sSQL) as $aToken)
			{
				if (isset($aValidTokens[$aToken['word_token']]))
				{
					$aValidTokens[$aToken['word_token']][] = $aToken;
				}
				else
				{
					$aValidTokens[$aToken['word_token']] = array($aToken);
				}
			}

			// Try and calculate UK postcodes
			foreach($aTokens as $sToken)
			{
				if (preg_match('/([A-Z][A-Z]?[0-9][0-9A-Z]? [0-9])([A-Z][A-Z])/', strtoupper(trim($sToken)), $aData))
				{
					$aNearPostcodes = $oDB->getAll($sSQL = 'select substring(upper(postcode) from \'^[A-Z][A-Z]?[0-9][0-9A-Z]? [0-9]([A-Z][A-Z])$\'),ST_X(ST_Centroid(geometry)) as lon,ST_Y(ST_Centroid(geometry)) as lat from placex where substring(upper(postcode) from \'^([A-Z][A-Z]?[0-9][0-9A-Z]? [0-9])[A-Z][A-Z]$\') = \''.$aData[1].'\'');
var_Dump($sSQL, $aNearPostcodes);
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
					$fLat = $fTotalLat / $fTotalFac;
					$fLon = $fTotalLon / $fTotalFac;
					$fRadius = 0.1 / $fTotalFac;
					
					$aValidTokens[$sToken] = array(array('lat' => $fLat, 'lon' => $fLon, 'radius' => $fRadius));
	
					// $fTotalFac is a suprisingly good indicator of accuracy
					//$iZoom = 18 + round(log($fTotalFac,32));
					//$iZoom = max(13,min(18,$iZoom));
					//var_Dump(sizeof($aNearPostcodes), $fTotalFac, log($fTotalFac/sizeof($aNearPostcodes),32));
				}
			}

if ($bDebug) var_Dump($aPhrases, $aValidTokens);			

			// Start the search process
			$aResultPlaceIDs = array();

			// Do a quick search for a perfect match
			if (false && !sizeof($aResultPlaceIDs))
			{
				$bValidSearch = true;
				$aNameTerms = array();
				$aAddressTerms = array();
				foreach($aPhrases as $iPhrase => $aPhrase)
				{
					if (!isset($aValidTokens[' '.$aPhrase['wordsets'][0][0]]))
					{
						$bValidSearch = false;
					}
					$iWordID = $aValidTokens[' '.$aPhrase['wordsets'][0][0]]['word_id'];
					if ($iPhrase == 0) $aNameTerms[] = $iWordID;
					$aAddressTerms[] = $iWordID;
				}

				$sSQL = "select place_id as _key,place_id from search_name";
				$sSQL .= " where name_vector @> ARRAY[".join($aNameTerms,",")."]";
				$sSQL .= " and nameaddress_vector @> ARRAY[".join($aAddressTerms,",")."]";
				$sSQL .= " order by search_rank desc limit 10";
				$aResultPlaceIDs = $oDB->getAll($sSQL);
			}

			// No perfect match? generate token sets using all words
			if (!sizeof($aResultPlaceIDs))
			{
				// Build all searches using aValidTokens
							
				/*
					Phrase Wordset
					0      0       (hawkworth road)
					0      1       (hawksworth)(road)
					1      0       (sheffield)
				*/

				// Start with a blank search
				$aSearches = array(
					array('iNamePhrase' => 0, 'aName'=>array(), 'aAddress'=>array(), 'sClass'=>'', 'sType'=>'', 'fLat'=>'', 'fLon'=>'', 'fRadius'=>'')
					);

				// If an entire phrase is not found			
				foreach($aPhrases as $iPhrase => $sPhrase)
				{
					$aNewPhraseSearches = array();

					foreach($aPhrases[$iPhrase]['wordsets'] as $iWordset => $aWordset)
					{
						$aWordsetSearches = $aSearches;

						// Add all words from this wordset
						foreach($aWordset as $sToken)
						{
							$aNewWordsetSearches = array();
							
							foreach($aWordsetSearches as $aCurrentSearch)
							{
	              // If the token is valid
								if (isset($aValidTokens[' '.$sToken]))
								{
									foreach($aValidTokens[' '.$sToken] as $aSearchTerm)
									{
										$aSearch = $aCurrentSearch;
										if ($aSearchTerm['lat'] !== '' && $aSearchTerm['lat'] !== null)
										{
											if ($aSearch['fLat'] === '')
											{
												$aSearch['fLat'] = $aSearchTerm['lat'];
												$aSearch['fLon'] = $aSearchTerm['lon'];
												$aSearch['fRadius'] = $aSearchTerm['radius'];
												$aNewWordsetSearches[] = $aSearch;
											}
										}
										elseif ($aSearchTerm['class'] !== '' && $aSearchTerm['class'] !== null)
										{
											if ($aSearch['sClass'] === '')
											{
												$aSearch['sClass'] = $aSearchTerm['class'];
												$aSearch['sType'] = $aSearchTerm['type'];
												$aNewWordsetSearches[] = $aSearch;
											}
										}
										else
										{
											$aSearch['aAddress'][$aSearchTerm['word_id']] = $aSearchTerm['word_id'];
											if (!sizeof($aSearch['aName']) || $aSearch['iNamePhrase'] == $iPhrase)
											{
												$aSearch['aName'][$aSearchTerm['word_id']] = $aSearchTerm['word_id'];
												$aSearch['iNamePhrase'] = $iPhrase;
											}
											$aNewWordsetSearches[] = $aSearch;
										}
									}
								}
								elseif (false && isset($aValidTokens[$sToken]))
								{
									// TODO
								}
							}
							$aWordsetSearches = $aNewWordsetSearches;
						}						
						$aNewPhraseSearches = array_merge($aNewPhraseSearches, $aNewWordsetSearches);
					}
					$aSearches = $aNewPhraseSearches;
				}
				foreach($aSearches as $aSearch)
				{
if ($bDebug) var_dump($aSearch);
					$aPlaceIDs = array();
					
					if (sizeof($aSearch['aName']) && $aSearch['fLat'] !== '' && $aSearch['sClass'])
					{
						$sSQL = "select place_id from search_name where ";
						$sSQL .= " name_vector @> ARRAY[".join($aSearch['aName'],",")."]";
						$sSQL .= " and nameaddress_vector @> ARRAY[".join($aSearch['aAddress'],",")."]";
						$sSQL .= " and ST_DWithin(centroid, ST_SetSRID(ST_Point(".$aSearch['fLon'].",".$aSearch['fLat']."),4326), ".$aSearch['fRadius'].") ";
        		$sSQL .= " order by ST_Distance(centroid, ST_SetSRID(ST_Point(".$aSearch['fLon'].",".$aSearch['fLat']."),4326)) ASC, search_rank ASC limit 10";
if ($bDebug)         		var_dump($sSQL);
						$aPlaceIDs = $oDB->getCol($sSQL);
						
						if (sizeof($aPlaceIDs))
						{
							$sPlaceIDs = join(',',$aPlaceIDs);
	  					$sSQL = "select place_id from placex where place_id in ($sPlaceIDs) and class='".$aSearch['sClass']."' and type='".$aSearch['sType']."'";
	        		$sSQL .= " order by rank_search asc limit 10";
if ($bDebug) 	        		var_dump($sSQL);
							$aPlaceIDs = $oDB->getCol($sSQL);
							
							if (!sizeof($aPlaceIDs))
							{
									$sSQL = "select l.place_id from placex as l,placex as f where ";
									$sSQL .= "f.place_id in ($sPlaceIDs) and ST_DWithin(l.geometry, f.geometry, 0.1) ";
									$sSQL .= "and l.class='".$aSearch['sClass']."' and l.type='".$aSearch['sType']."' order by ST_Distance(l.geometry, f.geometry) asc, l.rank_search ASC limit 10";
if ($bDebug) 	        		var_dump($sSQL);
									$aPlaceIDs = $oDB->getCol($sSQL);
							}
						}
					}
					elseif (sizeof($aSearch['aName']) && $aSearch['fLat'] !== '')
					{
						$sSQL = "select place_id from search_name where ";
						$sSQL .= " name_vector @> ARRAY[".join($aSearch['aName'],",")."]";
						$sSQL .= " and nameaddress_vector @> ARRAY[".join($aSearch['aAddress'],",")."]";
						$sSQL .= " and ST_DWithin(centroid, ST_SetSRID(ST_Point(".$aSearch['fLon'].",".$aSearch['fLat']."),4326), ".$aSearch['fRadius'].") ";
        		$sSQL .= " order by ST_Distance(centroid, ST_SetSRID(ST_Point(".$aSearch['fLon'].",".$aSearch['fLat']."),4326)) ASC, search_rank ASC limit 10";
if ($bDebug)         		var_dump($sSQL);
						$aPlaceIDs = $oDB->getCol($sSQL);
					}
					elseif (sizeof($aSearch['aName']) && $aSearch['sClass'])
					{
						$sSQL = "select place_id from search_name where ";
						$sSQL .= " name_vector @> ARRAY[".join($aSearch['aName'],",")."]";
						$sSQL .= " and nameaddress_vector @> ARRAY[".join($aSearch['aAddress'],",")."]";
        		$sSQL .= " order by search_rank ASC limit 10";
if ($bDebug)         		var_dump($sSQL);
						$aPlaceIDs = $oDB->getCol($sSQL);

						if (sizeof($aPlaceIDs))
						{
							$sPlaceIDs = join(',',$aPlaceIDs);
	  					$sSQL = "select place_id from placex where place_id in ($sPlaceIDs) and class='".$aSearch['sClass']."' and type='".$aSearch['sType']."'";
	        		$sSQL .= " order by rank_search asc limit 10";
if ($bDebug) 	        		var_dump($sSQL);
							$aPlaceIDs = $oDB->getCol($sSQL);
							
							if (!sizeof($aPlaceIDs))
							{
									$sSQL = "select l.place_id from placex as l,placex as f where ";
									$sSQL .= "f.place_id in ($sPlaceIDs) and ST_DWithin(l.geometry, f.geometry, 0.1) ";
									$sSQL .= "and l.class='".$aSearch['sClass']."' and l.type='".$aSearch['sType']."' order by ST_Distance(l.geometry, f.geometry) asc, l.rank_search asc limit 10";
if ($bDebug) 	        		var_dump($sSQL);
									$aPlaceIDs = $oDB->getCol($sSQL);
							}
						}
					}
					elseif ($aSearch['fLat'] !== '' && $aSearch['sClass'])
					{
						$sSQL = "select place_id from placex where ";
						$sSQL .= " ST_DWithin(geometry, ST_SetSRID(ST_Point(".$aSearch['fLon'].",".$aSearch['fLat']."),4326), ".$aSearch['fRadius'].") ";
						$sSQL .= " and class='".$aSearch['sClass']."' and type='".$aSearch['sType'];
        		$sSQL .= " order by ST_Distance(geometry, ST_SetSRID(ST_Point(".$aSearch['fLon'].",".$aSearch['fLat']."),4326)) ASC, rank_search asc limit 10";
if ($bDebug)         		var_dump($sSQL);
						$aPlaceIDs = $oDB->getCol($sSQL);
					}
					elseif (sizeof($aSearch['aName']))
					{
						$sSQL = "select place_id from search_name where ";
						$sSQL .= " name_vector @> ARRAY[".join($aSearch['aName'],",")."]";
						$sSQL .= " and nameaddress_vector @> ARRAY[".join($aSearch['aAddress'],",")."]";
        		$sSQL .= " order by search_rank ASC limit 10";
if ($bDebug)         		var_dump($sSQL);
						$aPlaceIDs = $oDB->getCol($sSQL);
					}
					elseif ($aSearch['fLat'])
					{
						$sSQL = "select place_id from search_name where search_rank > 25 and ";
						$sSQL .= " ST_DWithin(centroid, ST_SetSRID(ST_Point(".$aSearch['fLon'].",".$aSearch['fLat']."),4326), ".$aSearch['fRadius'].") ";
        		$sSQL .= " order by ST_Distance(centroid, ST_SetSRID(ST_Point(".$aSearch['fLon'].",".$aSearch['fLat']."),4326)) ASC, search_rank desc limit 1";
if ($bDebug)         		var_dump($sSQL);
						$aPlaceIDs = $oDB->getCol($sSQL);
					}
					elseif ($aSearch['sClass'])
					{
						// Not permited - ignore
					}
					else
					{
						// No search terms
					}

					foreach($aPlaceIDs as $iPlaceID)
					{
						$aResultPlaceIDs[$iPlaceID] = $iPlaceID;
					}
				}
			}

//var_dump($aResultPlaceIDs);
			// Still nothing, You have to be kidding!
			if (!sizeof($aResultPlaceIDs))
			{
				echo "No Matches Found";
//				var_dump($aValidTokens);
			}
			else
			{
				$sPlaceIDs = join(',',$aResultPlaceIDs);
				$sOrderSQL = 'CASE ';
				foreach(array_keys($aResultPlaceIDs) as $iOrder => $iPlaceID)
				{
					$sOrderSQL .= 'when place_id = '.$iPlaceID.' then '.$iOrder.' ';
				}
				$sOrderSQL .= ' ELSE 10000000 END ASC';
				$sSQL = "select *,get_address_by_language(place_id, ARRAY[".join(',',array_map("getDBQuoted",$aLangPrefOrder))."]) as langaddress,";
				$sSQL .= "ST_X(ST_Centroid(geometry)) as lon,ST_Y(ST_Centroid(geometry)) as lat ";
				$sSQL .= "from placex where place_id in ($sPlaceIDs) order by rank_search,".$sOrderSQL;
				$aSearchResults = $oDB->getAll($sSQL);
			}
		}
	}
	elseif (isset($_GET['lat']) && isset($_GET['lon']))
	{
	}
	else
	{
		$sURL = 'http://api.hostip.info/get_html.php?ip='.$_SERVER['REMOTE_ADDR'].'&position=true';
		$sData = file_get_contents($sURL);
		if (preg_match('/Latitude: (-?[0-9.]+)\\s+Longitude: (-?[0-9.]+)/im', $sData, $aData))
		{
			$fLat = $aData[1];
			$fLon = $aData[2];
		}
	}
//var_Dump($aSearchResults);
//exit;
$sSearchResult = '';
foreach($aSearchResults as $aResult)
{
$aResult['name'] = $aResult['langaddress'];
switch($aResult['class'].':'.$aResult['type'])
{
case 'place:town':
	$aResult['name'] .= ' (Town)';
	break;
case 'railway:station':
	$aResult['name'] .= ' (Railway Station)';
	$aResult['icon'] = '<img src="http://www.sjjb.co.uk/mapicons/SJJBMapIconsv0.03/recoloured/transport_train_station2.p.20.png">';
	break;
case 'railway:tram_stop':
	$aResult['name'] .= ' (Tram Stop)';
	$aResult['icon'] = '<img src="http://www.sjjb.co.uk/mapicons/SJJBMapIconsv0.03/recoloured/transport_tram_stop.p.20.png">';
	break;
case 'historic:castle':
	$aResult['name'] .= ' (Castle)';
	break;
case 'amenity:place_of_worship':
	$aResult['name'] .= ' (Church)';
	break;
case 'amenity:pub':
	$aResult['icon'] = '<img src="http://www.sjjb.co.uk/mapicons/SJJBMapIconsv0.03/recoloured/food_pub.p.20.png">';
	break;
default:
	$aResult['name'] .= ' ('.$aResult['class'].':'.$aResult['type'].')';
	break;
}
$sSearchResult .= '<hr>';
$sSearchResult .= '<div class="result" onClick="panToLatLon('.$aResult['lat'].', '.$aResult['lon'].');">'.$aResult['icon'].$aResult['name'].' ('.$aResult['lon'].', 
'.$aResult['lat'].') '.$aResult['place_id'].'</div>';
}

$aLanguageOptions = $oDB->getAll("select languagecode,nativename||case when nativename != englishname then ' ('||englishname||')' ELSE '' END as name from languagedata order by nativename");

$sSQL = '';
?>
<html>
<head>
    <title>OpenStreetMap Gazetteer</title>

    <script src="http://www.openlayers.org/api/OpenLayers.js"></script>
    <script src="http://www.openstreetmap.org/openlayers/OpenStreetMap.js"></script>
    <script src="prototype-1.6.0.3.js"></script>

    <style>
    </style>

    <script type="text/javascript">
        
        var map;

	function panToLatLon(lat,lon) {
            	var lonLat = new OpenLayers.LonLat(lon, lat).transform(new OpenLayers.Projection("EPSG:4326"), map.getProjectionObject());
            	map.panTo(lonLat, <?php echo $iZoom ?>);
	}

	function mapEventMove() {
		var proj = new OpenLayers.Projection("EPSG:4326");

		var topleft = map.getLonLatFromViewPortPx(new OpenLayers.Pixel(0,0)).transform(map.getProjectionObject(), proj);
		var bottomright = map.getLonLatFromViewPortPx(new OpenLayers.Pixel(map.size.w,map.size.h)).transform(map.getProjectionObject(), proj);
		$('viewbox').value = topleft.lat+','+topleft.lon+','+bottomright.lat+','+bottomright.lon;

		var center = map.getCenter();
		var lonLat = new OpenLayers.LonLat(center.lon, center.lat);
		var lonLat = lonLat.transform(map.getProjectionObject(), proj);
		new Ajax.Request('index.php?reverse=1&lat='+lonLat.lat+'&lon='+lonLat.lon+'&language='+$('language').value, {
			method:'get',
			onSuccess: function(transport){
			var response = transport.responseText || "no response";
			$('youarehere').innerHTML = 'You are in: '+response;
		}, onFailure: function(){ alert('Something went wrong...') }
		});
		
	}

        function init() {

            map = new OpenLayers.Map ("map", {
                controls:[
		    new OpenLayers.Control.Permalink(),
                    new OpenLayers.Control.Navigation(),
                    new OpenLayers.Control.PanZoomBar(),
		    new OpenLayers.Control.MouseDefaults(),
		    new OpenLayers.Control.LayerSwitcher(),
		    new OpenLayers.Control.MousePosition(),
                    new OpenLayers.Control.Attribution()],
                maxExtent: new OpenLayers.Bounds(-20037508.34,-20037508.34,20037508.34,20037508.34),
                maxResolution: 156543.0399,
                numZoomLevels: 19,
                units: 'm',
                projection: new OpenLayers.Projection("EPSG:900913"),
                displayProjection: new OpenLayers.Projection("EPSG:4326"),
                eventListeners: {
			"moveend": mapEventMove,
		}
            } );


            // Other defined layers are OpenLayers.Layer.OSM.Mapnik, OpenLayers.Layer.OSM.Maplint and OpenLayers.Layer.OSM.CycleMap
 
            map.addLayer(new OpenLayers.Layer.OSM.Mapnik("Mapnik"));
            var lonLat = new OpenLayers.LonLat(<?php echo $fLon ?>, <?php echo $fLat ?>).transform(new OpenLayers.Projection("EPSG:4326"), map.getProjectionObject());

            map.setCenter (lonLat, <?php echo $iZoom ?>);
        }


/*
*/

    </script>
</head>

<body onload="init();">


<div style="width:100%; height:10%;" id="seach">
<div onclick="mapEventMove()" id="youarehere">Click here</div>
<form><input name="q" value="<?php echo $sQueryOut; ?>">
<select id="language" name="language" onchange="mapEventMove();" style="vertical-align: top; padding: 0; margin: 0 0.4em;">
<?php foreach($aLanguageOptions as $aOption)
{
	echo '<option '.($_GET['language'] == $aOption['languagecode']?'selected="selected"':'').'value="'.$aOption['languagecode'].'" lang="'.$aOption['languagecode'].'" xml:lang="'.$aOption['languagecode'].'">'.$aOption['name'].'</option>';
}
?>
</select>
<input id="viewbox" name="viewbox">
<input type="submit">
</form>
<?php echo $sSQL; ?>
</div>
<div style="width:20%; height:90%; float:left; overflow-y:scroll; " id="searchresults"><?php echo $sSearchResult ?></div>
<div style="width:80%; height:90%; float:right; " id="map"></div>

</body>

</html>

<?php

	function postcodeAlphaDifference($s1, $s2)
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

	function wordToSearchTerm($s)
	{
		GLOBAL $aTagSearchWords;
		if (!isset($aTagSearchWords[$s]))
		{
			return pg_escape_string($s);	
		}
		return '('.pg_escape_string($s).'|tag'.$aTagSearchWords[$s].')';
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
	
	function getDBQuoted($s)
	{
		return "'".pg_escape_string($s)."'";
	}
/*
return '';
		$sPoint = $_GET['lon'].",".$_GET['lat'];
		$fDistance = 0.001;
		$sResult = false;
		while (!$aResult)
		{
			$sSQL = "select class,get_address_language(address,'{".$_GET['language']."}') as address from place where ST_DWithin(geometry, ST_SetSRID(ST_POINT($sPoint),4326), $fDistance)";
			$sSQL .= " and search_name != '' order by rank_search asc limit 1";
//echo $sSQL; exit;
			$aResult = $oDB->getRow($sSQL);
			$fDistance = $fDistance * 2;

if ($fDistance > 1)
{
echo "Unable to find location";
//echo $fDistance;
exit;
}
		}
		if ($aResult['class'] != 'place' && $aResult['class'] != 'boundary')
		{
			list($sName, $sResult) = explode(',', $aResult['address'], 2);
		}
		else
		{
			$sResult = $aResult['address'];
		}
		echo $sResult;
		exit;

			// If we have a view box try within that
			if (!sizeof($aResultPlaceIDs) && $_GET['viewbox'])
			{
				$aCoOrdinates = explode(',',$_GET['viewbox']);

	                        $sSQL = "select place_id from search_name join placex using (place_id)";
        	                $sSQL .= " where name_vector @@ ('".join($aNameTerms,"&")."')::tsquery ";
                	        $sSQL .= " and nameaddress_vector @@ ('".join($aAddressTerms,"&")."')::tsquery ";
				$sSQL .= " and ST_Contains(ST_SetSRID(ST_MakeBox2D(ST_Point(".(float)$aCoOrdinates[1].",".(float)$aCoOrdinates[0]."),ST_Point(".(float)$aCoOrdinates[3].",".(float)$aCoOrdinates[2].")),4326),geometry)";
                        	$sSQL .= " order by search_rank desc limit 10";
				$aSearchResults = $oDB->getAll($sSQL);

				if (!sizeof($aSearchResults))
				{
					$fHeight = $aCoOrdinates[0]-$aCoOrdinates[2];
					$fWidth = $aCoOrdinates[1]-$aCoOrdinates[3];
					$aCoOrdinates[0] += $fHeight;
					$aCoOrdinates[2] -= $fHeight;
					$aCoOrdinates[1] += $fWidth;
					$aCoOrdinates[3] -= $fWidth;
		                        $sSQL = "select place_id from search_name join placex using (place_id)";
        		                $sSQL .= " where name_vector @@ ('".join($aNameTerms,"&")."')::tsquery ";
                		        $sSQL .= " and nameaddress_vector @@ ('".join($aAddressTerms,"&")."')::tsquery ";
					$sSQL .= " and ST_Contains(ST_SetSRID(ST_MakeBox2D(ST_Point(".(float)$aCoOrdinates[1].",".(float)$aCoOrdinates[0]."),ST_Point(".(float)$aCoOrdinates[3].",".(float)$aCoOrdinates[2].")),4326),geometry)";
	                        	$sSQL .= " order by search_rank desc limit 10";
					$aSearchResults = $oDB->getAll($sSQL);
				}

//var_dump($sSQL, $aSearchResults);
				foreach($aSearchResults as $aRow)
				{
					$aResultPlaceIDs[$aRow['place_id']] = $aRow['place_id'];
				}
			}

			// Is it an unknown UK postcode?
			if (!sizeof($aResultPlaceIDs) && preg_match('/([A-Z][A-Z]?[0-9][0-9A-Z]? [0-9])([A-Z][A-Z])/', strtoupper($_GET['q']), $aData))
			{
				$aNearPostcodes = $oDB->getAll('select substring(upper(postcode) from \'^[A-Z][A-Z]?[0-9][0-9A-Z]? [0-9]([A-Z][A-Z])$\'),ST_X(ST_Centroid(geometry)) as lon,ST_Y(ST_Centroid(geometry)) as lat from placex where substring(upper(postcode) from \'^([A-Z][A-Z]?[0-9][0-9A-Z]? [0-9])[A-Z][A-Z]$\') = \''.$aData[1].'\'');
				$fTotalLat = 0;
				$fTotalLon = 0;
				$fTotalFac = 0;
				foreach($aNearPostcodes as $aPostcode)
				{
					$iDiff = _postcodeAlphaDifference($aData[2], $aPostcode['substring']);
					if ($iDiff == 0)
						$fFac = 1;
					else
						$fFac = 1/($iDiff*$iDiff);
					
					$fTotalFac += $fFac;
					$fTotalLat += $aPostcode['lat'] * $fFac;
					$fTotalLon += $aPostcode['lon'] * $fFac;
				}
				$fLat = $fTotalLat / $fTotalFac;
				$fLon = $fTotalLon / $fTotalFac;

				// $fTotalFac is a suprisingly good indicator of accuracy
				$iZoom = 18 + round(log($fTotalFac,32));
				$iZoom = max(13,min(18,$iZoom));
//var_Dump(sizeof($aNearPostcodes), $fTotalFac, log($fTotalFac/sizeof($aNearPostcodes),32));
//exit;
			}

*/

