<?php

        require_once('DB.php');
        $oDB =& DB::connect('pgsql://@/gazetteer', false);
        $oDB->setFetchMode(DB_FETCHMODE_ASSOC);
        $oDB->query("SET DateStyle TO 'sql,european'");
        $oDB->query("SET client_encoding TO 'utf-8'");

	$sAbbreviationsHTML = file_get_contents('http://wiki.openstreetmap.org/wiki/Name_finder:Abbreviations');

	preg_match_all('#<td>([^<]*)</td><td>([^<]*)</td><td>([^<]*)</td><td>([^<]*)</td><td>([^<]*)</td><td>(.*)#', $sAbbreviationsHTML, $aMatches, PREG_SET_ORDER);

	$aOut = array();
	$aOut[' st '] = ' st ';
	$aOut[' dr '] = ' dr ';
/*
' st ' => ' saint '
' st ' => ' street '
' str ' => ' street '
*/
	function _addAbbr($sResult, $sSearch, &$aCluster)
	{
		if (isset($aCluster[$sSearch]))
		{
			$aCluster[$sResult] = $aCluster[$sSearch];
		}
		else
		{
			$aCluster[$sSearch] = $sResult;
		}
		if (!isset($aCluster[$sSearch]))
		{
			$aCluster[$sResult] = $sResult;
		}
	}

	foreach($aMatches as $aMatch)
	{
		$aFullWord = explode(',',$aMatch[1]);
		$aAbbrWord = explode(',',$aMatch[2]);
		$sConCat = trim($aMatch[3]);
		$sSep = trim($aMatch[4]);
		$sNotes = trim($aMatch[6]);

		if ($sNotes == 'Abbreviation not in general use') continue;

		foreach($aFullWord as $sFullWord)
		{
			foreach($aAbbrWord as $sAbbrWord)
			{
				if ($sFullWord && $sAbbrWord)
				{
					$sFullWord = trim(str_replace('  ',' ',$oDB->getOne('select transliteration(\''.$sFullWord.'\')')));
					$sAbbrWord = trim(str_replace('  ',' ',$oDB->getOne('select transliteration(\''.$sAbbrWord.'\')')));
					_addAbbr(" $sAbbrWord "," $sFullWord ", $aOut);
/*
					if ($sConCat == 'yes')
					{
						if ($sSep == 'yes')
						{
							_addAbbr("$sAbbrWord ","$sFullWord ", $aOut);
							_addAbbr("$sFullWord "," $sFullWord ", $aOut);
						}
						else
						{
							_addAbbr("$sAbbrWord ","$sFullWord ", $aOut);
						}
					}
*/
				}
			}
		}
	}

	ksort($aOut);
	foreach($aOut as $sSearch => $sResult) if ($sSearch != $sResult) echo "out := replace(out, '$sSearch','$sResult');\n";
