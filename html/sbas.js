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
    
    var columns = ["sv",  "health", "sources", "db", "last-do-not-use", "last-seen-s"];    
    
    // append the header row
    thead.append("tr")
        .selectAll("th")
        .data(columns)
        .enter()
        .append("th")
        .html(function(column) {
            if(column == "delta_hz_corr")
                return "ΔHz";
            if(column == "delta-gps")
                return "ΔGPS ns";
            if(column == "delta-utc")
                return "ΔUTC ns";
            if(column == "sources")
                return '<a href="observers.html">sources</a>';
            if(column == "alma-dist")
                return '<a href="almanac.html">alma-dist</a>';
            
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
                if(column == "last-do-not-use") {
                    if(row["last-type-0-s"] < 30*86400) {
                        var b = moment.duration(-row["last-type-0-s"], 's');
                        ret.value = b.humanize(true);
                    }
                    else ret.value="";
                }
                else if(column == "sv") {
                    var img="";
                    var sv = row["sv"];
                    var sbas="";
                    if(sv == 138 || sv == 131 || sv == 133) {
                        img = 'ext/gps.png';
                        sbas = "WAAS";
                    }
                    else if(sv== 126 || sv == 136 || sv == 123 ) {
                        img='ext/gal.png';
                        sbas = "EGNOS";
                    }
                    else if(sv == 140 || sv == 125 || sv == 141) {
                        img='ext/glo.png';
                        sbas = "SDCM";
                    }
                    else if(sv == 127 || sv == 128 || sv == 138) {
                        img='ext/gagan.png';
                        sbas ="GAGAN";
                    }

                    ret.value = sbas + "&nbsp;";
                    if(img != "")
                        ret.value += '<img width="16" height="16" src="https://ds9a.nl/tmp/'+ img +'"/>';
                    else
                        ret.value += "";
                    //                    ret.value="";

                    ret.value += "&nbsp;<a href='sv.html?gnssid="+row.gnssid+"&sv="+row.svid+"&sigid="+row.sigid+"'>"+row.sv+"</a>";
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
                    if(row["gnssid"]==0 && Math.abs(row[column]) > 120 && row["last-seen-s"] < 120)
                        ret.color="orange";
                    if(row["gnissid"]==2 && Math.abs(row[column]) > 60 && row["last-seen-s"] < 120)
                        ret.color="orange";

                    if(Math.abs(row[column]) >120 && row["last-seen-s"] < 120)
                        ret.color="#ff4444";
                    if(Math.abs(row[column]) > 4*60 && row["last-seen-s"] < 120)
                        ret.color="#ff2222";

                    
                    if(row[column] != null) {
                        var b = moment.duration(-row[column], 'm');
                        ret.value = b.humanize(true);
                    }
                    else
                        ret.value="";
                }
                else if(row[column] != null && (column == "delta_hz_corr" || column =="delta_hz")) {
                    ret.value = row[column];
                }
                else if((column == "tle-dist")) {
                    if(row["best-tle-dist"] != null) {
                        ret.value = row["best-tle-dist"].toFixed(1) + " km";

                        if (Math.abs(row["best-tle-dist"]) > 10000)
                            ret.color="red";
                        else if (Math.abs(row["best-tle-dist"]) > 1000)
                            ret.color="#ffaaaa";
                    } 
                }

                else if(column == "last-seen-s") {
                    var b = moment.duration(row[column], 's');
                    ret.value = b.humanize();
                }
                else if(column == "health")  {
                    ret.value = "<small>"+row[column]+"</small>";
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

                if(column == "sisa" && parseInt(row[column]) > 312)
                    ret.color="#ffaaaa";
                var myRe = RegExp('[0-9]* m');
                if(column == "sisa" && myRe.test(row[column]))
                    ret.color="#ff2222";

                if(column == "sisa" && row[column]=="NO SISA AVAILABLE")
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

function updateSats()
{
    var arr=[];
    Object.keys(sats).forEach(function(e) {
        var o = sats[e];
        o.sv=e;
            o.sources="";
            o.db="";
            o.elev="";
            o.delta_hz_corr="";
            o.sources="";
            let prrestot = 0, prresnum=0, dbtot=0, hztot=0, hznum=0;
            let mindb=1000, maxdb=0;
            let minelev=90, maxelev=-1;

            Object.keys(o.perrecv).forEach(function(k) {
                if(o.perrecv[k]["last-seen-s"] < 30) {
                    o.sources += k+" ";
                    
                    dbtot += o.perrecv[k].db;
                    if(o.perrecv[k].db != null) {
                        let db=o.perrecv[k].db;
                        if(db > maxdb)
                            maxdb = db;
                        if(db <mindb)
                            mindb =db;
                    }
                    if(o.perrecv[k].elev != null) {
                        let elev=o.perrecv[k].elev;
                        if(elev > maxelev)
                            maxelev = elev;
                        if(elev <minelev)
                            minelev =elev;
                    }

                }
            });

            if(mindb != 1000)
                o.db = mindb+" - " +maxdb;
            else
                o.db = "-";

            if(maxelev != -1)
                o.elev = minelev.toFixed(0)+" - " +maxelev.toFixed(0);
            else
                o.elev = "-";

            if(o.hqsources > 2) {
                if(prresnum)
                    o.prres = (prrestot / prresnum).toFixed(2);
                else
                    o.prres ="-";
                if(hznum)
                    o.delta_hz_corr = (hztot / hznum).toFixed(2);
                else
                    o.delta_hz_corr ="-";
            }
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
            if(arr[n]["last-seen-s"] < 600)
                livearr.push(arr[n]);
            else
                stalearr.push(arr[n]);
        }
    
        maketable("#sbas", livearr);
        maketable("#sbasstale", stalearr);

}

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
    
    d3.json("sbas.json", function(d) {
        // put data in an array
        sats=d;
        updateSats();
        
    });
}


repeat=update();


