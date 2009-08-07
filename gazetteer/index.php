<?php
	header('Content-type: text/html; charset=utf-8');
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

	// Log
	if ($_GET['q']) $oDB->query('insert into query_log values (\'now\','.getDBQuoted($_GET['q']).','.getDBQuoted(($_GET["remote-ip"]?$_GET["remote-ip"]:$_SERVER["REMOTE_ADDR"])).')');

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

	if (isset($_GET['reverse']))
	{
		// TODO
		exit;
	}

	// Default possition
	$fLat = 20.0;
	$fLon = 0.0;
	$iZoom = 2;

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
				$sViewboxSmallSQL = "ST_SetSRID(ST_MakeBox2D(ST_Point(".(float)$aCoOrdinates[1].",".(float)$aCoOrdinates[0]."),ST_Point(".(float)$aCoOrdinates[3].",".(float)$aCoOrdinates[2].")),4326)";
				$fHeight = $aCoOrdinates[0]-$aCoOrdinates[2];
				$fWidth = $aCoOrdinates[1]-$aCoOrdinates[3];
				$aCoOrdinates[0] += $fHeight;
				$aCoOrdinates[2] -= $fHeight;
				$aCoOrdinates[1] += $fWidth;
				$aCoOrdinates[3] -= $fWidth;
				$sViewboxLargeSQL = "ST_SetSRID(ST_MakeBox2D(ST_Point(".(float)$aCoOrdinates[1].",".(float)$aCoOrdinates[0]."),ST_Point(".(float)$aCoOrdinates[3].",".(float)$aCoOrdinates[2].")),4326)";
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
if ($bDebug) var_Dump($sSQL);
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
						breakl;
					}
					$iWordID = $aValidTokens[' '.$aPhrase['wordsets'][0][0]][0]['word_id'];
					if ($iPhrase == 0) $aNameTerms[] = $iWordID;
					$aAddressTerms[] = $iWordID;
				}
				if ($bValidSearch)
				{
					if ($sViewboxSmallSQL)
					{
						$sSQL = "select place_id from search_name";
						$sSQL .= " where name_vector @> ARRAY[".join($aNameTerms,",")."]";
						$sSQL .= " and nameaddress_vector @> ARRAY[".join($aAddressTerms,",")."]";
 						$sSQL .= " and ST_Contains($sViewboxSmallSQL,centroid)";
						$sSQL .= " order by search_rank desc limit 10";
if ($bDebug) var_Dump($sSQL);
						$aPlaceIDs = $oDB->getCol($sSQL);
 					}
					if ($sViewboxLargeSQL)
					{
						$sSQL = "select place_id from search_name";
						$sSQL .= " where name_vector @> ARRAY[".join($aNameTerms,",")."]";
						$sSQL .= " and nameaddress_vector @> ARRAY[".join($aAddressTerms,",")."]";
 						$sSQL .= " and ST_Contains($sViewboxLargeSQL,centroid)";
						$sSQL .= " order by search_rank desc limit 10";
if ($bDebug) var_Dump($sSQL);
						$aPlaceIDs = $oDB->getCol($sSQL);
 					}
					if (!sizeof($aPlaceIDs))
					{
						$sSQL = "select place_id from search_name";
						$sSQL .= " where name_vector @> ARRAY[".join($aNameTerms,",")."]";
						$sSQL .= " and nameaddress_vector @> ARRAY[".join($aAddressTerms,",")."]";
						$sSQL .= " order by search_rank desc limit 10";
if ($bDebug) var_Dump($sSQL);
						$aPlaceIDs = $oDB->getCol($sSQL);
					}
					foreach($aPlaceIDs as $iPlaceID)
					{
						$aResultPlaceIDs[$iPlaceID] = $iPlaceID;
					}
				}
			}

			// No perfect match? generate token sets using all words
			if (!sizeof($aResultPlaceIDs))
			{
				// Try and calculate UK postcodes we might be missing
				foreach($aTokens as $sToken)
				{
					if (!isset($aValidTokens[$sToken]) && !isset($aValidTokens[' '.$sToken]) && preg_match('/^([A-Z][A-Z]?[0-9][0-9A-Z]? [0-9])([A-Z][A-Z])$/', strtoupper(trim($sToken)), $aData))
					{
						$aNearPostcodes = $oDB->getAll('select substring(upper(postcode) from \'^[A-Z][A-Z]?[0-9][0-9A-Z]? [0-9]([A-Z][A-Z])$\'),ST_X(ST_Centroid(geometry)) as lon,ST_Y(ST_Centroid(geometry)) as lat from placex where substring(upper(postcode) from \'^([A-Z][A-Z]?[0-9][0-9A-Z]? [0-9])[A-Z][A-Z]$\') = \''.$aData[1].'\'');
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
							$aValidTokens[$sToken] = array(array('lat' => $fLat, 'lon' => $fLon, 'radius' => $fRadius));
						}

						// $fTotalFac is a suprisingly good indicator of accuracy
						//$iZoom = 18 + round(log($fTotalFac,32));
						//$iZoom = max(13,min(18,$iZoom));
						//var_Dump(sizeof($aNearPostcodes), $fTotalFac, log($fTotalFac/sizeof($aNearPostcodes),32));
					}
				}

				// Build all searches using aValidTokens
							
				/*
					Phrase Wordset
					0      0       (hawkworth road)
					0      1       (hawksworth)(road)
					1      0       (sheffield)
				*/

				// Start with a blank search
				$aSearches = array(
					array('iSearchRank' => 0, 'iNamePhrase' => 0, 'sCountryCode' => false, 'aName'=>array(), 'aAddress'=>array(), 'sClass'=>'', 'sType'=>'', 'sHouseNumber'=>'', 'fLat'=>'', 'fLon'=>'', 'fRadius'=>'')
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
										$aSearch['iSearchRank']++;
										if ($aSearchTerm['country_code'] !== null)
										{
											$aSearch['sCountryCode'] = strtoupper($aSearchTerm['country_code']);
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
										elseif ($aSearchTerm['class'] !== '' && $aSearchTerm['class'] !== null)
										{
											if ($aSearch['sClass'] === '')
											{
												$aSearch['sClass'] = $aSearchTerm['class'];
												$aSearch['sType'] = $aSearchTerm['type'];
												if ($aSearch['sClass'] == 'place' && $aSearch['sType'] == 'house')
												{
													$aSearch['sHouseNumber'] = $sToken;
												}
												$aNewWordsetSearches[] = $aSearch;

												// Fall back to not searching for this item (better than nothing
												$aSearch = $aCurrentSearch;
												$aSearch['iSearchRank'] += 10;
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
													$aNewWordsetSearches[] = $aSearch;
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
					$aSearches = $aNewPhraseSearches;
				}
				
				// Re-group the searches by their score
				$aGroupedSearches = array();
				foreach($aSearches as $aSearch)
				{
					if ($aSearch['iSearchRank'] < 200)
					{
						if (!isset($aGroupedSearches[$aSearch['iSearchRank']])) $aGroupedSearches[$aSearch['iSearchRank']] = array();
						$aGroupedSearches[$aSearch['iSearchRank']][] = $aSearch;
					}
				}
				ksort($aGroupedSearches);

if ($bDebug) var_Dump($aGroupedSearches);

				foreach($aGroupedSearches as $aSearches)
				{
					foreach($aSearches as $aSearch)
					{
//	if ($bDebug) var_dump($aSearch);
						$aPlaceIDs = array();
						
						if (sizeof($aSearch['aName']) && $aSearch['fLat'] !== '' && $aSearch['sClass'])
						{
							$sSQL = "select place_id from search_name where ";
							$sSQL .= " name_vector @> ARRAY[".join($aSearch['aName'],",")."]";
							$sSQL .= " and nameaddress_vector @> ARRAY[".join($aSearch['aAddress'],",")."]";
							$sSQL .= " and ST_DWithin(centroid, ST_SetSRID(ST_Point(".$aSearch['fLon'].",".$aSearch['fLat']."),4326), ".$aSearch['fRadius'].") ";
							if ($aSearch['sCountryCode']) $sSQL .= " and country_code = '".pg_escape_string($aSearch['sCountryCode'])."'";
	        		$sSQL .= " order by ST_Distance(centroid, ST_SetSRID(ST_Point(".$aSearch['fLon'].",".$aSearch['fLat']."),4326)) ASC, search_rank ASC limit 10";
	if ($bDebug)         		var_dump($sSQL);
							$aPlaceIDs = $oDB->getCol($sSQL);
							
							if (sizeof($aPlaceIDs))
							{
								$sPlaceIDs = join(',',$aPlaceIDs);
		  					$sSQL = "select place_id from placex where place_id in ($sPlaceIDs) and class='".$aSearch['sClass']."' and type='".$aSearch['sType']."'";
								if ($aSearch['sHouseNumber']) $sSQL .= "and l.housenumber='".$aSearch['sHouseNumber']."' ";
		        		$sSQL .= " order by rank_search asc limit 10";
	if ($bDebug) 	        		var_dump($sSQL);
								$aPlaceIDs = $oDB->getCol($sSQL);
								
								if (!sizeof($aPlaceIDs))
								{
									$fRange = 0.01;
									if ($aSearch['sHouseNumber']) $fRange = 0.001;
									$sSQL = "select l.place_id from placex as l,placex as f where ";
									$sSQL .= "f.place_id in ($sPlaceIDs) and ST_DWithin(l.geometry, f.geometry, $fRange) ";
									$sSQL .= "and l.class='".$aSearch['sClass']."' and l.type='".$aSearch['sType']."' ";
									if ($aSearch['sHouseNumber']) $sSQL .= "and l.housenumber='".$aSearch['sHouseNumber']."' ";
									if ($aSearch['sCountryCode']) $sSQL .= " and l.country_code = '".pg_escape_string($aSearch['sCountryCode'])."'";
									$sSQL .= " order by ST_Distance(l.geometry, f.geometry) asc, l.rank_search ASC limit 10";
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
							if ($aSearch['sCountryCode']) $sSQL .= " and country_code = '".pg_escape_string($aSearch['sCountryCode'])."'";
				        		$sSQL .= " order by ST_Distance(centroid, ST_SetSRID(ST_Point(".$aSearch['fLon'].",".$aSearch['fLat']."),4326)) ASC, search_rank ASC limit 10";
	if ($bDebug)         		var_dump($sSQL);
							$aPlaceIDs = $oDB->getCol($sSQL);
						}
						elseif (sizeof($aSearch['aName']) && $aSearch['sClass'])
						{
							$sSQL = "select place_id,search_rank from search_name where ";
							$sSQL .= " name_vector @> ARRAY[".join($aSearch['aName'],",")."]";
							$sSQL .= " and nameaddress_vector @> ARRAY[".join($aSearch['aAddress'],",")."]";
							if ($aSearch['sCountryCode']) $sSQL .= " and country_code = '".pg_escape_string($aSearch['sCountryCode'])."'";
				        		$sSQL .= " order by search_rank ASC limit 10";
	if ($bDebug)         		var_dump($sSQL);
							$aNearPlaceIDs = $oDB->getAssoc($sSQL);
	
							if (sizeof($aNearPlaceIDs))
							{
								$sPlaceIDs = join(',',array_keys($aNearPlaceIDs));
			  					$sSQL = "select place_id from placex where place_id in ($sPlaceIDs) and class='".$aSearch['sClass']."' and type='".$aSearch['sType']."'";
					        		$sSQL .= " order by rank_search asc limit 100";
	if ($bDebug) 	        		var_dump($sSQL);
								$aPlaceIDs = $oDB->getCol($sSQL);
								
								if (!sizeof($aPlaceIDs))
								{
									$fRange = 0.01;
									if ($aSearch['sHouseNumber']) $fRange = 0.001;
									$sSQL = "select l.place_id from placex as l,placex as f where ";
									$sSQL .= "f.place_id in ($sPlaceIDs) and ST_DWithin(l.geometry, f.geometry, $fRange) ";
									$sSQL .= "and l.class='".$aSearch['sClass']."' and l.type='".$aSearch['sType']."' ";
									if ($aSearch['sHouseNumber']) $sSQL .= "and l.housenumber='".$aSearch['sHouseNumber']."' ";
									if ($aSearch['sCountryCode']) $sSQL .= " and l.country_code = '".pg_escape_string($aSearch['sCountryCode'])."'";
									$sSQL .= " order by f.rank_search ASC,ST_Distance(l.geometry, f.geometry) asc, l.rank_search ASC limit 100";
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
							if ($aSearch['sCountryCode']) $sSQL .= " and country_code = '".pg_escape_string($aSearch['sCountryCode'])."'";
	        					$sSQL .= " order by ST_Distance(geometry, ST_SetSRID(ST_Point(".$aSearch['fLon'].",".$aSearch['fLat']."),4326)) ASC, rank_search asc limit 10";
	if ($bDebug)         		var_dump($sSQL);
							$aPlaceIDs = $oDB->getCol($sSQL);
						}
						elseif (sizeof($aSearch['aName']))
						{
							$sSQL = "select place_id from search_name where ";
							$sSQL .= " name_vector @> ARRAY[".join($aSearch['aName'],",")."]";
							$sSQL .= " and nameaddress_vector @> ARRAY[".join($aSearch['aAddress'],",")."]";
							if ($aSearch['sCountryCode']) $sSQL .= " and country_code = '".pg_escape_string($aSearch['sCountryCode'])."'";
	        					$sSQL .= " order by search_rank ASC limit 10";
	if ($bDebug)         		var_dump($sSQL);
							$aPlaceIDs = $oDB->getCol($sSQL);
						}
						elseif ($aSearch['fLat'])
						{
							$sSQL = "select place_id from search_name where search_rank > 25 and ";
							$sSQL .= " ST_DWithin(centroid, ST_SetSRID(ST_Point(".$aSearch['fLon'].",".$aSearch['fLat']."),4326), ".$aSearch['fRadius'].") ";
							if ($aSearch['sCountryCode']) $sSQL .= " and country_code = '".pg_escape_string($aSearch['sCountryCode'])."'";
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
					if (sizeof($aResultPlaceIDs)) break;
				}
	
	//var_dump($aResultPlaceIDs);
				// Still nothing, You have to be kidding!
				if (!sizeof($aResultPlaceIDs))
				{
//					echo "No Matches Found";
	//				var_dump($aValidTokens);
				}
				else
				{
					$sPlaceIDs = join(',',$aResultPlaceIDs);
					$sOrderSQL = 'CASE ';
					foreach(array_keys($aResultPlaceIDs) as $iOrder => $iPlaceID)
					{
						$sOrderSQL .= 'when min(place_id) = '.$iPlaceID.' then '.$iOrder.' ';
					}
					$sOrderSQL .= ' ELSE 10000000 END ASC';
					$sSQL = "select class,type,rank_search,rank_address,min(place_id) as place_id,get_address_by_language(place_id, ARRAY[".join(',',array_map("getDBQuoted",$aLangPrefOrder))."]) as langaddress,";
					$sSQL .= "avg(ST_X(ST_Centroid(geometry))) as lon,avg(ST_Y(ST_Centroid(geometry))) as lat ";
					$sSQL .= "from placex where place_id in ($sPlaceIDs) ";
					$sSQL .= "group by class,type,rank_search,rank_address,get_address_by_language(place_id, ARRAY[".join(',',array_map("getDBQuoted",$aLangPrefOrder))."]) ";
					$sSQL .= "order by rank_search,rank_address,".$sOrderSQL;
					$aSearchResults = $oDB->getAll($sSQL);
if (PEAR::IsError($aSearchResults))
{
var_dump($sSQL, $aSearchResults);					
exit;
}
				}
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
if (!sizeof($aSearchResults) && $_GET['q'])
{
	$sSearchResult = 'No Results Found';
}
foreach($aSearchResults as $iResNum => $aResult)
{
 $sSQL = 'select ST_X(ST_PointN(ExteriorRing(ST_Box2D(geometry)),3))-ST_X(ST_PointN(ExteriorRing(ST_Box2D(geometry)),1)) as width,';
 $sSQL .= ' ST_Y(ST_PointN(ExteriorRing(ST_Box2D(geometry)),2))-ST_Y(ST_PointN(ExteriorRing(ST_Box2D(geometry)),4)) as height';
 $sSQL .= ' from placex where place_id = '.$aResult['place_id'];
 $aDiameter = $oDB->getRow($sSQL);
if (!PEAR::IsError($aDiameter))
{
//var_Dump($sSQL, $aDiameter);

$fMaxDiameter = max($aDiameter['width'], $aDiameter['height']);
//var_Dump($aResult, $fMaxDiameter, $aDiameter);
if ($fMaxDiameter < 0.0001)
{
	$aResult['zoom'] = 17;
	switch($aResult['class'].':'.$aResult['type'])
	{
	case 'place:city':
		$aResult['zoom'] = 12;
		break;
	case 'place:town':
		$aResult['zoom'] = 14;
		break;
	case 'place:village':
	case 'place:hamlet':
		$aResult['zoom'] = 15;
		break;
	case 'place:house':
		$aResult['zoom'] = 18;
		break;
	}
}
elseif ($fMaxDiameter < 0.005) $aResult['zoom'] = 18;
else if ($fMaxDiameter < 0.01) $aResult['zoom'] = 17;
else if ($fMaxDiameter < 0.02) $aResult['zoom'] = 16;
else if ($fMaxDiameter < 0.04) $aResult['zoom'] = 15;
else if ($fMaxDiameter < 0.08) $aResult['zoom'] = 14;
else if ($fMaxDiameter < 0.16) $aResult['zoom'] = 13;
else if ($fMaxDiameter < 0.32) $aResult['zoom'] = 12;
else if ($fMaxDiameter < 0.64) $aResult['zoom'] = 11;
else if ($fMaxDiameter < 1.28) $aResult['zoom'] = 10;
else if ($fMaxDiameter < 2.56) $aResult['zoom'] = 9;
else if ($fMaxDiameter < 5.12) $aResult['zoom'] = 8;
else if ($fMaxDiameter < 10.24) $aResult['zoom'] = 7;
else $aResult['zoom'] = 6;
}
else
{
	$aResult['zoom'] = 14;
}
$aResult['name'] = $aResult['langaddress'];
switch($aResult['class'].':'.$aResult['type'])
{
case 'place:city':
//	$aResult['name'] .= ' (City)';
	break;
case 'place:town':
//	$aResult['name'] .= ' (Town)';
	break;
case 'railway:station':
	$aResult['icon'] = '<img src="http://www.sjjb.co.uk/mapicons/SJJBMapIconsv0.03/recoloured/transport_train_station2.p.20.png">';
	break;
case 'railway:tram_stop':
	$aResult['icon'] = '<img src="http://www.sjjb.co.uk/mapicons/SJJBMapIconsv0.03/recoloured/transport_tram_stop.p.20.png">';
	break;
case 'historic:castle':
//	$aResult['name'] .= ' (Castle)';
	break;
case 'amenity:place_of_worship':
//	$aResult['name'] .= ' (Church)';
	break;
case 'amenity:pub':
	$aResult['icon'] = '<img src="http://www.sjjb.co.uk/mapicons/SJJBMapIconsv0.03/recoloured/food_pub.p.20.png">';
	break;
case 'shop:supermarket':
	$aResult['icon'] = '<img src="http://www.sjjb.co.uk/mapicons/SJJBMapIconsv0.03/recoloured/shopping_supermarket.p.20.png">';
	break;
default:
	break;
}
//var_dump($aResult);
if (isset($_GET['format']) && $_GET['format'] == 'raw')
{
	$sSearchResult .= '<div class="result">';
}
else
{
	if (isset($aResult['zoom']))
		$sSearchResult .= '<div class="result" onClick="panToLatLonZoom('.$aResult['lat'].', '.$aResult['lon'].', '.$aResult['zoom'].');">';
	else
		$sSearchResult .= '<div class="result" onClick="panToLatLon('.$aResult['lat'].', '.$aResult['lon'].');">';
}
$sSearchResult .= $aResult['icon'];
$sSearchResult .= ' <span class="name">'.$aResult['name'].'</span>';
$sSearchResult .= ' <span class="latlon">'.round($aResult['lat'],3).','.round($aResult['lat'],3).'</span>';
$sSearchResult .= ' <span class="place_id">'.$aResult['place_id'].'</span>';
$sSearchResult .= ' <span class="type">('.ucwords(str_replace('_',' ',$aResult['type'])).')</span>';
$sSearchResult .= ' <span class="details">(<a href="details.php?place_id='.$aResult['place_id'].'">details</a>)</span>';
$sSearchResult .= '</div>';

if ($iResNum == 0)
{
	$fLat = $aResult['lat'];
	$fLon = $aResult['lon'];
	if (isset($aResult['zoom'])) $iZoom = $aResult['zoom'];
}

}
if (isset($_GET['format']) && $_GET['format'] == 'raw')
{
echo $sSearchResult;
exit;
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
body {
	margin:0px;
	padding:0px;
	overflow: hidden;
  background:#ffffff;
  height: 100%;
}
#seachheader {
  position:absolute;
  z-index:5;
  top:0px;
  left:0px;
  width:100%;
  height:38px;
  background:#F0F7FF;
  border-bottom: 2px solid #75ADFF;
}
#q {
  width:300px;
}
#seachheaderfade1, #seachheaderfade2, #seachheaderfade3, #seachheaderfade4{
  position:absolute;
  z-index:4;
  top:0px;
  left:0px;
  width:100%;
  opacity: 0.15;
  filter: alpha(opacity = 15);
  background:#000000;
  border: 1px solid #000000;
}
#seachheaderfade1{
  height:39px;
}
#seachheaderfade2{
  height:40px;
}
#seachheaderfade3{
  height:41px;
}
#seachheaderfade4{
  height:42px;
}
#searchresultsfade1, #searchresultsfade2, #searchresultsfade3, #searchresultsfade4 {
  position:absolute;
  z-index:2;
  top:0px;
  left:200px;
  height: 100%;
  opacity: 0.2;
  filter: alpha(opacity = 20);
  background:#ffffff;
  border: 1px solid #ffffff;
}
#searchresultsfade1{
  width:1px;
}
#searchresultsfade2{
  width:2px;
}
#searchresultsfade3{
  width:3px;
}
#searchresultsfade4{
  width:4px;
}

