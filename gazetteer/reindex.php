<?php
	require_once('DB.php');
	$oDB =& DB::connect('pgsql://@/gazetteerworld', false);
	$oDB->setFetchMode(DB_FETCHMODE_ASSOC);
	$oDB->query("SET DateStyle TO 'sql,european'");
	$oDB->query("SET client_encoding TO 'utf-8'");

	$iStatus_Ready = 0;
	$iStatus_Done = 10000;

	$iSessionID = 0;
	for($i = 1; $i < $_SERVER['argc']; $i++)
	{
		if ((int)$_SERVER['argv'][$i])
		{
			$iSessionID = (int)$_SERVER['argv'][$i];
		}
	}

	if (!$iSessionID)
	{
		echo "Please enter a id number of the session (1-99) or 100 to reset index.\n";
		exit;
	}

	echo "Session ID: $iSessionID\n";

	if ($iSessionID == 100)
	{
		// 91 & 181
		$x = $oDB->query('delete from updatearea');
		if (PEAR::isError($x))
		{
			echo $x->getMessage();
			exit;
		}
		for ($iLat = -90; $iLat < 90; $iLat++)
		{
			for ($iLon = -181; $iLon < 181; $iLon++)
			{
				$oDB->query('insert into updatearea values ('.$iLat.','.$iLon.',0)');
			}
		}
		exit;
	}

	$oDB->query('update updatearea set status = 0 where status = '.$iSessionID);

