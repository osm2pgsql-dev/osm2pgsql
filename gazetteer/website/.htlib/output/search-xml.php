<?php
	header ("content-type: text/xml");
	echo "<";
	echo "?xml version=\"1.0\" encoding=\"UTF-8\" ?";
	echo ">\n";

	echo "<searchresults";
	echo " timestamp='".date(DATE_RFC822)."'";
	echo " querystring='".htmlspecialchars($sQuery, ENT_QUOTES)."'";
	if ($sViewBox) echo " viewbox='".htmlspecialchars($sViewBox, ENT_QUOTES)."'";
	echo " polygon='".($bShowPolygons?'true':'false')."'";
	echo ">\n";

	foreach($aSearchResults as $iResNum => $aResult)
	{
		echo "<place place_id='".$aResult['place_id']."'";
		$sOSMType = ($aResult['osm_type'] == 'N'?'node':($aResult['osm_type'] == 'W'?'way':($aResult['osm_type'] == 'R'?'relation':'')));
		if ($sOSMType)
		{
			echo " osm_type='$sOSMType'";
			echo " osm_id='".$aResult['osm_id']."'";
		}
		
		if (isset($aResult['aPointPolygon']) && $bShowPolygons)
		{
			echo ' boundingbox="';
			echo $aResult['aPointPolygon']['minlat'];
			echo ','.$aResult['aPointPolygon']['maxlat'];
			echo ','.$aResult['aPointPolygon']['minlon'];
			echo ','.$aResult['aPointPolygon']['maxlon'];
			echo '"';

			echo ' polygonpoints="';
			echo javascript_renderData($aResult['aPolyPoints']);
			echo '"';
		}

		if (isset($aResult['zoom']))
		{
			echo " zoom='".$aResult['zoom']."'";
		}

		echo " lat='".$aResult['lat']."'";
		echo " lon='".$aResult['lon']."'";
		echo " display_name='".htmlspecialchars($aResult['name'], ENT_QUOTES)."'";

		echo " class='".htmlspecialchars($aResult['class'])."'";
		echo " type='".htmlspecialchars($aResult['type'])."'";
		if ($aResult['icon'])
		{
			echo " icon='".htmlspecialchars($aResult['icon'], ENT_QUOTES)."'";
		}
		echo "/>";
	}
	
	echo "</searchresults>";
