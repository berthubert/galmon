"use strict";
var repeat;

moment.relativeTimeThreshold('m', 120);

function makeTable(str, arr, svs)
{
    var table=d3.select(str);
    table.html("");
    var thead=table.append("thead");
    var tbody=table.append("tbody");

    var header=["",""];
    var rows=tbody.selectAll("tr").
        data(header).
        enter().
        append("tr");
    
    var columns = arr;
    columns.unshift("@");
    console.log(columns);
    
    // append the header row
    thead.append("tr")
        .selectAll("th")
        .data(columns)
        .enter()
        .append("th")
        .text(function(d) {
            return d;
        });

    var sigid = 1;
    var cells = rows.selectAll("td").
        data(function(row) {
            var oldsigid = sigid;
            sigid = 5;
            return columns.map(function(column) {
                var ret={};
                ret.sigid = oldsigid;
                console.log("column: "+column);
                console.log("sigid: "+oldsigid);
                if(column=="@")
                    ret.value = oldsigid;
                else if(sigid == 1)
                    ret.value = svs[column+"@"+sigid].e1bhs; 
                else if(sigid == 5)
                    ret.value = svs[column+"@"+sigid].e5bhs; 

                if(ret.value == 0 || column=='@')
                    ret.color = null;
                else if(ret.value == 3)
                    ret.color="orange";
                else ret.color = "red";
                
                return ret;
            })}).
        enter().append("td").html(function(d) {
            return d.value;
            
        }).attr("align", "right").style("background-color", d=> d.color);

}

var sats={};
var lastseen=null;
function update()
{
    var seconds = 2;
    clearTimeout(repeat);
    repeat=setTimeout(update, 5000.0*seconds);

    if(lastseen != null)
        d3.select("#freshness").html(lastseen.fromNow());
    d3.json("global", function(d) {
        lastseen = moment(1000*d["last-seen"]);
        d3.select("#freshness").html(lastseen.fromNow());
    });


    d3.queue(1).defer(d3.json, "../svs").defer(d3.json, "../almanac").defer(d3.json, "../observers").awaitAll(ready);
    
    function ready(error, results) {
        var arr=[];
        Object.keys(results[0]).forEach(function(e) {
//            console.log(results[0][e]);
            if(results[0][e].gnssid == 2 && results[0][e].sigid==1)
                arr.push(e.slice(0,3));
        });
        makeTable("#galileo", arr, results[0]);
    };
      
}

repeat=update();


