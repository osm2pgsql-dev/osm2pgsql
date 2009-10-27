<?php

	$bHourly = false;
	$bImport = false;
	$bIndex = true;

	function getBlockingProcesses()
	{
		if (!preg_match('/procs_blocked ([0-9]+)/i', file_get_contents('/proc/stat'), $aBlocking))
		{
			echo "No process status\n";
			exit;
		}
		return $aBlocking[1];
	}

	// Lock to prevent multiple copies running
	if (exec('/bin/ps uww | grep '.basename(__FILE__).' | grep -v /dev/null | grep -v grep -c', $aOutput2, $iResult) > 1)
	{
		echo "Copy already running\n";
		exit;
	}

	if (getBlockingProcesses() > 1)
	{
		echo "Too many blocking processes for import\n";
		exit;
	}

	if ($bHourly)
	{
		// Mirror the hourly diffs
		exec('wget --quiet --mirror -l 1 -P /home/twain/ http://planet.openstreetmap.org/hourly');
	}
	else
	{
		// Mirror the daily diffs
		exec('wget --quiet --mirror -l 1 -P /home/twain/ http://planet.openstreetmap.org/daily');
	}

	require_once('DB.php');
	$oDB =& DB::connect('pgsql://@/gazetteer', false);
	$oDB->setFetchMode(DB_FETCHMODE_ASSOC);
	$oDB->query("SET DateStyle TO 'sql,european'");
	$oDB->query("SET client_encoding TO 'utf-8'");

	if ($bHourly)
	{
		$sNextFile = $oDB->getOne('select TO_CHAR(lastimportdate,\'YYYYMMDDHH24\')||\'-\'||TO_CHAR(lastimportdate+\'1 hour\'::interval,\'YYYYMMDDHH24\')||\'.osc.gz\' from import_status');
		$sNextFile = '/home/twain/planet.openstreetmap.org/hourly/'.$sNextFile;
	}
	else
	{

		$sNextFile = $oDB->getOne('select TO_CHAR(lastimportdate,\'YYYYMMDD\')||\'-\'||TO_CHAR(lastimportdate+\'1 day\'::interval,\'YYYYMMDD\')||\'.osc.gz\' from import_status');
		$sNextFile = '/home/twain/planet.openstreetmap.org/daily/'.$sNextFile;
	}

	// Missing file is not an error - it might not be created yet
	if ($bImport && file_exists($sNextFile))
	{
		// Import the file
		exec('./osm2pgsql -las -C 2000 -O gazetteer -d gazetteerworld '.$sNextFile);
	
		// More the date onwards
		if ($bHourly)
		{
			$oDB->query('update import_status set lastimportdate = lastimportdate+\'1 hour\'::interval');
		}
		else
		{
			$oDB->query('update import_status set lastimportdate = lastimportdate::date + 1');
		}
	}

	if ($bIndex)
	{
		if (getBlockingProcesses() > 3)
		{
			echo "Too many blocking processes for index\n";
			exit;
		}

		// Re-index the new items
		for ($i = 0; $i <= 30; $i++)
		{ 
			echo "Rank: $i";
			$iStartTime = date('U');
			flush();
			$sSQL = 'select geometry_index(geometry,indexed,name),count(*) from placex where rank_search = '.$i.' and indexed = false and name is not null group by geometry_index(geometry,indexed,name)';
			$aAllSectors = $oDB->getAll($sSQL);
			$iTotalNum = 0;
			foreach($aAllSectors as $aSector)
			{
				$iTotalNum += $aSector['count'];
			}
			$iTotalLeft = $iTotalNum;

//			$sSQL = 'select count(*) from placex where rank_search = '.$i.' and indexed = false and name is not null';
//			$iTotalLeft = $iTotalNum = $oDB->getOne($sSQL);
			echo ", total to do: $iTotalNum \n";
			flush();

			foreach($aAllSectors as $aSector)
			{
				if (getBlockingProcesses() > 3)
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
		}
	}

