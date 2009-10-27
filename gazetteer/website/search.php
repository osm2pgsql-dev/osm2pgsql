<?php

	require_once('.htlib/init.php');
	ini_set('memory_limit', '200M');

	// Display defaults
	$fLat = CONST_Default_Lat;
	$fLon = CONST_Default_Lon;
	$iZoom = CONST_Default_Zoom;
	$sOutputFormat = 'html';
	$aSearchResults = array();
	$aExcludePlaceIDs = array();

	// Format for output
	if (isset($_GET['format']) && ($_GET['format'] == 'html' || $_GET['format'] == 'xml' || $_GET['format'] == 'json'))
	{
		$sOutputFormat = $_GET['format'];
	}

	// Show / use polygons
	$bShowPolygons = isset($_GET['polygon']) && $_GET['polygon'];

	// Prefered language	
	$aLangPrefOrder = getPrefferedLangauges();
	$sLanguagePrefArraySQL = "ARRAY[".join(',',array_map("getDBQuoted",$aLangPrefOrder))."]";

	// Is this reporting a bug
	if (isset($_POST['report:description']))
	{
		$sReportQuery = trim($_POST['report:query']);
		$sReportDescription = trim($_POST['report:description']);
		$sReportEmail = trim($_POST['report:email']);
		$oDB->query('insert into report_log values ('.getDBQuoted('now').','.getDBQuoted(($_SERVER["REMOTE_ADDR"])).','.getDBQuoted($sReportQuery).','.getDBQuoted($sReportDescription).','.getDBQuoted($sReportEmail).')');
	}
	
	// Filter by lat/lon not implemented
	if (isset($_GET['lat']) && isset($_GET['lon']))
	{
			// TODO:
			fail('Not yet implemented');
	}
	
	// Search query
	$sQuery = (isset($_GET['q'])?trim($_GET['q']):'');
	if ($sQuery)
	{
		// Log
		$oDB->query('insert into query_log values ('.getDBQuoted('now').','.getDBQuoted($sQuery).','.getDBQuoted(($_SERVER["REMOTE_ADDR"])).')');

		// Is it just a pair of lat,lon?
		if (preg_match('/^(-?[0-9.]+)[, ]+(-?[0-9.]+)$/', $sQuery, $aData))
		{
			$fLat = $aData[1];
			$fLon = $aData[2];
		}
		elseif (preg_match('/^([NS])([0-9]+)( [0-9.]+)?[, ]+([EW])([0-9]+)( [0-9.]+)?$/', $sQuery, $aData))
		{
			// TODO: alternative lat lon format
			fail('Not yet implemented');
		}
		else
		{
			// If we have a view box create the SQL
			// Small is the actual view box, Large is double (on each axis) that 
			$sViewboxSmallSQL = $sViewboxLargeSQL = false;
			if (isset($_GET['viewbox']) && $_GET['viewbox'])
			{
				$aCoOrdinates = explode(',',$_GET['viewbox']);
				$sViewboxSmallSQL = "ST_SetSRID(ST_MakeBox2D(ST_Point(".(float)$aCoOrdinates[1].",".(float)$aCoOrdinates[0]."),ST_Point(".(float)$aCoOrdinates[3].",".(float)$aCoOrdinates[2].")),4326)";
				$fHeight = $aCoOrdinates[0]-$aCoOrdinates[2];
				$fWidth = $aCoOrdinates[1]-$aCoOrdinates[3];
				$aCoOrdinates[0] += $fHeight;
				$aCoOrdinates[2] -= $fHeight;
				$aCoOrdinates[1] += $fWidth;
				$aCoOrdinates[3] -= $fWidth;
				$sViewboxLargeSQL = "ST_SetSRID(ST_MakeBox2D(ST_Point(".(float)$aCoOrdinates[1].",".(float)$aCoOrdinates[0]."),ST_Point(".(float)$aCoOrdinates[3].",".(float)$aCoOrdinates[2].")),4326)";
			}

			//TODO: Most of this code needs moving into the database...

			// Split query into phrases
			// Commas are used to reduce the search space by indicating where phrases split
			$aPhrases = explode(',',str_replace(array(' in ',' near ',' im '),',',$sQuery));

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
				$aTokens = array_merge($aTokens, getTokensFromSets($aPhrases[$iPhrase]['wordsets']));
			}

			// Check which tokens we have, get the ID numbers			
			$sSQL = 'select min(word_id) as word_id,word_token, word, class, type, location,country_code';
			$sSQL .= ' from word where word_token in ('.join(',',array_map("getDBQuoted",$aTokens)).')';
			$sSQL .= ' group by word_token, word, class, type, location,country_code';

			if (CONST_Debug) var_Dump($sSQL);

			$aValidTokens = array();
			$aDatabaseWords = $oDB->getAll($sSQL);
			if (PEAR::IsError($aDatabaseWords))
			{
				var_dump($sSQL, $aDatabaseWords);
				exit;
			}
			foreach($aDatabaseWords as $aToken)
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
			if (CONST_Debug) var_Dump($aPhrases, $aValidTokens);

			// Try and calculate GB postcodes we might be missing
			foreach($aTokens as $sToken)
			{
				if (!isset($aValidTokens[$sToken]) && !isset($aValidTokens[' '.$sToken]) && preg_match('/^([A-Z][A-Z]?[0-9][0-9A-Z]? [0-9])([A-Z][A-Z])$/', strtoupper(trim($sToken)), $aData))
				{
					$aGBPostcodeLocation = gbPostcodeCalculate($aData[1], $oDB);
					if ($aGBPostcodeLocation)
					{
						$aValidTokens[$sToken] = $aGBPostcodeLocation;
					}
				}
			}

			// Any words that have failed completely?
			// TODO: suggestions

			// Start the search process
			$aResultPlaceIDs = array();

			/*
				Calculate all searches using aValidTokens i.e.

				'Wodsworth Road, Sheffield' =>
					
				Phrase Wordset
				0      0       (wodsworth road)
				0      1       (wodsworth)(road)
				1      0       (sheffield)
				
				Score how good the search is so they can be ordered
			*/

			// Start with a blank search
			$aSearches = array(
				array('iSearchRank' => 0, 'iNamePhrase' => 0, 'sCountryCode' => false, 'aName'=>array(), 'aAddress'=>array(), 'sClass'=>'', 'sType'=>'', 'sHouseNumber'=>'', 'fLat'=>'', 'fLon'=>'', 'fRadius'=>'')
			);
//echo "<pre>"; var_Dump($aValidTokens);exit;
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
										$aSearch['iSearchRank']++;
										if ($aSearchTerm['country_code'] !== null && $aSearchTerm['country_code'] != '0')
										{
											$aSearch['sCountryCode'] = strtolower($aSearchTerm['country_code']);
											$aNewWordsetSearches[] = $aSearch;
										}
										elseif ($aSearchTerm['lat'] !== '' && $aSearchTerm['lat'] !== null)
										{
											if ($aSearch['fLat'] === '')
											{
												$aSearch['fLat'] = $aSearchTerm['lat'];
												$aSearch['fLon'] = $aSearchTerm['lon'];
												$aSearch['fRadius'] = $aSearchTerm['radius'];
												$aNewWordsetSearches[] = $aSearch;
											}
										}
										elseif ($aSearchTerm['class'] == 'place' && $aSearchTerm['type'] == 'house')
										{
											if ($aSearch['sHouseNumber'] === '')
											{
												$aSearch['sHouseNumber'] = $sToken;
												$aNewWordsetSearches[] = $aSearch;

												// Fall back to not searching for this item (better than nothing)
												$aSearch = $aCurrentSearch;
												$aSearch['iSearchRank'] += 2;
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
												// If we already have a name there is the option of NOT
												// adding this word to the name, just the address
												if (sizeof($aSearch['aName']))
												{
													$aSearch['iNamePhrase'] = -1;
													$aSearch['iSearchRank'] += 1;
													$aNewWordsetSearches[] = $aSearch;
													$aSearch['iSearchRank'] -= 1;
												}
												$aSearch['aName'][$aSearchTerm['word_id']] = $aSearchTerm['word_id'];
												$aSearch['iNamePhrase'] = $iPhrase;
											}
											$aNewWordsetSearches[] = $aSearch;
										}
									}
								}
								if (isset($aValidTokens[$sToken]))
								{
									// Allow searching for a word - but at extra cost
									foreach($aValidTokens[$sToken] as $aSearchTerm)
									{
										$aSearch = $aCurrentSearch;
										$aSearch['iSearchRank']+=10;
										$aSearch['aAddress'][$aSearchTerm['word_id']] = $aSearchTerm['word_id'];
										if (!sizeof($aSearch['aName']) || $aSearch['iNamePhrase'] == $iPhrase)
										{
											$aSearch['aName'][$aSearchTerm['word_id']] = $aSearchTerm['word_id'];
											$aSearch['iNamePhrase'] = $iPhrase;
										}
										$aNewWordsetSearches[] = $aSearch;
									}
								}
								else
								{
									// Allow skipping a word - but at EXTREAM cost
									//$aSearch = $aCurrentSearch;
									//$aSearch['iSearchRank']+=100;
									//$aNewWordsetSearches[] = $aSearch;
								}
							}
							$aWordsetSearches = $aNewWordsetSearches;
						}						
						$aNewPhraseSearches = array_merge($aNewPhraseSearches, $aNewWordsetSearches);
					}

					// Re-group the searches by their score
					$aGroupedSearches = array();
					foreach($aNewPhraseSearches as $aSearch)
					{
						if ($aSearch['iSearchRank'] < 200)
						{
							if (!isset($aGroupedSearches[$aSearch['iSearchRank']])) $aGroupedSearches[$aSearch['iSearchRank']] = array();
							$aGroupedSearches[$aSearch['iSearchRank']][] = $aSearch;
						}
					}
					ksort($aGroupedSearches);

					$iSearchCount = 0;
					$aSearches = array();
					foreach($aGroupedSearches as $iScore => $aNewSearches)
					{
						$iSearchCount += sizeof($aSearches);
						$aSearches = array_merge($aSearches, $aNewSearches);
						if ($iSearchCount > 10) break;
					}
				}
				
				if (CONST_Debug) var_Dump($aGroupedSearches);
				if (CONST_Debug) _debugDumpGroupedSearches($aGroupedSearches, $aValidTokens);

				$iLoop = 0;
				foreach($aGroupedSearches as $aSearches)
				{
					if ($iLoop++ > 4) break;

					foreach($aSearches as $aSearch)
					{
						$aPlaceIDs = array();
						
						// First we need a position, either aName or fLat or both
						$aTerms = array();
						$aOrder = array();
						if (sizeof($aSearch['aName'])) $aTerms[] = "name_vector @> ARRAY[".join($aSearch['aName'],",")."]";
						if (sizeof($aSearch['aAddress'])) $aTerms[] = "nameaddress_vector @> ARRAY[".join($aSearch['aAddress'],",")."]";
						if ($aSearch['sCountryCode']) $aTerms[] = "country_code = '".pg_escape_string($aSearch['sCountryCode'])."'";
						if ($aSearch['sHouseNumber']) $aTerms[] = "address_rank = 26";
						if ($aSearch['fLon'])
						{
							$aTerms[] = "ST_DWithin(centroid, ST_SetSRID(ST_Point(".$aSearch['fLon'].",".$aSearch['fLat']."),4326), ".$aSearch['fRadius'].")";
							$aOrder[] = "ST_Distance(centroid, ST_SetSRID(ST_Point(".$aSearch['fLon'].",".$aSearch['fLat']."),4326)) ASC";
						}
						$aOrder[] = "search_rank ASC";
						
						if (sizeof($aTerms))
						{
							$sSQL = "select place_id from search_name";
							$sSQL .= " where ".join(' and ',$aTerms);
							$sSQL .= " order by ".join(', ',$aOrder);
							$sSQL .= " limit 10";
							if (CONST_Debug) var_dump($sSQL);
							$aPlaceIDs = $oDB->getCol($sSQL);
						}

						if ($aSearch['sHouseNumber'] && sizeof($aPlaceIDs))
						{
							$sPlaceIDs = join(',',$aPlaceIDs);

							// Make sure everything nearby is indexed (if we pre-indexed houses this wouldn't be needed!)
							$sSQL = "update placex set indexed = true from placex as f where placex.indexed = false";
							$sSQL .= " and f.place_id in (".$sPlaceIDs.") and ST_DWithin(placex.geometry, f.geometry, 0.001)";
							$sSQL .= " and placex.housenumber='".$aSearch['sHouseNumber']."'";
							$sSQL .= " and placex.class='place' and placex.type='house'";
							if (CONST_Debug) var_dump($sSQL);
							$oDB->query($sSQL);
							
							// Now they are indexed look for a house attached to a street we found
							$sSQL = "select place_id from placex where street_place_id in (".$sPlaceIDs.") and housenumber = '".$aSearch['sHouseNumber']."'";
							if (CONST_Debug) var_dump($sSQL);
							$aPlaceIDs = $oDB->getCol($sSQL);
						}
						
						if ($aSearch['sClass'] && sizeof($aPlaceIDs))
						{
							$sPlaceIDs = join(',',$aPlaceIDs);
							
							// If they were searching for a named class (i.e. 'Kings Head pub') then we might have an extra match
							$sSQL = "select place_id from placex where place_id in ($sPlaceIDs) and class='".$aSearch['sClass']."' and type='".$aSearch['sType']."'";
							$sSQL .= " order by rank_search asc limit 10";
							if (CONST_Debug) var_dump($sSQL);
							$aPlaceIDs = $oDB->getCol($sSQL);
								
							// No exact match, look for one nearby
							if (!sizeof($aPlaceIDs))
							{
								$fRange = 0.01;
								$sSQL = "select l.place_id from placex as l,placex as f where ";
								$sSQL .= "f.place_id in ($sPlaceIDs) and ST_DWithin(l.geometry, f.geometry, $fRange) ";
								$sSQL .= "and l.class='".$aSearch['sClass']."' and l.type='".$aSearch['sType']."' ";
								$sSQL .= " order by ST_Distance(l.geometry, f.geometry) asc, l.rank_search ASC limit 10";
								if (CONST_Debug) var_dump($sSQL);
								$aPlaceIDs = $oDB->getCol($sSQL);
							}
						}
						
						if (PEAR::IsError($aPlaceIDs))
						{
							var_dump($sSQL, $aPlaceIDs);					
							exit;
						}

						foreach($aPlaceIDs as $iPlaceID)
						{
							$aResultPlaceIDs[$iPlaceID] = $iPlaceID;
						}
					}
					//exit;
					if (sizeof($aResultPlaceIDs)) break;
				}
