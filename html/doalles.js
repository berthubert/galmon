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
    
    var columns = ["sv", "best-tle", "norad", "iod", "aodc/e", "eph-age-m", "latest-disco", "time-disco", "sisa", "delta_hz_corr", "health", "tle-dist", "alma-dist", "delta-utc", "delta-gps", "sources", "db", "elev", "last-seen-s"];    
    
    // append the header row
    thead.append("tr")
        .selectAll("th")
        .data(columns)
        .enter()
        .append("th")
        .text(function(column) {
            if(column == "delta_hz_corr")
                return "ΔHz";
            if(column == "delta-gps")
                return "ΔGPS ns";
            if(column == "delta-utc")
                return "ΔUTC ns";
            else
                return column;
        });
    
    var cells = rows.selectAll("td").
        data(function(row) {
            return columns.map(function(column) {
                var ret={};
                ret.column = column;
                ret.color=null;
                ret.Class = null;
                if(column == "sv") {
                    var img="";
                    if(row["gnssid"] == 0)
                        img="ext/gps.png";
                    else if(row["gnssid"] == 2)
                        img='ext/gal.png';
                    else if(row["gnssid"] == 3)
                        img='ext/bei.png';
                    else if(row["gnssid"] == 6)
                        img='ext/glo.png';
                    
                    ret.value = '<img width="16" height="16" src="https://ds9a.nl/tmp/'+ img +'"/>';
//                    ret.value="";
                    ret.value += "&nbsp;"+row.sv;
                }
                else if(column == "aodc/e") {
                    if(row["aodc"] != null && row["aode"] != null)
                        ret.value = row["aodc"]+"/"+row["aode"];
                    else if(row["aode"] != null)
                        ret.value = row["aode"];
                    else
                        ret.value="";
                }
                else if(column == "eph-age-m") {
                    if(row[column] != null) {
                        var b = moment.duration(-row[column], 'm');
                        ret.value = b.humanize(true);
                    }
                    else
                        ret.value="";
                }
                else if(row[column] != null && (column == "delta_hz_corr" || column =="delta_hz")) {

                    ret.value = row[column].toFixed(0);
                }
                else if((column == "tle-dist")) {
                    if(row["best-tle-dist"] != null)
                        ret.value = row["best-tle-dist"].toFixed(1) + " km";
                }
                else if((column == "alma-dist")) {
                    if(row["alma-dist"] != null)
                        ret.value = row["alma-dist"].toFixed(1) + " km";
                }

                else if(column == "norad") {
                    ret.value = row["best-tle-norad"];
                }
                else if(column == "delta-utc" && row["delta-utc"] != null) {
                    ret.value = row["delta-utc"]+'<span class="CellComment">a0: '+row["a0"]+'<br/>a1: '+row["a1"]+'<br/>wn0t: ' + row["wn0t"]+'<br/>t0t: '+row["t0t"]+'</span>';
                    ret.Class = 'CellWithComment';
                }
                else if(column == "delta-gps" && row["delta-gps"] != null) {
                    ret.value = row["delta-gps"]+'<span class="CellComment">a0g: '+row["a0g"]+'<br/>a1g: '+row["a1g"]+'<br/>wn0g: ' +row["wn0g"]+'<br/>t0g: '+row["t0g"]+'</span>';
                    ret.Class = 'CellWithComment';
                }

                else if(column == "last-seen-s") {
                    var b = moment.duration(row[column], 's');
                    ret.value = b.humanize();
                }
                else if(column == "gpshealth" && row[column] != null) {
                    if(row[column]==0)
                        ret.value = "ok";
                    else {
                        ret.value = "NOT OK";
                        ret.color="red";
                    }
                }
                else if(column == "latest-disco" && row[column] != null) 
                    ret.value = ((100*row[column]).toFixed(2))+" cm";
                else if(column == "time-disco" && row[column] != null) 
                    ret.value = row[column].toFixed(2)+" ns";
                else if(column == "health")  {
                    ret.value = row[column];
                    if(row["healthissue"] == 1) {
                        ret.color="orange";
                    }
                    if(row["healthissue"] == 2) {
                        ret.color="red";
                    }
                }                
                else {
                    ret.value= row[column];
                }
                if(column == "eph-age-m" && row[column] > 60 && row["last-seen-s"] < 120)
                    ret.color="orange";
                if(column == "eph-age-m" && row[column] > 4*60 && row["last-seen-s"] < 120)
                    ret.color="#ff2222";

                if(column == "sisa" && parseInt(row[column]) > 312)
                    ret.color="#ffaaaa";
                var myRe = RegExp('[0-9]* m');
                if(column == "sisa" && myRe.test(row[column]))
                    ret.color="#ff2222";

                if(column == "sisa" && row[column]=="NO SIS AVAILABLE")
                    ret.color="#ff2222";                    
                return ret;
            })
        })
        .enter().append("td").attr("class", function(d) { return d.Class; }).html(function(d) { return d.value; }).attr("align", "right").style("background-color", function(d) {
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

    
    d3.json("global", function(d) {
        d3.select('#facts').html("Galileo-UTC offset: <b>"+d["utc-offset-ns"].toFixed(2)+"</b> ns, Galileo-GPS offset: <b>"+d["gps-offset-ns"].toFixed(2)+"</b> ns, GPS UTC offset: <b>"+d["gps-utc-offset-ns"].toFixed(2)+"</b>. "+d["leap-seconds"]+"</b> leap seconds");
        lastseen = moment(1000*d["last-seen"]);
        d3.select("#freshness").html(lastseen.fromNow());
    });
    
    d3.json("svs", function(d) {
        // put data in an array
        sats=d;
        var arr=[];
        Object.keys(d).forEach(function(e) {
            var o = d[e];
            o.sv=e;
            o.sources="";
            o.db="";
            o.elev="";
            Object.keys(o.perrecv).forEach(function(k) {
                if(o.perrecv[k]["last-seen-s"] < 1800) {
                    o.sources = o.sources + k +" ";
                    o.db = o.db + o.perrecv[k].db +" ";
                    if(o.perrecv[k].elev != null)
                        o.elev = o.elev + o.perrecv[k].elev.toFixed(0)+" ";
                    else
                        o.elev = o.elev + "? ";
                }
            });
            
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

        var livearr=[], stalearr=[];
        for(n = 0 ; n < arr.length; n++)
        {
//            if(arr[n]["gnssid"]) continue;
            if(arr[n]["last-seen-s"] < 600)
                livearr.push(arr[n]);
            else
                stalearr.push(arr[n]);
        }

        maketable("#svs", livearr);
        maketable("#svsstale", stalearr);
        
    });
}

repeat=update();


