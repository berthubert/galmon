"use strict";
var repeat;

moment.relativeTimeThreshold('m', 120);

function escapeHTML(html) {
    return document.createElement('div').appendChild(document.createTextNode(html)).parentNode.innerHTML;
}


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

    var columns= ["id", "last-seen", "latitude", "longitude", "owner", "remark", "serialno", "hwversion", "swversion", "mods", "githash", "uptime", "clockdriftns", "clockacc", "freqacc", "h", "acc", "satellites"];
    
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
                ret.color = null;
                if(column == "id") {
                    ret.value='<a href="observer.html?observer='+row[column]+'">'+row[column]+"</a>";
                }
                else if(column == "last-seen") {
                    ret.value = moment(1000*row["last-seen"]).fromNow();
                    let lastSeen = moment(1000*row["last-seen"]);
                    let age = (moment() - lastSeen);
//                    console.log(age.valueOf()/1000);
                    if(age.valueOf() / 60000 > 5)
                        ret.color = "red";
                   
                }
                else if(column == "satellites") {
                    ret.value = 0;
                    Object.keys(row["svs"]).forEach(function(d) {ret.value = ret.value +1 });
                }
                else {
                    ret.value = escapeHTML(row[column]);
                }
                return ret;
            })}).
        enter().append("td").html(function(d) {
            return d.value;
            
        }).attr("align", d=> d.align).style("background-color", d=> d.color);

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
    d3.json("./global.json", function(d) {
        lastseen = moment(1000*d["last-seen"]);
        d3.select("#freshness").html(lastseen.fromNow());
    });


    d3.queue(1).defer(d3.json, "./svs.json").defer(d3.json, "./observers.json").awaitAll(ready);
    
    function ready(error, results) {
        var arr=[];
        Object.keys(results[1]).forEach(function(e) {
            arr.push(results[1][e]);
        });
        makeTable("#galileo", arr, results[0]);
    };
      
}

repeat=update();


