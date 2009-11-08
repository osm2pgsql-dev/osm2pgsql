#!/usr/bin/php -Cq
<?php

	require_once('website/.htlib/lib.php');

	$aCMDOptions = array(
		"Import / update / index osm data",
		array('help', 'h', 0, 1, 0, 0, false, 'Show Help'),
		array('quiet', 'q', 0, 1, 0, 0, 'bool', 'Quiet output'),
		array('verbose', 'v', 0, 1, 0, 0, 'bool', 'Verbose output'),

		array('import-hourly', '', 0, 1, 0, 0, 'bool', 'Import hourly diffs'),
		array('import-daily', '', 0, 1, 0, 0, 'bool', 'Import daily diffs'),
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
			exit;
		}
	}

	if (getBlockingProcesses() > 1)
	{
		echo "Too many blocking processes for import\n";
		exit;
	}

	require_once('DB.php');
	$oDB =& DB::connect('pgsql://@/gazetteerworld', false);
	$oDB->setFetchMode(DB_FETCHMODE_ASSOC);
	$oDB->query("SET DateStyle TO 'sql,european'");
	$oDB->query("SET client_encoding TO 'utf-8'");

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
		exec('./osm2pgsql -las -C 2000 -O gazetteer -d gazetteerworld '.$sNextFile);
		// TODO: check for errors!
	
		// Move the date onwards
		$oDB->query($sUpdateSQL);
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
				if (getBlockingProcesses() > 5)
				{
					echo "Too many blocking processes for index\n";
					exit;
				}
				$iNum = $aSector['count'];
				echo $aSector['geometry_index'].": $iNum, $iTotalLeft remaining (".date('H:i:s').")\n";
				$iTotalLeft -= $iNum;
				flush();
				$sSQL = 'update placex set indexed = true where geometry_index(geometry,indexed,name) = '.$aSector['geometry_index'].' and rank_search = '.$i;
//var_dump($sSQL);exit;
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

