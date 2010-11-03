#!/usr/bin/php -Cq
<?php
	require_once('DB.php');
	$oDB =& DB::connect('pgsql://@/gazetteer', false);
	$oDB->setFetchMode(DB_FETCHMODE_ASSOC);
	$oDB->query("SET DateStyle TO 'sql,european'");
	$oDB->query("SET client_encoding TO 'utf-8'");

	$aLanguageIn = array(
			'ar' => array('في'),
			'ca' => array('a', 'dins', 'en'),
			'dk' => array('i'),
			'nl' => array('in', 'te'),
			'en' => array('in'),
			'fr' => array('à','en','dans'),
			'de' => array('in','im'),
//			'he' => array('ב'), //Not sure how to deal with this one
			'hu' => array('itt'),
			'it' => array('in','a','ad'),
			'pl' => array('w'),
			'pt' => array('em'),
			'ru' => array('в'),
			'sk' => array('v'),
			'sl' => array('v'),
			'es' => array('en'),
			'sv' => array('i','på'),
			'tr' => array('\'de','\'da'),
		);

	$aLanguageNear = array(
			'ar' => array('قرب'),
			'ca' => array('prop de', 'prop', 'a prop', 'proper', 'proper a'),
			'dk' => array('nær'),
			'nl' => array('bij, nabij', 'in de buurt van'),
			'en' => array('near'),
			'fi' => array('lähellä'),
			'fr' => array('près', 'près de', 'proche', 'proche de'),
			'de' => array('nah', 'nahe', 'bei', 'in der Nähe von'),
//			'he' => array('ליד','בקרבת'),
			'hu' => array('közelében'),
			'it' => array('vicino', 'vicino a', 'presso'),
			'pl' => array('blisko', 'w pobliżu', 'przy', 'niedaleko', 'obok', 'koło'),
			'pt' => array('perto de', 'próximo a'),
			'ru' => array('возле', 'около', 'рядом с', 'у', 'близ'),
			'sk' => array('blízko', 'pri', 'v blízkosti', 'nedaleko'),
			'sl' => array('blizu', 'pri', 'v bližini'),
			'es' => array('cerca', 'cerca de'),
			'sv' => array('vid', 'nära', 'i närheten av', 'bredvid', 'omkring'),
			'tr' => array('yakın', 'yakınlarında', 'civarında', 'yanında'),
		);

	if (false)
	{
		$sYML = file_get_contents('rails/config/languages.yml');

		$oDB->query('delete from languagedata');

		$aLines = explode("\n", $sYML);
		$sCode = false;
		$sNative = false;
		$sEnglish = false;
		$aCodes = array();
		foreach($aLines as $sLine)
		{
			if (preg_match('/^([a-z]{2})\\:/', $sLine, $aData))
			{
				if ($sCode && ($sNative || $sEnglish))
				{
					$oDB->query($sSQL = 'insert into language values (\''.$sCode.'\',\''.$sNative.'\',\''.$sEnglish.'\')');
				}
				$sCode = $aData[1];
				$sLocal = false;
				$sEnglish = false;
			}
			if (preg_match('/ *english\\: ([^;]+)/', $sLine, $aData))
			{
				$sEnglish = $aData[1];
			}
			if (preg_match('/ *native\\: ([^;]+)/', $sLine, $aData))
			{
				$sNative = $aData[1];
			}
		}
		if ($sCode && ($sNative || $sEnglish))
		{
			$oDB->query($sSQL = 'insert into language values (\''.$sCode.'\',\''.$sNative.'\',\''.$sEnglish.'\')');
		}
	}

	if (false)
	{
		$aData = array();
		foreach (glob("rails/config/locales/*") as $sFileName)
		{
			$sConfigFilesHTML = file_get_contents($sFileName);
			$aTree = array();
			foreach(explode("\n",$sConfigFilesHTML) as $sLine)
			{
				if (@$sLine[0] == '#') continue;

				if (preg_match('/( *)([^:]+): ?(.*)/',$sLine, $aLine))
				{
					$iDepth = strlen($aLine[1]) / 2;
					if ($aLine[3])
					{
						$aP =& $aData;
						for($i = 0; $i < $iDepth; $i++)
						{
							if (!isset($aP[$aTree[$i]])) $aP[$aTree[$i]] = array();
							$aP =& $aP[$aTree[$i]];
						}
						$aP[$aLine[2]] = $aLine[3];
						
					}
					else
					{
						$aTree[$iDepth] = $aLine[2];
					}
				}
			}
		}
		$aTranslatedTags = array();
		foreach($aData as $sLanguage => $aLanguageData)
		{
			if (	!isset($aLanguageData['geocoder']) ||
				!isset($aLanguageData['geocoder']['search_osm_nominatim']) || 
				!isset($aLanguageData['geocoder']['search_osm_nominatim']['prefix']) || 
				!is_array($aLanguageData['geocoder']['search_osm_nominatim']['prefix']))
			{
				continue;
			}
			foreach($aLanguageData['geocoder']['search_osm_nominatim']['prefix'] as $sClass => $aType)
			{
				foreach($aType as $sType => $sLabel)
				{
					if ($sLabel[0] == '"' && substr($sLabel, -1, 1) == '"') $sLabel = substr($sLabel, 1, -1);
					$aTranslatedTags[$sClass][$sType][$sLanguage] = $sLabel;
				}
			}
		}
		foreach($aTranslatedTags as $sClass => $aType)
		{
			foreach($aType as $sType => $aLanguage)
			{
				foreach($aLanguage as $sLanguage => $sLabel)
				{
//					echo "$sLabel: ($sClass=$sType)\n";

					echo "select getorcreate_amenity(make_standard_name('".pg_escape_string($sLabel)."'), '$sClass', '$sType');\n";
					if (isset($aLanguageIn[$sLanguage]))
					{
						foreach($aLanguageIn[$sLanguage] as $sIn)
						{
							echo "select getorcreate_amenityoperator(make_standard_name('".pg_escape_string($sLabel.' '.$sIn)."'), '$sClass', '$sType', 'in');\n";
						}
					}
					if (isset($aLanguageNear[$sLanguage]))
					{
						foreach($aLanguageNear[$sLanguage] as $sNear)
						{
							echo "select getorcreate_amenityoperator(make_standard_name('".pg_escape_string($sLabel.' '.$sNear)."'), '$sClass', '$sType', 'near');\n";
						}
					}
				}
			}
		}
	}

	if (true)
	{
		$aData = array();
		foreach (glob("rails/config/locales/*") as $sFileName)
		{
			$sConfigFilesHTML = file_get_contents($sFileName);
			$aTree = array();
			foreach(explode("\n",$sConfigFilesHTML) as $sLine)
			{
				if (@$sLine[0] == '#') continue;

				if (preg_match('/( *)([^:]+): ?"?([^"]*)"?/',$sLine, $aLine))
				{
					$iDepth = strlen($aLine[1]) / 2;
					if ($aLine[3])
					{
						$aP =& $aData;
						for($i = 0; $i < $iDepth; $i++)
						{
							if (!isset($aP[$aTree[$i]])) $aP[$aTree[$i]] = array();
							$aP =& $aP[$aTree[$i]];
						}
						$aP[$aLine[2]] = $aLine[3];
						
					}
					else
					{
						$aTree[$iDepth] = $aLine[2];
					}
				}
			}
		}
		$aTranslatedTags = array();
		foreach($aData as $sLanguage => $aLanguageData)
		{
			if (	!isset($aLanguageData['geocoder']) ||
				!isset($aLanguageData['geocoder']['search_osm_nominatim']) || 
				!isset($aLanguageData['geocoder']['search_osm_nominatim']['prefix']) || 
				!is_array($aLanguageData['geocoder']['search_osm_nominatim']['prefix']))
			{
				continue;
			}
			foreach($aLanguageData['geocoder']['search_osm_nominatim']['prefix'] as $sClass => $aType)
			{
				foreach($aType as $sType => $sLabel)
				{
					if ($sLabel[0] == '"' && substr($sLabel, -1, 1) == '"') $sLabel = substr($sLabel, 1, -1);
					$aTranslatedTags[$sClass][$sType] = 1;
				}
			}
		}

		unset($aTranslatedTags['bridge']['yes']);
		unset($aTranslatedTags['building']['yes']);
		unset($aTranslatedTags['place']['house']);
		unset($aTranslatedTags['place']['postcode']);
		unset($aTranslatedTags['highway']['residential']);
		unset($aTranslatedTags['highway']['unclassified']);
		unset($aTranslatedTags['highway']['tertiary']);
		unset($aTranslatedTags['highway']['secondary']);

		foreach($aTranslatedTags as $sClass => $aType)
		{
			foreach($aType as $sType => $aLanguage)
			{
				if (!preg_match('/^[a-z]+$/', $sClass) || !preg_match('/^[a-z]+$/', $sType)) continue;
				echo "DROP TABLE place_classtype_".$sClass."_".$sType.";\n";
				echo "CREATE TABLE place_classtype_".$sClass."_".$sType." ( place_id bigint, centroid geometry );\n";
				echo "GRANT select on place_classtype_".$sClass."_".$sType." to \"www-data\";\n";
				echo "INSERT INTO place_classtype_".$sClass."_".$sType." SELECT place_id,st_centroid(geometry) from placex where class='".pg_escape_string($sClass)."' and type='".pg_escape_string($sType)."' ;\n";
				echo "CREATE INDEX idx_place_classtype_".$sClass."_".$sType."_centroid ON place_classtype_".$sClass."_".$sType." USING GIST (centroid);\n";
				echo "CREATE INDEX idx_place_classtype_".$sClass."_".$sType."_place_id ON place_classtype_".$sClass."_".$sType." USING BTREE (place_id);\n";
			}
		}
	}
