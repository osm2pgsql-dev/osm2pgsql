<?php

        function getDBQuoted($s)
        {
                return "'".pg_escape_string($s)."'";
        }

	if (false)
	{
		echo "Closed for re-indexing...";
		exit;
	}

	if (get_magic_quotes_gpc())
	{
		echo "Please disable magic quotes in your php.ini configuration";
		exit;
	}

	require_once('DB.php');
//	$oDB =& DB::connect('pgsql://www-data@/gazetteer', false);
	$oDB =& DB::connect('pgsql://www-data@/gazetteerworld', false);
	$oDB->setFetchMode(DB_FETCHMODE_ASSOC);
	$oDB->query("SET DateStyle TO 'sql,european'");
	$oDB->query("SET client_encoding TO 'utf-8'");

	$bDebug = false;

        if (isset($_GET['accept-language']) && $_GET['accept-language'])
        {
                $_SERVER["HTTP_ACCEPT_LANGUAGE"] = $_GET['accept-language'];
        }
        preg_match_all('/([a-z]{1,8}(-[a-z]{1,8})?)\s*(;\s*q\s*=\s*(1|0\.[0-9]+))?/i', $_SERVER['HTTP_ACCEPT_LANGUAGE'], $aLanguagesParse, PREG_SET_ORDER);
        $aLanguages = array();
        foreach($aLanguagesParse as $aLanguage)
        {
                $aLanguages[$aLanguage[1]] = isset($aLanguage[4])?(float)$aLanguage[4]:1;
        }
        arsort($aLanguages);
        if (!sizeof($aLanguages)) $aLanguages = array('en'=>1);
        foreach($aLanguages as $sLangauge => $fLangauagePref)
        {
                $aLangPrefOrder[] = 'name:'.$sLangauge;
        }
        $aLangPrefOrder[] = 'name';
        $aLangPrefOrder[] = 'ref';
        $aLangPrefOrder[] = 'type';

	$sLanguagePrefArraySQL = "ARRAY[".join(',',array_map("getDBQuoted",$aLangPrefOrder))."]";
	if (isset($_GET['osmtype']) && isset($_GET['osmid']) && (int)$_GET['osmid'] && ($_GET['osmtype'] == 'N' || $_GET['osmtype'] == 'W' || $_GET['osmtype'] == 'R'))
	{
		$_GET['place_id'] = $oDB->getOne("select place_id from placex where osm_type = '".$_GET['osmtype']."' and osm_id = ".(int)$_GET['osmid']." order by type = 'postcode' asc");
	}

	if (!isset($_GET['place_id']))
	{
		echo "Please select a place id";
		exit;
	}

	$iPlaceID = (int)$_GET['place_id'];

?>
<html>
  <head>
    <title>OpenStreetMap Gazetteer</title>
    <style>
body {
	margin:0px;
	padding:16px;
  background:#ffffff;
  height: 100%;
  font: normal 12px/15px arial,sans-serif;
}
.line{
  margin-left:20px;
}
.name{
  font-weight: bold;
}
.notused{
  color:#aaa;
}
.noname{
  color:#800;
}
    </style>
  </head>
