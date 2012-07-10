
// This function returns a list of all features supported by this script.
function usedTimetableInformations() {
    return [ 'Platform', 'StopID', 'RouteStops', 'RouteTimes' ];
}

function getTimetable( values ) {
    var url = "http://reiseauskunft.insa.de/bin/stboard.exe/dn?ld=web&L=vbn" +
        "&input=" + values.stop +
        "&boardType=" + (values.dataType == "arrivals" ? "arr" : "dep") +
        "&time=" + helper.formatDateTime(values.dateTime, "hh:mm") +
        "&disableEquivs=no" +
        "&maxJourneys=" + values.maxCount + "&start=yes";
    var request = network.createRequest( url );
    request.finished.connect( parseTimetable );
    network.get( request );
}

function typeOfVehicleFromString( string ) {
    string = string.toLowerCase();
    if ( string == "sbahn" ) {
        return "interurbantrain";
    } else if ( string == "ubahn" ) {
        return "subway";
    } else if ( string == "ice" ) {
        return "highspeedtrain";
    } else if ( string == "ic" || string == "ec" ) { // Eurocity
        return "intercitytrain";
    } else if ( string == "re" || string == "me" ) { // Metronom
        return "regionalexpresstrain";
    } else if ( string == "rb" || string == "mer" ) { // Metronom regional
        return "regionaltrain";
    } else if ( string == "ir" ) {
        return "interregionaltrain";
    } else {
        return string;
    }
}

