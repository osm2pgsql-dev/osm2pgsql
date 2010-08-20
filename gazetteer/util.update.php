#!/usr/bin/php -Cq
<?php

	// Database connection string
	$sDatabaseName = 'gazetteerworld';
	$sDatabaseDSN = 'pgsql://@/'.$sDatabaseName;

	// make this point to a place where you want --import-hourly and --import-daily
	// save mirrored files
	$sMirrorDir = "/home/twain/";

	// Where to find and store osmosis files
	$sOsmosisCMD = '/home/twain/osmosis-0.35.1/bin/osmosis';
	$sOsmosisConfigDirectory = '/home/twain/.osmosis';

	ini_set('memory_limit', '800M');
	require_once('website/.htlib/lib.php');

	$aCMDOptions = array(
		"Import / update / index osm data",
		array('help', 'h', 0, 1, 0, 0, false, 'Show Help'),
		array('quiet', 'q', 0, 1, 0, 0, 'bool', 'Quiet output'),
		array('verbose', 'v', 0, 1, 0, 0, 'bool', 'Verbose output'),

		array('max-load', '', 0, 1, 1, 1, 'float', 'Maximum load average - indexing is paused if this is exceeded'),
		array('max-blocking', '', 0, 1, 1, 1, 'int', 'Maximum blocking processes - indexing is aborted / paused if this is exceeded'),

		array('import-osmosis', '', 0, 1, 0, 0, 'bool', 'Import using osmosis'),
		array('import-osmosis-all', '', 0, 1, 0, 0, 'bool', 'Import using osmosis forever'),

		array('import-hourly', '', 0, 1, 0, 0, 'bool', 'Import hourly diffs'),
		array('import-daily', '', 0, 1, 0, 0, 'bool', 'Import daily diffs'),
		array('import-all', '', 0, 1, 0, 0, 'bool', 'Import all available files'),

		array('import-file', '', 0, 1, 1, 1, 'realpath', 'Re-import data from an OSM file'),
		array('import-diff', '', 0, 1, 1, 1, 'realpath', 'Import a diff (osc) file from local file system'),

		array('import-node', '', 0, 1, 1, 1, 'int', 'Re-import node'),
		array('import-way', '', 0, 1, 1, 1, 'int', 'Re-import way'),
		array('import-relation', '', 0, 1, 1, 1, 'int', 'Re-import relation'),

		array('index', '', 0, 1, 0, 0, 'bool', 'Index'),
		array('index-rank', '', 0, 1, 1, 1, 'int', 'Rank to start indexing from'),
		array('index-instances', '', 0, 1, 1, 1, 'int', 'Number of indexing instances (threads)'),
		array('index-estrate', '', 0, 1, 1, 1, 'int', 'Estimated indexed items per second (def:30)'),

		array('deduplicate', '', 0, 1, 0, 0, 'bool', 'Deduplicate tokens'),
	);
	getCmdOpt($_SERVER['argv'], $aCMDOptions, $aResult, true, true);

	if ($aResult['import-hourly'] + $aResult['import-daily'] + isset($aResult['import-diff']) > 1)
	{
		showUsage($aCMDOptions, true, 'Select either import of hourly or daily');
	}

	if (!isset($aResult['index-instances'])) $aResult['index-instances'] = 1;

	// Lock to prevent multiple copies running
	if (exec('/bin/ps uww | grep '.basename(__FILE__).' | grep -v /dev/null | grep -v grep -c', $aOutput2, $iResult) > 1)
	{
		echo "Copy already running\n";
		exit;
	}

	if (!isset($aResult['max-load'])) $aResult['max-load'] = 1.9;
	if (!isset($aResult['max-blocking'])) $aResult['max-blocking'] = 3;

	if (getBlockingProcesses() > $aResult['max-blocking'])
	{
		echo "Too many blocking processes for import\n";
		exit;
	}

	// Assume osm2pgsql is in the folder above
	$sBasePath = dirname(dirname(__FILE__));

	require_once('DB.php');
	$oDB =& DB::connect($sDatabaseDSN.'?new_link=true', array('persistent' => false));
	if (PEAR::IsError($oDB))
	{
		echo $oDB->getMessage()."\n";
		exit;
	} 
	$oDB->setFetchMode(DB_FETCHMODE_ASSOC);
	$oDB->query("SET DateStyle TO 'sql,european'");
	$oDB->query("SET client_encoding TO 'utf-8'");

	$bFirst = true;
	$bContinue = $aResult['import-all'];
	while ($bContinue || $bFirst)
	{
		$bFirst = false;

		if ($aResult['import-hourly'])
		{
			// Mirror the hourly diffs
			exec('wget --quiet --mirror -l 1 -P '.$sMirrorDir.' http://planet.openstreetmap.org/hourly');
			$sNextFile = $oDB->getOne('select TO_CHAR(lastimportdate,\'YYYYMMDDHH24\')||\'-\'||TO_CHAR(lastimportdate+\'1 hour\'::interval,\'YYYYMMDDHH24\')||\'.osc.gz\' from import_status');
			$sNextFile = $sMirrorDir.'planet.openstreetmap.org/hourly/'.$sNextFile;
			$sUpdateSQL = 'update import_status set lastimportdate = lastimportdate+\'1 hour\'::interval';
		}

		if ($aResult['import-daily'])
		{
			// Mirror the daily diffs
			exec('wget --quiet --mirror -l 1 -P '.$sMirrorDir.' http://planet.openstreetmap.org/daily');
			$sNextFile = $oDB->getOne('select TO_CHAR(lastimportdate,\'YYYYMMDD\')||\'-\'||TO_CHAR(lastimportdate+\'1 day\'::interval,\'YYYYMMDD\')||\'.osc.gz\' from import_status');
			$sNextFile = $sMirrorDir.'planet.openstreetmap.org/daily/'.$sNextFile;
			$sUpdateSQL = 'update import_status set lastimportdate = lastimportdate::date + 1';
		}
		
		if (isset($aResult['import-diff']))
		{
			// import diff directly (e.g. from osmosis --rri)
			$sNextFile = $aResult['import-diff'];
			if (!file_exists($nextFile))
			{
				echo "Cannot open $nextFile\n";
				exit;
			}
			// Don't update the import status - we don't know what this file contains
			$sUpdateSQL = 'update import_status set lastimportdate = now() where false';
		}

		// Missing file is not an error - it might not be created yet
		if (($aResult['import-hourly'] || $aResult['import-daily']) && file_exists($sNextFile))
		{
			// Import the file
			$sCMD = $sBasePath.'/osm2pgsql -las -C 2000 -O gazetteer -d '.$sDatabaseName.' '.$sNextFile;
			echo $sCMD."\n";
			exec($sCMD, $sJunk, $iErrorLevel);

			if ($iErrorLevel)
			{
				echo "Error from $sBasePath/osm2pgsql -las -C 2000 -O gazetteer -d '.$sDatabaseName.' $sNextFile, $iErrorLevel\n";
				exit;
			}
	
			// Move the date onwards
			$oDB->query($sUpdateSQL);
		}
		else
		{
			$bContinue = false;
		}
	}

	$sModifyXML = false;
	if (isset($aResult['import-file']) && $aResult['import-file'])
	{
		$sModifyXML = file_get_contents($aResult['import-file']);
	}
	if (isset($aResult['import-node']) && $aResult['import-node'])
	{
		$sModifyXML = file_get_contents('http://www.openstreetmap.org/api/0.6/node/'.$aResult['import-node']);
	}
	if (isset($aResult['import-way']) && $aResult['import-way'])
	{
		$sModifyXML = file_get_contents('http://www.openstreetmap.org/api/0.6/way/'.$aResult['import-way'].'/full');
	}
	if (isset($aResult['import-relation']) && $aResult['import-relation'])
	{
		$sModifyXML = file_get_contents('http://www.openstreetmap.org/api/0.6/relation/'.$aResult['import-relation'].'/full');
	}
	if ($sModifyXML)
	{
		// Hack into a modify request
		$sModifyXML = str_replace('<osm version="0.6" generator="OpenStreetMap server">',
			'<osmChange version="0.6" generator="OpenStreetMap server"><modify>', $sModifyXML);
		$sModifyXML = str_replace('</osm>', '</modify></osmChange>', $sModifyXML);

		// Outputing this is too verbose
		if ($aResult['verbose'] && false) var_dump($sModifyXML);

		$aSpec = array(
			0 => array("pipe", "r"),  // stdin
			1 => array("pipe", "w"),  // stdout
			2 => array("pipe", "w") // stderr
		);
		$aPipes = array();
		$hProc = proc_open($sBasePath.'/osm2pgsql -las -C 2000 -O gazetteer -d '.$sDatabaseName.' -', $aSpec, $aPipes);
		if (!is_resource($hProc))
		{
			echo "$sBasePath/osm2pgsql failed\n";
			exit;	
		}
		fwrite($aPipes[0], $sModifyXML);
		fclose($aPipes[0]);
		$sOut = stream_get_contents($aPipes[1]);
		if ($aResult['verbose']) echo $sOut;
		fclose($aPipes[1]);
		$sErrors = stream_get_contents($aPipes[2]);
		if ($aResult['verbose']) echo $sErrors;
		fclose($aPipes[2]);
		if ($iError = proc_close($hProc))
		{
			echo "osm2pgsql existed with error level $iError\n";
			echo $sOut;
			echo $sError;
			exit;
		}
	}

	if ($aResult['deduplicate'])
	{
		$sSQL = "select word_token,count(*) from word where substr(word_token, 1, 1) = ' ' and class is null and type is null and country_code is null group by word_token having count(*) > 1 order by word_token";
		$aDuplicateTokens = $oDB->getAll($sSQL);
		foreach($aDuplicateTokens as $aToken)
		{
			if (trim($aToken['word_token']) == '' || trim($aToken['word_token']) == '-') continue;
			echo "Deduping ".$aToken['word_token']."\n";
			$sSQL = "select word_id,(select count(*) from search_name where nameaddress_vector @> ARRAY[word_id]) as num from word where word_token = '".$aToken['word_token']."' and class is null and type is null and country_code is null order by num desc";
			$aTokenSet = $oDB->getAll($sSQL);
			if (PEAR::isError($aTokenSet))
			{
				var_dump($aTokenSet, $sSQL);
				exit;
			}
			
			$aKeep = array_shift($aTokenSet);
			$iKeepID = $aKeep['word_id'];

			foreach($aTokenSet as $aRemove)
			{
				$sSQL = "update search_name set";
				$sSQL .= " name_vector = (name_vector - ".$aRemove['word_id'].")+".$iKeepID.",";
				$sSQL .= " nameaddress_vector = (nameaddress_vector - ".$aRemove['word_id'].")+".$iKeepID;
				$sSQL .= " where name_vector @> ARRAY[".$aRemove['word_id']."]";
				$x = $oDB->query($sSQL);
				if (PEAR::isError($x))
				{
					var_dump($x);
					exit;
				}

				$sSQL = "update search_name set";
				$sSQL .= " nameaddress_vector = (nameaddress_vector - ".$aRemove['word_id'].")+".$iKeepID;
				$sSQL .= " where nameaddress_vector @> ARRAY[".$aRemove['word_id']."]";
				$x = $oDB->query($sSQL);
				if (PEAR::isError($x))
				{
					var_dump($x);
					exit;
				}

				$sSQL = "delete from word where word_id = ".$aRemove['word_id'];
				$x = $oDB->query($sSQL);
				if (PEAR::isError($x))
				{
					var_dump($x);
					exit;
				}
			}

		}
	}

	if ($aResult['index'])
	{
		index($aResult, $sDatabaseDSN);
	}

	if ($aResult['import-osmosis'] || $aResult['import-osmosis-all'])
	{
		$sImportFile = $sBasePath.'/osmosischange.osc';
		$sCMDDownload = $sOsmosisCMD.' --read-replication-interval workingDirectory='.$sOsmosisConfigDirectory.' --simplify-change --write-xml-change '.$sImportFile;
		$sCMDImport = $sBasePath.'/osm2pgsql -las -C 2000 -O gazetteer -d '.$sDatabaseName.' '.$sImportFile;
		$sCMDIndex = $sBasePath.'/gazetteer/util.update.php --index --index-instances 2 --max-load 20 --max-blocking 10';
		while(true)
		{
			$fStartTime = time();
			$iFileSize = 0;
			
			if (!file_exists($sImportFile))
			{
				// Use osmosis to download the file
				$fCMDStartTime = time();
				echo $sCMDDownload."\n";
				exec($sCMDDownload, $sJunk, $iErrorLevel);
				if ($iErrorLevel)
				{
					echo "Error: $iErrorLevel\n";
					exit;
				}
				$iFileSize = filesize($sImportFile);
				$sBatchEnd = getosmosistimestamp($sOsmosisConfigDirectory);
				echo "Completed for $sBatchEnd in ".round((time()-$fCMDStartTime)/60,2)." minutes\n";
				$sSQL = "INSERT INTO import_osmosis_log values ('$sBatchEnd',$iFileSize,'".date('Y-m-d H:i:s',$fCMDStartTime)."','".date('Y-m-d H:i:s')."','osmosis')";
				$oDB->query($sSQL);
			}

			$iFileSize = filesize($sImportFile);
			$sBatchEnd = getosmosistimestamp($sOsmosisConfigDirectory);
		
			// Import the file
			$fCMDStartTime = time();
			echo $sCMDImport."\n";
			exec($sCMDImport, $sJunk, $iErrorLevel);
			if ($iErrorLevel)
			{
				echo "Error: $iErrorLevel\n";
				exit;
			}
			echo "Completed for $sBatchEnd in ".round((time()-$fCMDStartTime)/60,2)." minutes\n";
			$sSQL = "INSERT INTO import_osmosis_log values ('$sBatchEnd',$iFileSize,'".date('Y-m-d H:i:s',$fCMDStartTime)."','".date('Y-m-d H:i:s')."','osm2pgsql')";
			var_Dump($sSQL);
			$oDB->query($sSQL);

			// Archive for debug?
			unlink($sImportFile);

			// Index file
			$fCMDStartTime = time();
			index($aResult, $sDatabaseDSN);

			// Force a new database connection - problem where one of the other treads closes our connection
			// which shouldn't be possible be still seems to happen!
			$oDB =& DB::connect($sDatabaseDSN.'?new_link=true', array('persistent' => false));
			if (PEAR::IsError($oDB))
			{
				echo $oDB->getMessage()."\n";
				exit;
			} 
			$oDB->setFetchMode(DB_FETCHMODE_ASSOC);
			$oDB->query("SET DateStyle TO 'sql,european'");
			$oDB->query("SET client_encoding TO 'utf-8'");

			echo "Completed for $sBatchEnd in ".round((time()-$fCMDStartTime)/60,2)." minutes\n";
			$sSQL = "INSERT INTO import_osmosis_log values ('$sBatchEnd',$iFileSize,'".date('Y-m-d H:i:s',$fCMDStartTime)."','".date('Y-m-d H:i:s')."','index')";
			$oDB->query($sSQL);

			$fDuration = time() - $fStartTime;
			echo "Completed for $sBatchEnd in ".round($fDuration/60,2)."\n";
			if (!$aResult['import-osmosis-all']) exit;

			echo "Sleeping ".max(0,600-$fDuration)." seconds\n";
			sleep(max(0,600-$fDuration));
		}
		
	}