//exit;
				// Did we find anything?	
				if (sizeof($aResultPlaceIDs))
				{
//var_Dump($aResultPlaceIDs);exit;
					// Get the details for display (is this a redundant extra step?)
					$sPlaceIDs = join(',',$aResultPlaceIDs);
					$sOrderSQL = 'CASE ';
					foreach(array_keys($aResultPlaceIDs) as $iOrder => $iPlaceID)
					{
						$sOrderSQL .= 'when min(place_id) = '.$iPlaceID.' then '.$iOrder.' ';
					}
					$sOrderSQL .= ' ELSE 10000000 END ASC';
					$sSQL = "select osm_type,osm_id,class,type,rank_search,rank_address,min(place_id) as place_id,";
					$sSQL .= "get_address_by_language(place_id, $sLanguagePrefArraySQL) as langaddress,";
					$sSQL .= "get_name_by_language(name, $sLanguagePrefArraySQL) as placename,";
					$sSQL .= "get_name_by_language(name, ARRAY['ref']) as ref,";
					$sSQL .= "avg(ST_X(ST_Centroid(geometry))) as lon,avg(ST_Y(ST_Centroid(geometry))) as lat ";
					$sSQL .= "from placex where place_id in ($sPlaceIDs) ";
					$sSQL .= "group by osm_type,osm_id,class,type,rank_search,rank_address";
					$sSQL .= ",get_address_by_language(place_id, $sLanguagePrefArraySQL) ";
					$sSQL .= ",get_name_by_language(name, $sLanguagePrefArraySQL) ";
					$sSQL .= ",get_name_by_language(name, ARRAY['ref']) ";
					$sSQL .= "order by rank_search,rank_address,".$sOrderSQL;
					if (CONST_Debug) var_dump($sSQL);
					$aSearchResults = $oDB->getAll($sSQL);
//var_dump($sSQL,$aSearchResults);exit;

					if (PEAR::IsError($aSearchResults))
					{
						var_dump($sSQL, $aSearchResults);					
						exit;
					}
				}
			}
	}
	
	$sSearchResult = '';
	if (!sizeof($aSearchResults) && isset($_GET['q']) && $_GET['q'])
	{
		$sSearchResult = 'No Results Found';
	}
	
	$aClassType = getClassTypesWithImportance();

	foreach($aSearchResults as $iResNum => $aResult)
	{
		if (CONST_Search_AreaPolygons)
		{
		        // Get the bounding box and outline polygon
	        	$sSQL = "select place_id,numfeatures,area,outline,";
			$sSQL .= "ST_Y(ST_PointN(ExteriorRing(ST_Box2D(outline)),4)) as minlat,ST_Y(ST_PointN(ExteriorRing(ST_Box2D(outline)),2)) as maxlat,";
			$sSQL .= "ST_X(ST_PointN(ExteriorRing(ST_Box2D(outline)),1)) as minlon,ST_X(ST_PointN(ExteriorRing(ST_Box2D(outline)),3)) as maxlon,";
			$sSQL .= "ST_AsText(outline) as outlinestring from get_place_boundingbox(".$aResult['place_id'].")";
			$aPointPolygon = $oDB->getRow($sSQL);
			if (PEAR::IsError($aPointPolygon))
			{
				var_dump($sSQL, $aPointPolygon);
				exit;
			}
		        if (preg_match('#POLYGON\\(\\(([- 0-9.,]+)#',$aPointPolygon['outlinestring'],$aMatch))
        		{
        	        	preg_match_all('/(-?[0-9.]+) (-?[0-9.]+)/',$aMatch[1],$aPolyPoints,PREG_SET_ORDER);
		        }
        		elseif (preg_match('#POINT\\((-?[0-9.]+) (-?[0-9.]+)\\)#',$aPointPolygon['outlinestring'],$aMatch))
		        {
	        	        $fRadius = 0.01;
                		$iSteps = ($fRadius * 40000)^2;
	        	        $fStepSize = (2*pi())/$iSteps;
        		        $aPolyPoints = array();
	                	for($f = 0; $f < 2*pi(); $f += $fStepSize)
	                	{
	     		                $aPolyPoints[] = array('',$aMatch[1]+($fRadius*sin($f)),$aMatch[2]+($fRadius*cos($f)));
        		        }
		                $aPointPolygon['minlat'] = $aPointPolygon['minlat'] - $fRadius;
        	        	$aPointPolygon['maxlat'] = $aPointPolygon['maxlat'] + $fRadius;
                		$aPointPolygon['minlon'] = $aPointPolygon['minlon'] - $fRadius;
		                $aPointPolygon['maxlon'] = $aPointPolygon['maxlon'] + $fRadius;
			}
			if ($bShowPolygons)
			{
				$aResult['aPolyPoints'] = array();
				foreach($aPolyPoints as $aPoint)
				{
					$aResult['aPolyPoints'][] = array($aPoint[1], $aPoint[2]);
				}
			}
			$aResult['aPointPolygon'] = $aPointPolygon;

			$fMaxDiameter = max($aPointPolygon['maxlon'] - $aPointPolygon['minlon'], $aPointPolygon['maxlat'] - $aPointPolygon['minlat']);
			if ($fMaxDiameter < 0.0001)
			{
				$aResult['zoom'] = 17;
				if (isset($aClassType[$aResult['class'].':'.$aResult['type']]['defzoom']) 
					&& $aClassType[$aResult['class'].':'.$aResult['type']]['defzoom'])
				{
					$aResult['zoom'] = $aClassType[$aResult['class'].':'.$aResult['type']]['defzoom'];
				}
			}
			elseif ($fMaxDiameter < 0.005) $aResult['zoom'] = 18;
			elseif ($fMaxDiameter < 0.01) $aResult['zoom'] = 17;
			elseif ($fMaxDiameter < 0.02) $aResult['zoom'] = 16;
			elseif ($fMaxDiameter < 0.04) $aResult['zoom'] = 15;
			elseif ($fMaxDiameter < 0.08) $aResult['zoom'] = 14;
			elseif ($fMaxDiameter < 0.16) $aResult['zoom'] = 13;
			elseif ($fMaxDiameter < 0.32) $aResult['zoom'] = 12;
			elseif ($fMaxDiameter < 0.64) $aResult['zoom'] = 11;
			elseif ($fMaxDiameter < 1.28) $aResult['zoom'] = 10;
			elseif ($fMaxDiameter < 2.56) $aResult['zoom'] = 9;
			elseif ($fMaxDiameter < 5.12) $aResult['zoom'] = 8;
			elseif ($fMaxDiameter < 10.24) $aResult['zoom'] = 7;
			else $aResult['zoom'] = 6;
		}
		else
		{
		$sSQL = 'select ST_X(ST_PointN(ExteriorRing(ST_Box2D(geometry)),3))-ST_X(ST_PointN(ExteriorRing(ST_Box2D(geometry)),1)) as width,';
		$sSQL .= ' ST_Y(ST_PointN(ExteriorRing(ST_Box2D(geometry)),2))-ST_Y(ST_PointN(ExteriorRing(ST_Box2D(geometry)),4)) as height';
		$sSQL .= ' from placex where place_id = '.$aResult['place_id'];
		$aDiameter = $oDB->getRow($sSQL);
		$aResult['zoom'] = 14;
		if (!PEAR::IsError($aDiameter))
		{
			$fMaxDiameter = max($aDiameter['width'], $aDiameter['height']);
			if ($fMaxDiameter < 0.0001)
			{
				$aResult['zoom'] = 17;
				if (isset($aClassType[$aResult['class'].':'.$aResult['type']]['defzoom']) 
					&& $aClassType[$aResult['class'].':'.$aResult['type']]['defzoom'])
				{
					$aResult['zoom'] = $aClassType[$aResult['class'].':'.$aResult['type']]['defzoom'];
				}
			}
			elseif ($fMaxDiameter < 0.005) $aResult['zoom'] = 18;
			elseif ($fMaxDiameter < 0.01) $aResult['zoom'] = 17;
			elseif ($fMaxDiameter < 0.02) $aResult['zoom'] = 16;
			elseif ($fMaxDiameter < 0.04) $aResult['zoom'] = 15;
			elseif ($fMaxDiameter < 0.08) $aResult['zoom'] = 14;
			elseif ($fMaxDiameter < 0.16) $aResult['zoom'] = 13;
			elseif ($fMaxDiameter < 0.32) $aResult['zoom'] = 12;
			elseif ($fMaxDiameter < 0.64) $aResult['zoom'] = 11;
			elseif ($fMaxDiameter < 1.28) $aResult['zoom'] = 10;
			elseif ($fMaxDiameter < 2.56) $aResult['zoom'] = 9;
			elseif ($fMaxDiameter < 5.12) $aResult['zoom'] = 8;
			elseif ($fMaxDiameter < 10.24) $aResult['zoom'] = 7;
			else $aResult['zoom'] = 6;
		}
		}

		if (isset($aClassType[$aResult['class'].':'.$aResult['type']]['icon']) 
			&& $aClassType[$aResult['class'].':'.$aResult['type']]['icon'])
		{
			$aResult['icon'] = 'http://katie.openstreetmap.org/~twain/images/mapicons/'.$aClassType[$aResult['class'].':'.$aResult['type']]['icon'].'.p.20.png';
		}

		if (isset($aClassType[$aResult['class'].':'.$aResult['type']]['importance']) 
			&& $aClassType[$aResult['class'].':'.$aResult['type']]['importance'])
		{
			$aResult['importance'] = $aClassType[$aResult['class'].':'.$aResult['type']]['importance'];
		}
		else
		{
			$aResult['importance'] = 1000000000000000;
		}

		$aResult['name'] = $aResult['langaddress'];
		$aSearchResults[$iResNum] = $aResult;
	}
	
	uasort($aSearchResults, 'byImportance');
	
	$aOSMIDDone = array();
	$aClassTypeNameDone = array();
	$aToFilter = $aSearchResults;
	$aSearchResults = array();

	$bFirst = true;
	foreach($aToFilter as $iResNum => $aResult)
	{
		if ($bFirst)
		{
			$fLat = $aResult['lat'];
			$fLon = $aResult['lon'];
			if (isset($aResult['zoom'])) $iZoom = $aResult['zoom'];
			$bFirst = false;
		}
		if (!isset($aOSMIDDone[$aResult['osm_type'].$aResult['osm_id']])
			&& !isset($aClassTypeNameDone[$aResult['osm_type'].$aResult['osm_class'].$aResult['name']]))
		{
			$aOSMIDDone[$aResult['osm_type'].$aResult['osm_id']] = true;
			$aClassTypeNameDone[$aResult['osm_type'].$aResult['osm_class'].$aResult['name']] = true;
			$aSearchResults[] = $aResult;
		}
	}

	include('.htlib/output/search-'.$sOutputFormat.'.php');