$sSQL = "select a5.lat,a5.lon from updatearea as a5 ";
$sSQL .= " join updatearea as a1 on (a1.lat = a5.lat+1 and a1.lon = a5.lon+1) ";
$sSQL .= " join updatearea as a2 on (a2.lat = a5.lat+1 and a2.lon = a5.lon+0) ";
$sSQL .= " join updatearea as a3 on (a3.lat = a5.lat+1 and a3.lon = a5.lon-1) ";
$sSQL .= " join updatearea as a4 on (a4.lat = a5.lat+0 and a4.lon = a5.lon+1) ";
$sSQL .= " join updatearea as a6 on (a6.lat = a5.lat+0 and a6.lon = a5.lon-1) ";
$sSQL .= " join updatearea as a7 on (a7.lat = a5.lat-1 and a7.lon = a5.lon+1) ";
$sSQL .= " join updatearea as a8 on (a8.lat = a5.lat-1 and a8.lon = a5.lon+0) ";
$sSQL .= " join updatearea as a9 on (a9.lat = a5.lat-1 and a9.lon = a5.lon-1) ";
$sSQL .= " where a5.status = 0 ";
$sSQL .= " and a1.status in (0,10000) ";
$sSQL .= " and a2.status in (0,10000) ";
$sSQL .= " and a3.status in (0,10000) ";
$sSQL .= " and a4.status in (0,10000) ";
$sSQL .= " and a6.status in (0,10000) ";
$sSQL .= " and a7.status in (0,10000) ";
$sSQL .= " and a8.status in (0,10000) ";
$sSQL .= " and a9.status in (0,10000) ";
$sSQL .= " limit 1 ";
$sSQLAvail = $sSQL;

	while($oDB->getOne('select count(*) from updatearea where status = '.$iStatus_Ready) > 1080)
	{
		// Allocate a block to run
		$oDB->query($sDebug = 'update updatearea set status = '.$iSessionID.' from ('.$sSQLAvail.') as x where updatearea.lat = x.lat and updatearea.lon = x.lon and updatearea.status = '.$iStatus_Ready);
		$aAllocated = $oDB->getRow('select * from updatearea where status = '.$iSessionID);
		if (!$aAllocated)
  		{
			echo "Unable to allocate\n";
			sleep(1);
		}
		else
		{
			$iLat = $aAllocated['lat']; 
			$iLon = $aAllocated['lon']; 
			$iLonTop = $iLon + 1;
			$iLatTop = $iLat + 1;
			echo "Running ($iLon,$iLat,$iLonTop,$iLatTop) ";
// Testing
//sleep(10);

			$sSQL = "select count(*) from placex where name is not null AND";
			$sSQL .= " (ST_Contains(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($iLon,$iLat),4326), ST_SetSRID(ST_POINT($iLonTop,$iLatTop),4326)),4326),geometry)";
			$sSQL .= " OR ST_Intersects(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($iLon,$iLat),4326), ST_SetSRID(ST_POINT($iLonTop,$iLatTop),4326)),4326),geometry))";
			$iCount = $oDB->getOne($sSQL);
			echo "Batch of $iCount @ ".$oDB->getOne('select \'now\'::timestamp')."\n";
			if ($iCount)
			{
				$fNumSteps = round(sqrt($iCount) / 100);

				if ($fNumSteps > 1 )
				{
					$fStepSize = 1 / $fNumSteps;
					for ($iStepLat = $iLat; $iStepLat <= ($iLatTop - $fStepSize); $iStepLat += $fStepSize)
					{
						$iStepLatTop = $iStepLat + $fStepSize;
						for ($iStepLon = $iLon; $iStepLon <= ($iLonTop - $fStepSize); $iStepLon += $fStepSize)
						{
							$iStepLonTop = $iStepLon + $fStepSize;
							echo "  Step1 ($iStepLon,$iStepLat,$iStepLonTop,$iStepLatTop)\n";
				$sSQL = "update placex set indexed = true where not indexed and rank_search <= 26 and name is not null AND";
				$sSQL .= " (ST_Contains(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($iStepLon,$iStepLat),4326), ST_SetSRID(ST_POINT($iStepLonTop,$iStepLatTop),4326)),4326),geometry)";
				$sSQL .= " OR ST_Intersects(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($iStepLon,$iStepLat),4326), ST_SetSRID(ST_POINT($iStepLonTop,$iStepLatTop),4326)),4326),geometry))";
//var_dump($sSQL);
				$oDB->query($sSQL);
						}
					}

				$sSQL = "update placex set indexed = true where not indexed and rank_search <= 26 and name is not null AND";
				$sSQL .= " (ST_Contains(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($iLon,$iLat),4326), ST_SetSRID(ST_POINT($iLonTop,$iLatTop),4326)),4326),geometry)";
				$sSQL .= " OR ST_Intersects(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($iLon,$iLat),4326), ST_SetSRID(ST_POINT($iLonTop,$iLatTop),4326)),4326),geometry))";
//var_dump($sSQL);
				$oDB->query($sSQL);

					for ($iStepLat = $iLat; $iStepLat <= ($iLatTop - $fStepSize); $iStepLat += $fStepSize)
					{
						$iStepLatTop = $iStepLat + $fStepSize;
						for ($iStepLon = $iLon; $iStepLon <= ($iLonTop - $fStepSize); $iStepLon += $fStepSize)
						{
							$iStepLonTop = $iStepLon + $fStepSize;
							echo "  Step2 ($iStepLon,$iStepLat,$iStepLonTop,$iStepLatTop)\n";
				$sSQL = "update placex set indexed = true where not indexed and rank_search > 26 and name is not null AND";
				$sSQL .= " (ST_Contains(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($iStepLon,$iStepLat),4326), ST_SetSRID(ST_POINT($iStepLonTop,$iStepLatTop),4326)),4326),geometry)";
				$sSQL .= " OR ST_Intersects(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($iStepLon,$iStepLat),4326), ST_SetSRID(ST_POINT($iStepLonTop,$iStepLatTop),4326)),4326),geometry))";
//var_dump($sSQL);
				$oDB->query($sSQL);
						}
					}
				}

				$sSQL = "update placex set indexed = true where not indexed and rank_search <= 26 and name is not null AND";
				$sSQL .= " (ST_Contains(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($iLon,$iLat),4326), ST_SetSRID(ST_POINT($iLonTop,$iLatTop),4326)),4326),geometry)";
				$sSQL .= " OR ST_Intersects(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($iLon,$iLat),4326), ST_SetSRID(ST_POINT($iLonTop,$iLatTop),4326)),4326),geometry))";
//var_dump($sSQL);
				$oDB->query($sSQL);

				$sSQL = "update placex set indexed = true where not indexed and rank_search > 26 and name is not null AND";
				$sSQL .= " (ST_Contains(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($iLon,$iLat),4326), ST_SetSRID(ST_POINT($iLonTop,$iLatTop),4326)),4326),geometry)";
				$sSQL .= " OR ST_Intersects(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($iLon,$iLat),4326), ST_SetSRID(ST_POINT($iLonTop,$iLatTop),4326)),4326),geometry))";
//var_dump($sSQL);
				$oDB->query($sSQL);

				$oDB->query('update updatearea set status = '.$iStatus_Done.' where status = '.$iSessionID);
/*
				$sSQL = "select count(*) from placex where not indexed AND rank_search <= 26 AND name is not null AND street IS NULL AND";
				$sSQL .= " (ST_Contains(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($iLon,$iLat),4326), ST_SetSRID(ST_POINT($iLonTop,$iLatTop),4326)),4326),geometry)";
				$sSQL .= " OR ST_Intersects(ST_SetSRID(ST_MakeBox2D(ST_SetSRID(ST_POINT($iLon,$iLat),4326), ST_SetSRID(ST_POINT($iLonTop,$iLatTop),4326)),4326),geometry))";
				if ($oDB->getOne($sSQL) == 0)
 				{
				}
				else
				{
					echo "Fail while running ($iLon,$iLat,$iLonTop,$iLatTop)\n";
var_Dump($sSQL);
					exit;
				}
*/
			}
			else
			{
				$oDB->query('update updatearea set status = '.$iStatus_Done.' where status = '.$iSessionID);
			}
		}
	}