// This function parses a given HTML document for departure/arrival data.
function parseTimetable( html ) {
    // Check for errors
    var errorRegExp = /<div class="errorBox">\s*<h1>Error!<\/h1>\s*<p>([^<]*)<\/p>/i;
    if ( (error = errorRegExp.exec(html)) ) {
        helper.error( "Got an error message: " + error[1], html );
        return;
    }

    // Find result table
    var resultTableRegExp = /<table [^>]*?class="hafasButtons"[^>]*>[\s\S]*?<\/table>\s*<table [^>]*?class="hafasResult"[^>]*>([\s\S]*)<\/table>/i;
    if ( (resultTable = resultTableRegExp.exec(html)) ) {
        str = resultTable[1];
    } else {
        helper.error("Result table not found!", html);
        return;
    }

    // Initialize regular expressions
    var departureRegExp = /<tr class="[^"]*">([\s\S]*?)<\/tr>/ig;
    var columnsRegExp = /<td[^>]*?>([\s\S]*?)<\/td>/ig;
    var typeOfVehicleRegExp = /<a[^>]*><img src="\/hafas-res\/img\/([^_]*)_pic.gif"[^>]*>/i;
    var targetRegExp = /<span[^>]*>([\s\S]*)<\/span>/i;
    var routeBlocksRegExp = /\r?\n\s*(-|&#8226;)\s*\r?\n/gi;
    var routeBlockEndOfExactRouteMarkerRegExp = /&#8226;/i;

    var timeCol = 0, typeOfVehicleCol = 1, transportLineCol = 1, routeCol = 2, targetCol = 2, platformCol = 3;

    // Go through all departure blocks
    var departureNumber = 0;
    while ( (departureRow = departureRegExp.exec(str)) ) {
        // This gets the current departure row
        departureRow = departureRow[1];

        // Get column contents
        var columns = new Array;
        while ( (col = columnsRegExp.exec(departureRow)) ) {
            columns.push( col[1] );
        }
        columnsRegExp.lastIndex = 0;

        if ( columns.length < 3 ) {
            if ( departureRow.indexOf("<th") == -1 ) {
                helper.error("Too less columns in a departure row found (" + columns.length + ")", departureRow);
            }
            continue;
        }

        // Initialize result variables with defaults
        var time = 0, typeOfVehicle = "", transportLine = "", targetString = "", platformString = "",
            routeStops = new Array, routeTimes = new Array, exactRouteStops = 0;

        // Parse time column
        time = helper.matchTime( helper.trim(helper.stripTags(columns[timeCol])), "hh:mm" );
        if ( time.error ) {
            helper.error("Unexpected string in time column", columns[timeCol]);
            continue;
        }
        var dateTime = new Date();
        dateTime.setHours( time.hour );
        dateTime.setMinutes( time.minute );

        if ( (typeOfVehicle = typeOfVehicleRegExp.exec(columns[typeOfVehicleCol])) ) {
            typeOfVehicle = typeOfVehicleFromString( typeOfVehicle[1] );
        } else {
            helper.error("Unexpected string in type of vehicle column", columns[typeOfVehicleCol]);
            continue;
        }

        // Parse transport line column
        var transportLine = helper.trim( helper.stripTags(columns[transportLineCol]) );

        // Parse target column
        if ( (targetString = targetRegExp.exec(columns[targetCol])) ) {
            targetString = helper.trim( helper.stripTags(targetString[1]) );
        } else {
            helper.error("Unexpected string in target column", columns[targetCol]);
            continue;
        }

        // Parse route column
        var route = columns[routeCol];
        var posRoute = route.indexOf( "<br />" );
        if ( posRoute != -1 ) {
            route = route.substr( posRoute + 6 );
            var routeBlocks = route.split( routeBlocksRegExp );

            if ( !routeBlockEndOfExactRouteMarkerRegExp.test(route) ) {
                exactRouteStops = routeBlocks.length;
            } else {
                while ( (splitter = routeBlocksRegExp.exec(route)) ) {
                    ++exactRouteStops;
                    if ( routeBlockEndOfExactRouteMarkerRegExp.test(splitter) )
                        break;
                }
            }

            for ( var n = 0; n < routeBlocks.length; ++n ) {
                var lines = helper.splitSkipEmptyParts( routeBlocks[n], "\n" );
                if ( lines.count < 4 )
                    continue;

                routeStops.push( helper.trim(helper.stripTags(lines[1])) );
                routeTimes.push( lines[3] );
            }
        }

        // Parse platform column
        var platformString = columns.length > platformCol && typeof(columns[platformCol]) != 'undefined'
                ? helper.trim( helper.stripTags(columns[platformCol]) ) : "";

        // Add departure to the result set
        result.addData( {DepartureTime: dateTime,
            TransportLine: transportLine,
            TypeOfVehicle: typeOfVehicle,
            Platform: platformString,
            Target: targetString,
            RouteStops: routeStops,
            RouteTimes: routeTimes,
            RouteExactStops: exactRouteStops} );

        ++departureNumber;
    }

    if ( departureNumber == 0 ) {
        helper.error( "Regexp didn't match anything", str );
    }
}

function getStopSuggestions( values  ) {
    var url = "http://reiseauskunft.insa.de/bin/stboard.exe/dn?ld=web&L=vbn" +
    "&input=" + values.stop +
//  "&boardType=" + (values.dataType == "arrivals" ? "arr" : "dep") +
//  "&time=" + helper.formatDateTime(values.dateTime, "hh:mm") +
    "&disableEquivs=no" +
    "&maxJourneys=" + values.maxCount + "&start=yes";
    var html = network.getSynchronous( url );

    if ( network.lastDownloadAborted ) {
      return false;
    }

    // Find stop range
    var range = /<select class="error" name="input">\s*([\s\S]*)\s*<\/select>/i.exec( html );
    if ( range == null ) {
    helper.error( "Stop range not found!", html );
    return false;
    }
    range = range[1];

    // Initialize regular expressions (compile them only once)
    var stopRegExp = /<option value="[^"]+#([0-9]+)">([^<]*)<\/option>/ig;

    // Go through all stop options
    while ( (stop = stopRegExp.exec(range)) ) {
    var stopID = stop[1];
    var stopName = stop[2];

    // Add stop
    result.addData( {StopName: stop[1], StopID: stop[2]} );
    }

    return result.hasData();
}