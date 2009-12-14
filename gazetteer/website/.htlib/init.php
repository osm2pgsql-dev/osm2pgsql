<?php

	require_once('settings.php');
	require_once('lib.php');
	require_once('DB.php');

	if (get_magic_quotes_gpc())
	{
		echo "Please disable magic quotes in your php.ini configuration";
		exit;
	}

	if (CONST_ClosedForIndexing && strpos(CONST_ClosedForIndexingExceptionIPs, ','.$_SERVER["REMOTE_ADDR"].',') === false)
 	{
		echo "Closed for re-indexing...";
		exit;
	}

	if (strpos(CONST_BlockedIPs, ','.$_SERVER["REMOTE_ADDR"].',') !== false)
	{
		echo "Your IP has been blocked. \n";
		echo "Please create a nominatim trac ticket (http://trac.openstreetmap.org/newticket?component=nominatim) to request this to be removed. \n";
		echo "Information on the Nominatim usage policy can be found here: http://wiki.openstreetmap.org/wiki/Nominatim#Usage_Policy \n";
		exit;
	}

	// Get the database object
	$oDB =& DB::connect(CONST_Database_DSN, false);
	if (PEAR::IsError($oDB))
	{
		fail($oDB->getMessage(), 'Unable to connect to the database');
	}
	$oDB->setFetchMode(DB_FETCHMODE_ASSOC);
	$oDB->query("SET DateStyle TO 'sql,european'");
	$oDB->query("SET client_encoding TO 'utf-8'");

	header('Content-type: text/html; charset=utf-8');
