#!/usr/bin/php -Cq
<?php
	ini_set('memory_limit', '800M');
	require_once('website/.htlib/lib.php');

	$aCMDOptions = array(
		"Import / update / index osm data",
		array('help', 'h', 0, 1, 0, 0, false, 'Show Help'),
		array('quiet', 'q', 0, 1, 0, 0, 'bool', 'Quiet output'),
		array('verbose', 'v', 0, 1, 0, 0, 'bool', 'Verbose output'),

		array('import-hourly', '', 0, 1, 0, 0, 'bool', 'Import hourly diffs'),
		array('import-daily', '', 0, 1, 0, 0, 'bool', 'Import daily diffs'),
		array('import-all', '', 0, 1, 0, 0, 'bool', 'Import all available files'),

		array('import-file', '', 0, 1, 1, 1, 'realpath', 'Re-import data from an OSM file'),
		array('import-node', '', 0, 1, 1, 1, 'int', 'Re-import node'),
		array('import-way', '', 0, 1, 1, 1, 'int', 'Re-import way'),
		array('import-relation', '', 0, 1, 1, 1, 'int', 'Re-import relation'),

		array('index', '', 0, 1, 0, 0, 'bool', 'Index'),
		array('index-instances', '', 0, 1, 1, 1, 'int', 'Number of indexing instances'),
		array('index-instance', '', 0, 1, 1, 1, 'int', 'Which instance are we (0 to index-instances-1)'),
		array('index-estrate', '', 0, 1, 1, 1, 'int', 'Estimated indexed items per second (def:30)'),
	);
	getCmdOpt($_SERVER['argv'], $aCMDOptions, $aResult, true, true);

	if ($aResult['import-hourly'] && $aResult['import-daily']) showUsage($aCMDOptions, true, 'Select either import of hourly or daily');

	if (!isset($aResult['index-instances']) || $aResult['index-instances'] == 1)
	{
		// Lock to prevent multiple copies running
		if (exec('/bin/ps uww | grep '.basename(__FILE__).' | grep -v /dev/null | grep -v grep -c', $aOutput2, $iResult) > 1)
		{
			echo "Copy already running\n";
//			exit;
		}
	}

	if (getBlockingProcesses() > 2)
	{
		echo "Too many blocking processes for import\n";
		exit;
	}

	// Assume osm2pgsql is in the folder above
	$sBasePath = dirname(dirname(__FILE__));

	require_once('DB.php');
	$oDB =& DB::connect('pgsql://@/gazetteerworld', false);
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
			exec('wget --quiet --mirror -l 1 -P /home/twain/ http://planet.openstreetmap.org/hourly');
			$sNextFile = $oDB->getOne('select TO_CHAR(lastimportdate,\'YYYYMMDDHH24\')||\'-\'||TO_CHAR(lastimportdate+\'1 hour\'::interval,\'YYYYMMDDHH24\')||\'.osc.gz\' from import_status');
			$sNextFile = '/home/twain/planet.openstreetmap.org/hourly/'.$sNextFile;
			$sUpdateSQL = 'update import_status set lastimportdate = lastimportdate+\'1 hour\'::interval';
		}

		if ($aResult['import-daily'])
		{
			// Mirror the daily diffs
			exec('wget --quiet --mirror -l 1 -P /home/twain/ http://planet.openstreetmap.org/daily');
			$sNextFile = $oDB->getOne('select TO_CHAR(lastimportdate,\'YYYYMMDD\')||\'-\'||TO_CHAR(lastimportdate+\'1 day\'::interval,\'YYYYMMDD\')||\'.osc.gz\' from import_status');
			$sNextFile = '/home/twain/planet.openstreetmap.org/daily/'.$sNextFile;
			$sUpdateSQL = 'update import_status set lastimportdate = lastimportdate::date + 1';
		}

		// Missing file is not an error - it might not be created yet
		if (($aResult['import-hourly'] || $aResult['import-daily']) && file_exists($sNextFile))
		{
			// Import the file
			$sCMD = $sBasePath.'/osm2pgsql -las -C 2000 -O gazetteer -d gazetteerworld '.$sNextFile;
			echo $sCMD."\n";
			exec($sCMD, $sJunk, $iErrorLevel);

			if ($iErrorLevel)
			{
				echo "Error from $sBasePath/osm2pgsql -las -C 2000 -O gazetteer -d gazetteerworld $sNextFile, $iErrorLevel\n";
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

		$aSpec = array(
			0 => array("pipe", "r"),  // stdin
			1 => array("pipe", "w"),  // stdout
			2 => array("pipe", "w") // stderr
		);
		$aPipes = array();
		$hProc = proc_open($sBasePath.'/osm2pgsql -las -C 2000 -O gazetteer -d gazetteerworld -', $aSpec, $aPipes);
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

	if ($aResult['index'])
	{
		if (!isset($aResult['index-estrate'])) $aResult['index-estrate'] = 30;

		if (getBlockingProcesses() > 3)
		{
			echo "Too many blocking processes for index\n";
			exit;
		}

		$sModSQL = '';
		if (isset($aResult['index-instances']) && $aResult['index-instances'] > 1)
		{
			$sModSQL = ' and geometry_index(geometry,indexed,name) % '.$aResult['index-instances'].' = '.(int)$aResult['index-instance'].' ';
		}

		// Re-index the new items
		for ($i = 0; $i <= 30; $i++)
		{ 
			echo "Rank: $i";
			$iStartTime = date('U');
			flush();
			
			$sSQL = 'select geometry_index(geometry,indexed,name),count(*) from placex where rank_search = '.$i.' and indexed = false and name is not null '.$sModSQL.'group by geometry_index(geometry,indexed,name)';
			$aAllSectors = $oDB->getAll($sSQL);
			$iTotalNum = 0;
			foreach($aAllSectors as $aSector)
			{
				$iTotalNum += $aSector['count'];
			}
			$iTotalLeft = $iTotalNum;

			echo ", total to do: $iTotalNum \n";
			flush();

			foreach($aAllSectors as $aSector)
			{
				while (getBlockingProcesses() > 3 || getLoadAverage() > 1.9)
				{
					echo "System busy, pausing indexing...\n";
					sleep(60);
				}
				$iNum = $aSector['count'];
				echo $aSector['geometry_index'].": $iNum, $iTotalLeft remaining (".date('H:i:s').")\n";
				$iTotalLeft -= $iNum;
				flush();

                                $fNumSteps = round(sqrt($iNum) / 100);

                                if ($fNumSteps > 1 )
                                {
					echo "$fNumSteps steps\n";
					// Convert sector number back to lat lon
					$fLon = (500 - floor($aSector['geometry_index']/1000)) - 0.5;
					$fLat = (500 -  $aSector['geometry_index']%1000) - 0.5;

                                        $fStepSize = 1 / $fNumSteps;
                                        for ($fStepLat = $fLat; $fStepLat < ($fLat + 1); $fStepLat += $fStepSize)
                                        {
                                                for ($fStepLon = $fLon; $fStepLon < ($fLon + 1); $fStepLon += $fStepSize)
                                                {
                                                        $fStepLonTop = $fStepLon + $fStepSize;
							$fStepLatTop = $fStepLat + $fStepSize;
                                                        echo "  Step1 ($fStepLon,$fStepLat,$fStepLonTop,$fStepLatTop)\n";
							$sSQL = 'update placex set indexed = true where geometry_index(geometry,indexed,name) = '.$aSector['geometry_index'].' and rank_search = '.$i;
							$sSQL .= " and ST_Contains(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($fStepLon,$fStepLat),4326),ST_SetSRID(ST_POINT($fStepLonTop,$fStepLatTop),4326)),4326),geometry)";
//							var_Dump($sSQL);
							$oDB->query($sSQL);
						}
					}
				}
				$sSQL = 'update placex set indexed = true where geometry_index(geometry,indexed,name) = '.$aSector['geometry_index'].' and rank_search = '.$i;
				$oDB->query($sSQL);
			}
			$iDuration = date('U') - $iStartTime;
			if ($iDuration && $iTotalNum)
			{
				echo "Finished Rank: $i in $iDuration @ ".($iTotalNum / $iDuration)." per second\n";
			}

			// Keep in sync with other instances
			if ($aResult['index-instances'])
			{
				$sSQL = 'select count(*) from placex where rank_search = '.$i.' and indexed = false and name is not null';
				while($iWaitNum = $oDB->getOne($sSQL))
				{
					$iEstSleepSeconds = round(max(1,($iWaitNum / $aResult['index-estrate'])/10));
					echo "Waiting for $iWaitNum other places to be indexed at this level, sleeping $iEstSleepSeconds seconds\n";
					sleep($iEstSleepSeconds);
				}
			}
		}
	}