function getosmosistimestamp($sOsmosisConfigDirectory)
{
	$sStateFile = file_get_contents($sOsmosisConfigDirectory.'/state.txt');
	preg_match('#timestamp=(.+)#', $sStateFile, $aResult);
	return str_replace('\:',':',$aResult[1]);
}

function index($aResult, $sDatabaseDSN)
{
	$oDB =& DB::connect($sDatabaseDSN.'?new_link=true', array('persistent' => false));
	if (PEAR::IsError($oDB))
	{
		echo $oDB->getMessage()."\n";
		exit;
	} 
	$oDB->setFetchMode(DB_FETCHMODE_ASSOC);
	$oDB->query("SET DateStyle TO 'sql,european'");
	$oDB->query("SET client_encoding TO 'utf-8'");

	if (!isset($aResult['index-estrate']) || $aResult['index-estrate'] < 1) $aResult['index-estrate'] = 30;

	if (getBlockingProcesses() > $aResult['max-blocking'])
	{
		echo "Too many blocking processes for index\n";
		exit;
	}

	if ($aResult['index-instances'] > 1)
	{
		$aInstances = array();
		for ($iInstance = 0; $iInstance < $aResult['index-instances']; $iInstance++)
		{
			$aInstances[$iInstance] = array('iPID' => null, 'hSocket' => null, 'bBusy' => false);
			socket_create_pair(AF_UNIX, SOCK_STREAM, 0, $aSockets);
			$iPID = pcntl_fork();
			if ($iPID == -1) die('Could not fork Process');
			if (!$iPID)
			{
				// THIS IS THE CHILD PROCESS

				// Reconnect to database - reusing the same connection is not a good idea!
				$oDBChild =& DB::connect($sDatabaseDSN.'?new_link=true', array('persistent' => false));
				if (PEAR::IsError($oDBChild))
				{
					echo $oDBChild->getMessage()."\n";
					exit;
				} 
				$oDBChild->setFetchMode(DB_FETCHMODE_ASSOC);
				$oDBChild->query("SET DateStyle TO 'sql,european'");
				$oDBChild->query("SET client_encoding TO 'utf-8'");

				socket_close($aSockets[1]);
				$hSocket = $aSockets[0];
				while($sRead = socket_read($hSocket, 1000, PHP_BINARY_READ))
				{
					if ($sRead == 'EXIT') break;
					$aSector = unserialize($sRead);
					echo " - $iInstance processing ".$aSector['geometry_index']."\n";
					indexSector($oDBChild, $aSector, $aResult['max-blocking'], $aResult['max-load']);
					socket_write($hSocket, 'DONE');
					echo " - $iInstance finished ".$aSector['geometry_index']."\n";
				}
				socket_close($hSocket);
				exit;

				// THIS IS THE END OF THE CHILD PROCESS
			}
			$aInstances[$iInstance]['iPID'] = $iPID;
			socket_close($aSockets[0]);
			socket_set_nonblock($aSockets[1]);
			$aInstances[$iInstance]['hSocket'] = $aSockets[1];
		}
	}

	// Re-index the new items
	if (!isset($aResult['index-rank']) || !$aResult['index-rank']) $aResult['index-rank'] = 0;
	for ($i = $aResult['index-rank']; $i <= 30; $i++)
	{ 
		echo "Rank: $i";

		$iStartTime = date('U');
		flush();
			
		$sSQL = 'select geometry_index(geometry,indexed,name),count(*) from placex where rank_search = '.$i.' and indexed = false and name is not null group by geometry_index(geometry,indexed,name)';
		$sSQL .= ' order by count desc';
		$aAllSectors = $oDB->getAll($sSQL);
		if (PEAR::isError($aAllSectors))
		{
			var_dump($aAllSectors);
			exit;
		}
		$iTotalNum = 0;
		foreach($aAllSectors as $aSector)
		{
			$iTotalNum += $aSector['count'];
		}
		$iTotalLeft = $iTotalNum;

		echo ", total to do: $iTotalNum \n";
		flush();

		$fRankStartTime = time();
		$iTotalDone = 0;
		$fRate = $aResult['index-estrate'];

		foreach($aAllSectors as $aSector)
		{
			$aSector['rank'] = $i;

			if ($aSector['rank'] == 21 && $aSector['geometry_index'] == 617467)
			{
				echo "Skipping sector 617467 @ 21 due to issue\n";
				continue;
			}

			while (getBlockingProcesses() > $aResult['max-blocking'] || getLoadAverage() > $aResult['max-load'])
			{
				echo "System busy, pausing indexing...\n";
				sleep(60);
			}

			$iEstSeconds = (int)($iTotalLeft / $fRate);
			$iEstDays = floor($iEstSeconds / (60*60*24));
			$iEstSeconds -= $iEstDays*(60*60*24);
			$iEstHours = floor($iEstSeconds / (60*60));
			$iEstSeconds -= $iEstHours*(60*60);
			$iEstMinutes = floor($iEstSeconds / (60));
			$iEstSeconds -= $iEstMinutes*(60);
			$iNum = $aSector['count'];
			$sRate = round($fRate, 1);
			echo $aSector['geometry_index'].": $iNum, $iTotalLeft left.  Est. time remaining (rank $i) d:$iEstDays h:$iEstHours m:$iEstMinutes s:$iEstSeconds @ $sRate per second\n";
			flush();

			if ($aResult['index-instances'] > 1)
			{
				// Wait for a non-busy socket
				while(true)
				{
					for ($iInstance = 0; $iInstance < $aResult['index-instances']; $iInstance++)
					{
						if (!$aInstances[$iInstance]['bBusy']) break 2;
						$sRead = socket_read($aInstances[$iInstance]['hSocket'], 10, PHP_BINARY_READ);
						if ($sRead == 'DONE')
						{
							$aInstances[$iInstance]['bBusy'] = false;
							$iTotalDone += $iNum;
							$iTotalLeft -= $iNum;
							break 2;
						}
					}
					usleep(1000);
				}
				echo "Dispatch to $iInstance (".$aInstances[$iInstance]['iPID'].")\n";
				socket_write($aInstances[$iInstance]['hSocket'], serialize($aSector));
				$aInstances[$iInstance]['bBusy'] = true;
			}
			else
			{
				indexSector($oDB, $aSector, $aResult['max-blocking'], $aResult['max-load']);
				$iTotalDone += $iNum;
				$iTotalLeft -= $iNum;
			}

			$fDuration = time() - $fRankStartTime;
			if ($fDuration)
				$fRate = $iTotalDone / $fDuration;
			else
				$fRate = $aResult['index-estrate'];
		}

		if ($aResult['index-instances'] > 1)
		{
			// Wait for a non-busy socket
			$bPending = true;
			while($bPending)
			{
				$bPending = false;
				for ($iInstance = 0; $iInstance < $aResult['index-instances']; $iInstance++)
				{
					if ($aInstances[$iInstance]['bBusy'])
					{
						if (socket_read($aInstances[$iInstance]['hSocket'], 10, PHP_BINARY_READ) == 'DONE')
						{
							$aInstances[$iInstance]['bBusy'] = false;
						}
						else
						{
							$bPending = true;
						}
					}
				}
				if ($bPending) usleep(1000000);
			}
		}
			
		$iDuration = date('U') - $iStartTime;
		if ($iDuration && $iTotalNum)
		{
			echo "Finished Rank: $i in $iDuration @ ".($iTotalNum / $iDuration)." per second\n";
		}
	}

	if ($aResult['index-instances'] > 1)
	{
		for ($iInstance = 0; $iInstance < $aResult['index-instances']; $iInstance++)
		{
			socket_write($aInstances[$iInstance]['hSocket'], 'EXIT');
			socket_close($aInstances[$iInstance]['hSocket']);
		}
	}
}

