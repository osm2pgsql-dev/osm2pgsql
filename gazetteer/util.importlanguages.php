#!/usr/bin/php -Cq
<?php
	require_once('DB.php');
	$oDB =& DB::connect('pgsql://@/gazetteer', false);
	$oDB->setFetchMode(DB_FETCHMODE_ASSOC);
	$oDB->query("SET DateStyle TO 'sql,european'");
	$oDB->query("SET client_encoding TO 'utf-8'");

	if (false)
	{
		$sYML = file_get_contents('http://svn.openstreetmap.org/sites/rails_port/config/languages.yml');

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

	if (true)
	{
		$aData = array();
		$sConfigFilesHTML = file_get_contents('http://svn.openstreetmap.org/sites/rails_port/config/locales/');
		if (preg_match_all('/href="([a-z]{2,3}(-[a-zA-Z]+)?[.]yml)"/', $sConfigFilesHTML, $aYMLLanguages))
		{
			foreach($aYMLLanguages[1] as $sFileName)
			{
				$sConfigFilesHTML = file_get_contents('http://svn.openstreetmap.org/sites/rails_port/config/locales/'.$sFileName);
				$aTree = array();
				foreach(explode("\n",$sConfigFilesHTML) as $sLine)
				{
					if ($sLine[0] == '#') continue;
					preg_match('/( *)([^:]+): ?(.*)/',$sLine, $aLine);
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
			if (!isset($aLanguageData['geocoder']['search_osm_nominatim']['prefix'])) continue;
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
					echo "$sLabel: ($sClass=$sType)\n";

					$sLabel = pg_escape_string($sLabel);
//					echo "select getorcreate_amenity(make_standard_name('$sLabel'), '$sClass','$sType');\n";
				}
			}
		}
		
	}
