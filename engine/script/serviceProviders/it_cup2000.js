/** Service provider travelplanner.cup2000.it (Regione Emilia-Romagna, Italia)
* © 2012, Friedrich Pülz */

include("base_hafas.js");

// Create instance of Hafas
var hafas = Hafas({
    baseUrl: "http://travelplanner.cup2000.it/rer",
    language: "e",
    productBits: 9 });

// No XML sources available, HTML parsers are implemented below
hafas.timetable.options.format = Hafas.HtmlFormat;
hafas.stopSuggestions.options.format = Hafas.HtmlFormat;
hafas.stopSuggestions.removeFeature( PublicTransport.ProvidesStopsByGeoPosition );

function features() {
    return hafas.stopSuggestions.features.concat(
            hafas.timetable.features );
}

var getStopSuggestions = hafas.stopSuggestions.get;
var getTimetable = hafas.timetable.get;

hafas.timetable.parser.parseHtml = function( html ) {
    // Decode document
    html = helper.decode( html, "utf8" );

    // Find block of departures
    var departureBlock = helper.findFirstHtmlTag( html, "div",
            {attributes: {"id": "sq_results_content_table_stboard"}} );
    if ( !departureBlock.found ) {
        helper.error("Result element not found", html);
        return;
    }

    departureBlock = helper.findFirstHtmlTag( departureBlock.contents, "tbody" );
    if ( !departureBlock.found ) {
        helper.error("Result table not found", html);
        return;
    }

    // Initialize regular expressions (compile them only once)
    var dateRegExp = /<a href="[^\"]*date=(\d{2}\.\d{2}\.\d{2})&amp;/i;
    var departuresRegExp = /<tr class="pari">([\s\S]*?)<\/tr>/ig;
    var columnsRegExp = /<t(?:d|h)[^>]*?>([\s\S]*?)<\/t(?:d|h)>/ig;
    var typeOfVehicleRegExp = /<a[^>]*?><img src="\/rer\/img\/products\/([^\.]*?)\./i;
    var targetRegExp = /<p class="tDirection">Direction:\s*<a[^>]*?>([\s\S]*?)<\/a>\s*<\/p>/ig;
    var paragraphRegExp = /<p>([\s\S]*?)<\/p>/i;
    var routeBlocksRegExp = /\r?\n\s*(-|<span class="ivuBold">&rarr;<\/span>)\s*\r?\n/gi;
    var routeBlockEndOfExactRouteMarkerRegExp = /<span class="ivuBold">&rarr;<\/span>/i;
//     var timeCompleteRegExp = /(\d{2}:\d{2})/i;
    var delayRegExp = /<span class="prognosis">approx.&nbsp;(\d{2}:\d{2})<\/span>/i;

    // Go through all departure blocks
    while ( (departureRowArr = departuresRegExp.exec(departureBlock.contents)) ) {
        departureRow = departureRowArr[1];

        // Get column contents
        var columns = new Array;
        while ( (col = columnsRegExp.exec(departureRow)) ) {
            columns.push( col[1] );
        }
        columnsRegExp.lastIndex = 0;
        if ( columns.length < 4 ) {
            helper.error("Too less columns found (" + columns.length + ") in a departure row", departureRow);
            continue;
        }

        // Initialize result variables with defaults
        var departure = { RouteStops: new Array, RouteTimes: new Array, RouteExactStops: 0 };

        // Parse time column
        time = helper.matchTime( helper.trim(columns[0]), "hh:mm" );
        if ( time.error ) {
            helper.error("Unexpected string in time column!", columns[0]);
            continue;
        }

        // Parse type of vehicle column
        if ( (typeOfVehicleArr = typeOfVehicleRegExp.exec(columns[1])) != null ) {
            var vehicle = typeOfVehicleArr[1].toLowerCase();
            if ( vehicle == "au" ) { // AutoBus
                vehicle = "bus";
            } // TODO Move to hafas.otherVehicleFromString()
            departure.TypeOfVehicle = hafas.vehicleFromString( vehicle );
        } else {
            helper.error("Unexcepted string in type of vehicle column", columns[1]);
        }

        // Parse transport line column
        departure.TransportLine = helper.simplify( helper.stripTags(columns[2]) );
        if ( departure.TransportLine.substring(0, 4) == "Lin " ) {
            departure.TransportLine = helper.trim( departure.TransportLine.substring(4) );
        }
        if ( (dateString = dateRegExp.exec(columns[2])) == null ) {
            helper.error("Unexcepted string in transport line column", columns[2]);
            continue;
        }
        departure.DepartureDateTime = helper.matchDate( dateString, "dd.MM.yy" );
        departure.DepartureDateTime.setHours( time.hour, time.minute, 0 );

        // Parse route column ..
        //  .. target
        if ( (targetString = targetRegExp.exec(columns[3])) == null ) {
            helper.error("Unexcepted string in target column", columns[3]);
            continue; // Unexcepted string in target column
        }
        departure.Target = helper.camelCase( helper.trim(targetString[1]) );

        // .. route
        var routeColumn = columns[3].substring( targetRegExp.lastIndex );
        targetRegExp.lastIndex = 0;
        var routeArr = paragraphRegExp.exec( routeColumn );
        if ( routeArr != null ) {
            var route = helper.trim( routeArr[1] );
            var routeBlocks = route.split( routeBlocksRegExp );

            if ( !routeBlockEndOfExactRouteMarkerRegExp.test(route) ) {
                departure.RouteExactStops = routeBlocks.length;
            } else {
                while ( (splitter = routeBlocksRegExp.exec(route)) ) {
                    ++departure.RouteExactStops;
                    if ( routeBlockEndOfExactRouteMarkerRegExp.test(splitter) )
                        break;
                }
            }

            for ( var n = 0; n < routeBlocks.length; ++n ) {
                var lines = helper.splitSkipEmptyParts( routeBlocks[n], "\n" );
                if ( lines.count < 4 )
                    continue;

                departure.RouteStops.push( helper.camelCase(lines[1]) );
                departure.RouteTimes.push( lines[3] );
            }
        }

//         // Parse platform column if any
//         if ( platformCol != -1 )
//             platformString = helper.trim( helper.stripTags(columns[platformCol]) );
//
//         Parse delay column if any
//         if ( delayCol != -1 ) {
//             delayArr = delayRegExp.exec( columns[delayCol] );
//             if ( delayArr )
//             delay = helper.duration( time[0] + ":" + time[1], delayArr[1] );
//         }

        // Add departure
        result.addData( departure );
    }

    return true;
}

// No ajax-getstops.exe, use timetable URL with a "?" to get HTML stop suggestions
hafas.stopSuggestions.url = function( values, options ) {
    options.stopPostfix = "?"; // Always show stop suggestions, no departures
    return hafas.timetable.url( values, options );
}

hafas.stopSuggestions.parser.parseHtml = function( html ) {
    // Decode document
    html = helper.decode( html, "utf8" );

    // Find all stop suggestions
    var pos = html.search( /<select\s*name="input"\s*>/i );
    if ( pos == -1 ) {
        helper.error("Stop suggestion element not found!", html);
        return;
    }
    var end = html.indexOf( '</select>', pos + 1 );
    var str = html.substr( pos, end - pos );

    // Initialize regular expressions (compile them only once)
    var stopRegExp = /<option value="[^"]+?#([0-9]+)">([^<]*?)<\/option>/ig;

    // Go through all stop options
    while ( (stop = stopRegExp.exec(str)) ) {
        result.addData( {StopName: stop[2], StopID: stop[1]} );
    }

    return result.hasData();
}
