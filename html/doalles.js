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
    
    var columns = ["sv", "best-tle", "iod", "eph-age-m", "latest-disco", "time-disco", "sisa", "health", "tle-dist", "alma-dist", "delta-utc", "delta-gps", "sources", "db", "delta_hz_corr","prres", "elev", "last-seen-s"];    
    
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
                else if(column == "best-tle") {
                    ret.value = "<small>"+row[column]+"</small>";
                    if(row["best-tle-dist"] != null) {
                        if (Math.abs(row["best-tle-dist"]) > 10000)
                            ret.color="red";
                        else if (Math.abs(row["best-tle-dist"]) > 1000)
                            ret.color="#ffaaaa";
                    } 
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
                    ret.value = ((100*row[column]).toFixed(1))+" cm";
                else if(column == "time-disco" && row[column] != null) 
                    ret.value = row[column].toFixed(1)+" ns";
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
    setButtonSettings();
        Object.keys(sats).forEach(function(e) {
            var o = sats[e];
            o.sv=e;
            o.sources="";
            o.db="";
            o.elev="";
            o.delta_hz_corr="";
            o.prres="";
            Object.keys(o.perrecv).forEach(function(k) {
                if(o.perrecv[k]["last-seen-s"] < 1800) {
                    o.sources = o.sources + '<a href="observer.html?observer=' + k + '">'+k+'</a> ';

                    o.db = o.db + o.perrecv[k].db +" ";
                    if(o.perrecv[k].elev != null)
                        o.elev = o.elev + o.perrecv[k].elev.toFixed(0)+" ";
                    else
                        o.elev = o.elev + "? ";

                    if(o.delta_hz_corr == null)
                        o.delta_hz_corr ="";
                    if(o.perrecv[k].delta_hz_corr != null)
                        o.delta_hz_corr = o.delta_hz_corr + o.perrecv[k].delta_hz_corr.toFixed(0)+" ";
                    else
                        o.delta_hz_corr = o.delta_hz_corr + "_ ";

                    if(o.prres == null)
                        o.prres ="";
                    if(o.perrecv[k].prres != null)
                        o.prres = o.prres + o.perrecv[k].prres.toFixed(0)+" ";
                    else
                        o.prres = o.prres + "_ ";

                    
                    
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
            let wantIt = false;
            if(d3.select("#GalE1").property("checked") && arr[n].gnssid==2 && arr[n].sigid == 1)
                wantIt = true;
            if(d3.select("#GalE5b").property("checked") && arr[n].gnssid==2 && arr[n].sigid == 5)
                wantIt = true;
            if(d3.select("#GPSL1CA").property("checked") && arr[n].gnssid==0 && arr[n].sigid == 0)
                wantIt = true;
            if(d3.select("#GPSL2C").property("checked") && arr[n].gnssid==0 && arr[n].sigid == 4)
                wantIt = true;
            if(d3.select("#BeiDouB1I").property("checked") && arr[n].gnssid==3 && arr[n].sigid == 0)
                wantIt = true;
            if(d3.select("#BeiDouB2I").property("checked") && arr[n].gnssid==3 && arr[n].sigid == 2)
                wantIt = true;
            if(d3.select("#GlonassL1").property("checked") && arr[n].gnssid==6 && arr[n].sigid == 0)
                wantIt = true;
            if(d3.select("#GlonassL2").property("checked") && arr[n].gnssid==6 && arr[n].sigid == 2)
                wantIt = true;
  
            if(!wantIt)
                continue;
            if(arr[n]["last-seen-s"] < 600)
                livearr.push(arr[n]);
            else
                stalearr.push(arr[n]);
        }

        maketable("#svs", livearr);
        maketable("#svsstale", stalearr);

}

function update()
{
    var seconds = 2;
    clearTimeout(repeat);
    repeat=setTimeout(update, 1000.0*seconds);

    if(lastseen != null)
        d3.select("#freshness").html(lastseen.fromNow());

    
    d3.json("global.json", function(d) {
        d3.select('#facts').html("Galileo-UTC offset: <b>"+d["utc-offset-ns"].toFixed(2)+"</b> ns, Galileo-GPS offset: <b>"+d["gps-offset-ns"].toFixed(2)+"</b> ns, GPS UTC offset: <b>"+d["gps-utc-offset-ns"].toFixed(2)+"</b>. "+d["leap-seconds"]+"</b> leap seconds");
        lastseen = moment(1000*d["last-seen"]);
        d3.select("#freshness").html(lastseen.fromNow());
    });
    
    d3.json("svs.json", function(d) {
        // put data in an array
        sats=d;
        updateSats();
        
    });
}

function getButtonSettings(name, def)
{
    let itemName = "want"+name;
    let value = localStorage.getItem(itemName);
    if(value == null) {
        console.log("Defaulting "+itemName+" to "+def);
        localStorage.setItem(itemName, def);
    }
//    else
         // console.log("Found "+itemName+" set to '"+value+"'");
    d3.select("#"+name).property("checked", localStorage.getItem(itemName)=="true");
}

function setButtonSetting(name)
{
//    console.log("Storing button state want"+name+" as "+d3.select("#"+name).property("checked"));
    localStorage.setItem("want"+name, d3.select("#"+name).property("checked"));
}

var modes=["GalE1", "GalE5b", "GPSL1CA", "GPSL2C", "BeiDouB1I", "BeiDouB2I", "GlonassL1", "GlonassL2"];

function setButtonSettings()
{
    modes.forEach(function(d) {
        setButtonSetting(d);
    });
}

getButtonSettings("GalE1", true);
modes.forEach(function(d) {
    getButtonSettings(d, false);
});;

repeat=update();