<body>
<?php

	$sSQL = "UPDATE placex set indexed = true where indexed = false and place_id = $iPlaceID";
	$oDB->query($sSQL);

	$sSQL = "select place_id, osm_type, osm_id, class, type, name, admin_level, housenumber, street, isin, postcode, country_code, ";
	$sSQL .= " street_place_id, rank_address, rank_search, indexed, get_name_by_language(name,$sLanguagePrefArraySQL) as localname, ";
	$sSQL .= " ST_GeometryType(geometry) as geotype";
	$sSQL .= " from placex where place_id = $iPlaceID";
	$aAddressLine = $oDB->getRow($sSQL);
	echo '<h1>'.$aAddressLine['localname'].'</h1>';
	echo '<div class="locationdetails">';
	echo ' <div>Type: <span class="type">'.$aAddressLine['class'].':'.$aAddressLine['type'].'</span></div>';
	echo ' <div>Admin Level: <span class="adminlevel">'.$aAddressLine['admin_level'].'</span></div>';
	echo ' <div>Rank: <span class="rankaddress">'.$aAddressLine['rank_address'].'</span></div>';
	echo ' <div>Coverage: <span class="area">'.($aAddressLine['fromarea']=='t'?'Polygon':'Point').'</span></div>';
	$sOSMType = ($aAddressLine['osm_type'] == 'N'?'node':($aAddressLine['osm_type'] == 'W'?'way':($aAddressLine['osm_type'] == 'R'?'relation':'')));
	if ($sOSMType) echo ' <div>OSM: <span class="osm"><span class="label"></span>'.$sOSMType.' <a href="http://www.openstreetmap.org/browse/'.$sOSMType.'/'.$aAddressLine['osm_id'].'">'.$aAddressLine['osm_id'].'</a></span></div>';
	echo '</div>';

	if ($aAddressLine['rank_address'] == 26)
	{
		$sSQL = "UPDATE placex set indexed = true from placex as srcplace where placex.indexed = false and ST_DWithin(placex.geometry, srcplace.geometry, 0.0005) and srcplace.place_id = $iPlaceID";
		$oDB->query($sSQL);
	}

	// Address
	echo '<h2>Address</h2>';
	echo '<div class=\"address\">';
	$sSQL = "select placex.place_id, osm_type, osm_id, class, type, admin_level, rank_address, fromarea, distance, ";
	$sSQL .= " get_name_by_language(name,$sLanguagePrefArraySQL) as localname, length(name::text) as namelength ";
	$sSQL .= " from place_addressline join placex on (address_place_id = placex.place_id)";
	$sSQL .= " where place_addressline.place_id = $iPlaceID and rank_address > 0 and placex.place_id != $iPlaceID";
	$sSQL .= " order by rank_address desc,rank_search desc,fromarea desc,distance asc,namelength desc";
	$aAddressLines = $oDB->getAll($sSQL);
	$iPrevRank = 1000000;
	foreach($aAddressLines as $aAddressLine)
	{	
		$sOSMType = ($aAddressLine['osm_type'] == 'N'?'node':($aAddressLine['osm_type'] == 'W'?'way':($aAddressLine['osm_type'] == 'R'?'relation':'')));

		echo '<div class="line'.($iPrevRank==$aAddressLine['rank_address']?' notused':'').'">';
		$iPrevRank = $aAddressLine['rank_address'];
		echo '<span class="name">'.(trim($aAddressLine['localname'])?$aAddressLine['localname']:'<span class="noname">No Name</span>').'</span>';
		echo ' (';
		echo '<span class="type"><span class="label">Type: </span>'.$aAddressLine['class'].':'.$aAddressLine['type'].'</span>';
		if ($sOSMType) echo ', <span class="osm"><span class="label"></span>'.$sOSMType.' <a href="http://www.openstreetmap.org/browse/'.$sOSMType.'/'.$aAddressLine['osm_id'].'">'.$aAddressLine['osm_id'].'</a></span>';
		echo ', <span class="adminlevel">'.$aAddressLine['admin_level'].'</span>';
		echo ', <span class="rankaddress">'.$aAddressLine['rank_address'].'</span>';
		echo ', <span class="area">'.($aAddressLine['fromarea']=='t'?'Polygon':'Point').'</span>';
		echo ', <span class="distance">'.$aAddressLine['distance'].'</span>';
		echo ' <a href="details.php?place_id='.$aAddressLine['place_id'].'">GOTO</a>';
		echo ')';
		echo '</div>';
	}
	echo '</div>';

	$sSQL = "select placex.place_id, osm_type, osm_id, class, type, admin_level, rank_address, fromarea, distance, ";
	$sSQL .= " get_name_by_language(name,$sLanguagePrefArraySQL) as localname, length(name::text) as namelength ";
	$sSQL .= " from (select * from place_addressline where address_place_id = $iPlaceID limit 1000) as place_addressline join placex on (place_addressline.place_id = placex.place_id)";
	$sSQL .= " where place_addressline.address_place_id = $iPlaceID and rank_address > 0 and placex.place_id != $iPlaceID";
	$sSQL .= " and type != 'postcode'";
	$sSQL .= " order by rank_address asc,rank_search asc,get_name_by_language(name,$sLanguagePrefArraySQL) limit 100";
	$aAddressLines = $oDB->getAll($sSQL);

	if (sizeof($aAddressLines))
	{
		echo '<h2>Parent Of:</h2>';
		foreach($aAddressLines as $aAddressLine)
		{
			$sOSMType = ($aAddressLine['osm_type'] == 'N'?'node':($aAddressLine['osm_type'] == 'W'?'way':($aAddressLine['osm_type'] == 'R'?'relation':'')));
	
			echo '<div class="line">';
			echo '<span class="name">'.(trim($aAddressLine['localname'])?$aAddressLine['localname']:'<span class="noname">No Name</span>').'</span>';
			echo ' (';
			echo '<span class="type"><span class="label">Type: </span>'.$aAddressLine['class'].':'.$aAddressLine['type'].'</span>';
			if ($sOSMType) echo ', <span class="osm"><span class="label"></span>'.$sOSMType.' <a href="http://www.openstreetmap.org/browse/'.$sOSMType.'/'.$aAddressLine['osm_id'].'">'.$aAddressLine['osm_id'].'</a></span>';
			echo ', <span class="adminlevel">'.$aAddressLine['admin_level'].'</span>';
			echo ', <span class="rankaddress">'.$aAddressLine['rank_address'].'</span>';
			echo ', <span class="area">'.($aAddressLine['fromarea']=='t'?'Polygon':'Point').'</span>';
			echo ', <span class="distance">'.$aAddressLine['distance'].'</span>';
			echo ' <a href="details.php?place_id='.$aAddressLine['place_id'].'">GOTO</a>';
			echo ')';
			echo '</div>';
		}
		echo '</div>';
	}

//	echo '<h2>Other Parts:</h2>';
//	echo '<h2>Linked To:</h2>';
echo '</body></html>';
