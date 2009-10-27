<?php
	require_once('DB.php');
	$oDB =& DB::connect('pgsql://@/gazetteer', false);
	$oDB->setFetchMode(DB_FETCHMODE_ASSOC);
	$oDB->query("SET DateStyle TO 'sql,european'");
	$oDB->query("SET client_encoding TO 'utf-8'");

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
