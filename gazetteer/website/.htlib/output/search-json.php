<?php
	header ("Content-Type: text/x-json");
	
	$aFilteredPlaces = array();
	foreach($aSearchResults as $iResNum => $aResult)
	{
		$aPlace = array(
								'place_id'=>$aResult['place_id']
								);

		$sOSMType = ($aPointDetails['osm_type'] == 'N'?'node':($aPointDetails['osm_type'] == 'W'?'way':($aPointDetails['osm_type'] == 'R'?'relation':'')));
		if ($sOSMType)
		{
			$aPlace['osm_type'] = $sOSMType;
			$aPlace['osm_id'] = $aPointDetails['osm_id'];
		}
		
		if (isset($aResult['aPointPolygon']) && $bShowPolygons)
		{
			$aPlace['boundingbox'] = array(
				$aResult['aPointPolygon']['minlat'],
				$aResult['aPointPolygon']['maxlat'],
				$aResult['aPointPolygon']['minlon'],
				$aResult['aPointPolygon']['maxlon']);

			$aPlace['polygonpoints']=$aResult['aPolyPoints'];
		}

		if (isset($aResult['zoom']))
		{
			$aPlace['zoom']=$aResult['zoom'];
		}

		$aPlace['lat']=$aResult['lat'];
		$aPlace['lon']=$aResult['lon'];
		$aPlace['display_name']=$aResult['name'];

		$aPlace['class']=$aResult['class'];
		$aPlace['type']=$aResult['type'];
		if ($aResult['icon'])
		{
			$aPlace['icon']=$aResult['icon'];
		}
		$aFilteredPlaces[] = $aPlace;
	}
	
	echo javascript_renderData($aFilteredPlaces);