function indexSector($oDB, $aSector, $fMaxBlocking, $fMaxLoad)
{
	$iNum = $aSector['count'];
	$iRank = $aSector['rank'];

	$fNumSteps = ceil(sqrt($iNum) / 10);
	$iNumSteps = $fNumSteps*$fNumSteps;

	if ($fNumSteps > 1 )
	{
		$iStepNum = 1;
		echo "Spliting into ".($fNumSteps*$fNumSteps)." steps\n";

		// Convert sector number back to lat lon
		$fLon = (500 - floor($aSector['geometry_index']/1000)) - 0.5;
		$fLat = (500 -  $aSector['geometry_index']%1000) - 0.5;

		$fStepSize = 1 / $fNumSteps;
		for ($fStepLat = $fLat; $fStepLat < ($fLat + 1); $fStepLat += $fStepSize)
		{
			for ($fStepLon = $fLon; $fStepLon < ($fLon + 1); $fStepLon += $fStepSize)
			{
				while (getBlockingProcesses() > $fMaxBlocking || getLoadAverage() > $fMaxLoad)
				{
					echo "System busy, pausing indexing...\n";
					sleep(60);
				}

				$fStepLonTop = $fStepLon + $fStepSize;
				$fStepLatTop = $fStepLat + $fStepSize;
				echo "  Step $iStepNum of $iNumSteps: ($fStepLon,$fStepLat,$fStepLonTop,$fStepLatTop)\n";
				$sSQL = 'update placex set indexed = true where geometry_index(geometry,indexed,name) = '.$aSector['geometry_index'].' and rank_search = '.$iRank;
				$sSQL .= " and ST_Contains(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($fStepLon,$fStepLat),4326),ST_SetSRID(ST_POINT($fStepLonTop,$fStepLatTop),4326)),4326),geometry)";
				$oDB->query($sSQL);
				$iStepNum++;
			}
		}
	}
	$sSQL = 'update placex set indexed = true where geometry_index(geometry,indexed,name) = '.$aSector['geometry_index'].' and rank_search = '.$iRank;
	$oDB->query($sSQL);
}
