var repeat;

moment.relativeTimeThreshold('m', 120);

function maketable(str, arr)
{
    var table=d3.select(str);
    table.html("");
    var thead=table.append("thead");
    var tbody=table.append("tbody");
    
    var rows=tbody.selectAll("tr").
        data(arr).
        enter().
        append("tr");
    
    var columns = ["sv", "best-tle", "best-tle-dist", "best-tle-norad", "best-tle-int-desig", "eph-ecefX", "eph-ecefY", "eph-ecefZ", "tle-ecefX", "tle-ecefY", "tle-ecefZ", "eph-latitude", "eph-longitude", "tle-latitude", "tle-longitude", "tle-eciX", "tle-eciY", "tle-eciZ", "t0e", "t", "E", "M0"];    
    var columndescriptions = {"sv":"Satellite Vehicle, an identifier for a satellite, other satellis could take over this number in case of failures", 
                              "best-tle": "From TLE database, closest satellite to the reported position", 
                              "best-tle-dist": "Distance to the closest satellite to the reported position",
                              "best-tle-norad": "?", 
                              "best-tle-int-desig": "?", 
                              "eph-ecefX": "?",
                              "eph-ecefY": "?", 
                              "eph-ecefZ": "?", 
                              "tle-ecefX": "?", 
                              "tle-ecefY": "?", 
                              "tle-ecefZ": "?", 
                              "eph-latitude": "Latitute of the ephemeris", 
                              "eph-longitude": "Longitude of the ephemeris", 
                              "tle-latitude": "?", 
                              "tle-longitude": "?", 
                              "tle-eciX": "?", 
                              "tle-eciY": "?", 
                              "tle-eciZ": "?", 
                              "t0e":"?",
                              "t":"?",
                              "E":"?", 
                              "M0":"?"};    
    
    // append the header row
    thead.append("tr")
        .selectAll("th")
        .data(columns)
        .enter()
        .append("th")
        .attr("title",columndescriptions[column])
        .text(function(column) {
            if(column == "delta_hz_corr")
                return "ΔHz";
            else
                return column;
        });
    
    var cells = rows.selectAll("td").
        data(function(row) {
            return columns.map(function(column) {
                var ret={};
                ret.column = column;
                ret.color=null;
                if(row[column] != null && column != "sv" && column != "best-tle" && column != "best-tle-norad" && column != "best-tle-int-desig")
                    ret.value = row[column].toFixed(1);
                else
                    ret.value = row[column];
                return ret;
            })
        })
        .enter().append("td").html(function(d) { return d.value; }).attr("align", "right").style("background-color", function(d) {
            return d.color;
        });

}

var sats={};
var lastseen=null;
function update()
{
    var seconds = 2;
    clearTimeout(repeat);
    repeat=setTimeout(update, 1000.0*seconds);

    if(lastseen != null)
        d3.select("#freshness").html(lastseen.fromNow());
    d3.json("global.json", function(d) {
        lastseen = moment(1000*d["last-seen"]);
        d3.select("#freshness").html(lastseen.fromNow());
    });

    
    d3.json("almanac.json", function(d) {
        // put data in an array
        sats=d;
        var arr=[];
        Object.keys(d).forEach(function(e) {
            var o = d[e];
            o.sv=e;
            arr.push(o);
        });

        // sort it on SV
        arr.sort(function(a, b) {
            if(Number(a.sv) < Number(b.sv))
                return -1;
            if(Number(b.sv) < Number(a.sv))
                return 1;
            return 0;
        });

        var livearr=[];
        for(n = 0 ; n < arr.length; n++)
        {
            livearr.push(arr[n]);
        }

        maketable("#svs", livearr);
    });
}

repeat=update();


