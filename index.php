<?php

	// Check if the script is triggered due to upload from Arduino, or due
	// to page load from browser
	if (isset($_POST['temperature']))
	{
		// HTTP POST from Arduino with temperature/humidity data
		file_put_contents("post.log", print_r( $_POST, true ));

		$temperature       = $_POST['temperature'];
		$tempFarenheit     = $_POST['tempFar'];
		$timeStamp         = $_POST['timeStamp'];
		$humidity          = $_POST['humidity'];
		$pressure          = $_POST['pressure'];
		$ldr               = $_POST['ldr'];
		$maxTemp           = $_POST['maxTemp'];
		$minTemp           = $_POST['minTemp'];
		$maxTempTimestamp  = $_POST['maxTempTimestamp'];
		$minTempTimestamp  = $_POST['minTempTimestamp'];
		$minHumdity        = $_POST['minHumidity'];
		$maxHumdity        = $_POST['maxHumidity'];
		$maxHumTimestamp   = $_POST['maxHumidityTimestamp'];
		$minHumTimestamp   = $_POST['minHumidityTimestamp'];
		$minPressure       = $_POST['minPressure'];
		$maxPressure       = $_POST['maxPressure'];
		$maxPressTimestamp = $_POST['maxPressureTimestamp'];
		$minPressTimestamp = $_POST['minPressureTimestamp'];
		$uptime            = $_POST['uptime'];
		$approxDewPoint    = $_POST['dewPoint'];
		$maxDewPoint       = $_POST['maxDewPoint'];
		$minDewPoint       = $_POST['minDewPoint'];
		$maxDPTimestamp    = $_POST['maxDewPointTimestamp'];
		$minDPTimestamp    = $_POST['minDewPointTimestamp'];
		$dewPointFeeling   = $_POST['dewPointFeeling'];
		$sunriseTime       = $_POST['sunrise'];
		$sunsetTime        = $_POST['sunset'];
		$sunriseTomorrow   = $_POST['sunriseTomorrow'];
		$sunsetTomorrow    = $_POST['sunsetTomorrow'];

		$fh = fopen("temp_humidity.txt", 'w+');
		if (!$fh)
		{
			echo '<html><body><p>Failed to open file!</body></html>';
			exit;
		}

		date_default_timezone_set("UTC");
		$timeStampString = date("H:i:s j-M-Y (D)",$timeStamp);
		date_default_timezone_set("Australia/Sydney");
		$timeStampStringSydney  = date("H:i:s j-M-Y (D)",$timeStamp);
		$timeStampStringMaxTemp = date("H:i:s j-M-Y (D)",$maxTempTimestamp);
		$timeStampStringMinTemp = date("H:i:s j-M-Y (D)",$minTempTimestamp);
		$timeStampStringMaxHum  = date("H:i:s j-M-Y (D)",$maxHumTimestamp);
		$timeStampStringMinHum  = date("H:i:s j-M-Y (D)",$minHumTimestamp);
		$timeStampStringMaxPres = date("H:i:s j-M-Y (D)",$maxPressTimestamp);
		$timeStampStringMinPres = date("H:i:s j-M-Y (D)",$minPressTimestamp);
		$timeStampStringRestart = date("H:i:s j-M-Y (D)",$uptime);
		$timeStampStringMaxDP   = date("H:i:s j-M-Y (D)",$maxDPTimestamp);
		$timeStampStringMinDP   = date("H:i:s j-M-Y (D)",$minDPTimestamp);

		$outputString = "Sydney time:       " .	$timeStampStringSydney . "\n";
		fwrite($fh, $outputString, strlen($outputString));
		$outputString = "UTC time:          " .	$timeStampString . "\n\n";
		fwrite($fh, $outputString, strlen($outputString));

		$outputString = "Sunrise today:     " . $sunriseTime . "\n";
		fwrite($fh, $outputString, strlen($outputString));
		$outputString = "Sunset today:      " . $sunsetTime . "\n";
		fwrite($fh, $outputString, strlen($outputString));
		$outputString = "Sunrise tomorrow:  " . $sunriseTomorrow . "\n";
		fwrite($fh, $outputString, strlen($outputString));
		$outputString = "Sunset tomorrow:   " . $sunsetTomorrow . "\n\n";
		fwrite($fh, $outputString, strlen($outputString));

		$outputString = "Temperature:       " . $temperature . "&degC, " . $tempFarenheit . "&degF\n";
		fwrite($fh, $outputString, strlen($outputString));
	    $outputString = "Max. Temperature:  " . $maxTemp . "&degC @ " . $timeStampStringMaxTemp ."\n";
	    fwrite($fh, $outputString, strlen($outputString));
	    $outputString = "Min. Temperature:  " . $minTemp . "&degC @ " . $timeStampStringMinTemp ."\n\n";
	    fwrite($fh, $outputString, strlen($outputString));

		$outputString = "Humidity:          " . $humidity . "%\n";
		fwrite($fh, $outputString, strlen($outputString));
		$outputString = "Max. Humidity:     " . $maxHumdity . "% @ " . $timeStampStringMaxHum ."\n";
		fwrite($fh, $outputString, strlen($outputString));
		$outputString = "Min. Humidity:     " . $minHumdity . "% @ " . $timeStampStringMinHum ."\n\n";
		fwrite($fh, $outputString, strlen($outputString));

		$outputString = "Approx. Dew Point: " . $approxDewPoint . "&degC " . $dewPointFeeling . "\n";
		fwrite($fh, $outputString, strlen($outputString));
	    $outputString = "Max. Dew Point:    " . $maxDewPoint . "&degC @ " . $timeStampStringMaxDP ."\n";
	    fwrite($fh, $outputString, strlen($outputString));
	    $outputString = "Min. Dew Point:    " . $minDewPoint . "&degC @ " . $timeStampStringMinDP ."\n\n";
	    fwrite($fh, $outputString, strlen($outputString));

		$outputString = "Pressure:          " . $pressure . "hPa\n";
		fwrite($fh, $outputString, strlen($outputString));
		$outputString = "Max. Pressure:     " . $maxPressure . "hPa @ " . $timeStampStringMaxPres ."\n";
		fwrite($fh, $outputString, strlen($outputString));
		$outputString = "Min. Pressure:     " . $minPressure . "hPa @ " . $timeStampStringMinPres ."\n\n";
		fwrite($fh, $outputString, strlen($outputString));

		$outputString = "Ambient Light:     " . $ldr . "/1023\n";
		fwrite($fh, $outputString, strlen($outputString));
		$outputString = "Arduino Restart:   " . $timeStampStringRestart . "\n";
		fwrite($fh, $outputString, strlen($outputString));

	} // POST from Arduino
	else
	{
		// Browser is trying to load me, read data from the file and output
		$inputString = file_get_contents('temp_humidity.txt');
		echo <<<_END
		<head>
			<title>Sydney - Temperature/Humidity/Pressure</title></head>
			<meta http-equiv="refresh" content="30">
			<body>
				<h2>Location: North West Sydney, NSW, Australia</h2>
				<h2>Garage/Nerd Grotto</h2>
				<pre>
				<p>$inputString</p>
				</pre>
				<p>The Arduino tries to send data to the server every minute (see timestamp).</br>
				   Timestamp of data is local Sydney time - UTC+10 or UTC+11 (DST).</br>
				   DST runs from first Sunday of October @ 02:00 until first Sunday of April @ 03:00.</br>
                   Max/Min readings reset every Sunday at midnight.</br>
                   Approximate <a href="http://www.shorstmeyer.com/wxfaqs/humidity/humidity.html">dew point</a> is <a href="https://ag.arizona.edu/azmet/dewpoint.html">calculated</a> using the Magnus formula.</br>
                   (Fog <a href="https://en.wikipedia.org/wiki/Dew_point">forms</a> when the ambient temperature is around the dew point)</br>
                   Sunrise/Sunset times are <a href="http://williams.best.vwh.net/sunrise_sunset_algorithm.htm">estimated</a> and recalculated at midnight.</br>
           </br>
				   This page will automatically refresh every 30 seconds.</p>
			</body>
		</html>
_END;
	} // page load from browser
?>