"use strict";
var repeat;

moment.relativeTimeThreshold('m', 120);

function makeTable(str, arr)
{
    var table=d3.select(str);
    table.html("");
    var thead=table.append("thead");
    var tbody=table.append("tbody");

    var rows=tbody.selectAll("tr").
        data(arr).
        enter().
        append("tr");

    var columns= ["id1", "value1", "id2", "value2"];
    
    // append the header row
    thead.append("tr")
        .selectAll("th")
        .data(columns)
        .enter()
        .append("th")
        .text(function(d) {
            return d;
        });

    var cells = rows.selectAll("td").
        data(function(row) {
            return columns.map(function(column) {
                var ret={};
                ret.align = "right";
                if(column=="id1" || column == "id2")
                    ret.value = row[column]+":";
                else
                    ret.value = row[column];

                ret.color= null;
                return ret;
            })}).
        enter().append("td").html(function(d) {
            return d.value;
            
        }).attr("align", d=> d.align).style("background-color", d=> d.color);

}

var sats={};
var lastseen=null;
var sv=2;
var gnssid=3;
var sigid=0;

function update()
{
    var seconds = 2;
    clearTimeout(repeat);
    repeat=setTimeout(update, 1000.0*seconds);

    if(lastseen != null)
        d3.select("#freshness").html(lastseen.fromNow());
    d3.json("./global.json", function(d) {
        lastseen = moment(1000*d["last-seen"]);
        d3.select("#freshness").html(lastseen.fromNow());
    });


    d3.queue(1).defer(d3.json, "./sv.json?gnssid="+gnssid+"&sv="+sv+"&sigid="+sigid).defer(d3.json, "./almanac.json").awaitAll(ready);
    
    function ready(error, results) {
        var arr=[];
        Object.keys(results[0]).forEach(function(e) {
            arr.push({id: e, value: results[0][e]});
        });

        var newarr=[];
        for(var n=0 ; n < arr.length; n+=2) {
            if(n + 1 < arr.length)
                newarr.push({id1: arr[n].id, value1: arr[n].value, id2: arr[n+1].id, value2: arr[n+1].value});
            else
                newarr.push({id1: arr[n].id, value1: arr[n].value, id2: "", value2: ""});
        }
        makeTable("#galileo", newarr, results[0]);
    };
      
}

console.log(window.location.href);
var url = new URL(window.location.href);
sv = url.searchParams.get("sv");
gnssid = url.searchParams.get("gnssid");
sigid = url.searchParams.get("sigid");




repeat=update();