#searchresults{
  position:absolute;
  z-index:3;
  top:41px;
  width:200px;
  height: 100%;
  background:#ffffff;
  border: 1px solid #ffffff;
}
#map{
  position:absolute;
  z-index:1;
  top:38px;
  left:200px;
  width:100%;
  height:100%;
  background:#eee;
}
.result {
	margin:5px;
	margin-bottom:0px;
	padding:2px;
	padding-left:4px;
	padding-right:4px;
	border-radius: 5px;
  -moz-border-radius: 5px;
  -webkit-border-radius: 5px;
  background:#F0F7FF;
  border: 2px solid #D7E7FF;
  font: normal 12px/15px arial,sans-serif;
}
.result img{
  float:right;
}
.result .latlon{
  display: none;
}
.result .place_id{
  display: none;
}
.result .type{
  color: #ccc;
  text-align:center;
  font: normal 9px/10px arial,sans-serif;
  padding-top:4px;
}
.result .details, .result .details a{
  color: #ccc;
  text-align:center;
  font: normal 9px/10px arial,sans-serif;
  padding-top:4px;
}
.disclaimer{
  color: #ccc;
  text-align:center;
  font: normal 9px/10px arial,sans-serif;
  padding-top:4px;
}
form{
  margin:0px;
  padding:0px;
}
    </style>

    <script type="text/javascript">
        
        var map;

	function handleResize()
	{
//		if (document.documentElement.clientWidth > 0)
		{
			$('map').style.width = (document.documentElement.clientWidth > 0?document.documentElement.clientWidth:document.documentElement.offsetWidth) - 200;
			$('map').style.height = (document.documentElement.clientHeight > 0?document.documentElement.clientHeight:document.documentElement.offsetHeight) - 38;
		}
	}
	window.onresize = handleResize;

	function panToLatLon(lat,lon) {
            	var lonLat = new OpenLayers.LonLat(lon, lat).transform(new OpenLayers.Projection("EPSG:4326"), map.getProjectionObject());
            	map.panTo(lonLat, <?php echo $iZoom ?>);
	}

	function panToLatLonZoom(lat,lon, zoom) {
            	var lonLat = new OpenLayers.LonLat(lon, lat).transform(new OpenLayers.Projection("EPSG:4326"), map.getProjectionObject());
		if (zoom != map.getZoom())
	            	map.setCenter(lonLat, zoom);
		else
        	    	map.panTo(lonLat, 10);
	}

	function mapEventMove() {
		var proj = new OpenLayers.Projection("EPSG:4326");
		var bounds = map.getExtent();
		bounds = bounds.transform(map.getProjectionObject(), proj);
		$('viewbox').value = bounds.left+','+bounds.top+','+bounds.right+','+bounds.bottom;

		/*
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
		*/
	}

        function init() {
        	
        	handleResize();

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

<div id="seachheaderfade1"></div><div id="seachheaderfade2"></div><div id="seachheaderfade3"></div><div id="seachheaderfade4"></div>
<div id="searchresultsfade1"></div><div id="searchresultsfade2"></div><div id="searchresultsfade3"></div><div id="searchresultsfade4"></div>

<div id="seachheader"><form>
<table border="0"><tr><td valign="center"><img src="images/logo.gif"></td><td valign="center"><input id="q" name="q" value="<?php echo htmlspecialchars($sQueryOut); ?>"><input type="hidden" id="viewbox" name="viewbox"></td><td><input type="submit" value="Search"></td>
<td valign="top" style="padding-left: 20px;font: normal 10px/12px arial,sans-serif;">Known problems:</td><td valign="top" style="font: normal 10px/12px arial,sans-serif;"> ss does not find &szlig;, requires re-index to fix</td>
</tr></table>
</form></div>

<div id="searchresults">
<?php echo $sSearchResult ?>
<div class="disclaimer">Addresses and postcodes are approximate</div>
</div>

<div id="map"></div>

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

*/

