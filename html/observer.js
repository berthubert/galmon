"use strict";
var repeat;

moment.relativeTimeThreshold('m', 120);

function flippedStereographic(x, y)  {
    var cx = Math.cos(x), cy = Math.cos(y), k = 1 / (1 + cx * cy);
    return [k * cy * Math.sin(x), -k * Math.sin(y)];
}

var gnss_position=[];
function componentDidMount() {
    var sats = d3.select(".Drawing").html("").append("svg");

    var width = 500; //sats.clientWidth;
    var height = 500; //sats.clientHeight;

    sats.attr("width", 500);
    sats.attr("height", 500);
    
    var projection = d3.geoProjection(flippedStereographic)
        .scale(width * 0.40)
        .clipAngle(130)
        .rotate([0, -90])
        .translate([width / 2 + 0.5, height / 2 + 0.5])
        .precision(1);

    
    var path = d3.geoPath().projection(projection);

    sats.append("path")
        .datum(d3.geoCircle().center([0, 90]).radius(90))
        .attr("stroke-width", 1.5)
        .attr("d", path);

    sats.append("path")
        .datum(d3.geoGraticule())
        .attr("stroke-width", 0.15)
        .attr("d", path);
//    .attr("fill", "none").attr("stroke", "black").attr("width", "100%").attr("height", "100%");


    sats.append("g")
        .selectAll("line")
        .data(d3.range(360))
        .enter().append("line")
        .each(function(d) {
            var p0 = projection([d, 0]),
                p1 = projection([d, d % 10 ? -1 : -2]);
            
            d3.select(this)
                .attr("x1", p0[0])
                .attr("y1", p0[1])
                .attr("x2", p1[0])
                .attr("y2", p1[1]);
        });
    
    sats.append("g")
        .attr("fill", "black")
        .attr("stroke", "none")
        .selectAll("text")
        .data(d3.range(0, 360, 10))
        .enter().append("text")
        .each(function(d) {
            var p = projection([d, -4]);
            d3.select(this).attr("x", p[0]).attr("y", p[1]);
        })
        .attr("dy", "0.35em")
        .text(function(d) { return d === 0 ? "N" : d === 90 ? "E" : d === 180 ? "S" : d === 270 ? "W" : d + "°"; })
        .data(d3.range(0, 360, 90), function(d) { return d; })
        .attr("font-weight", "bold")
        .attr("font-size", 14);
    
    sats.append("g")
        .attr("fill", "#A3ACA9")
        .attr("stroke", "none")
        .selectAll("text")
        .data(d3.range(10, 91, 10))
        .enter().append("text")
        .each(function(d) {
            var p = projection([0, d]);
            d3.select(this).attr("x", p[0]).attr("y", p[1]);
        })
        .attr("dy", "-0.4em")
        .text(function(d) { return d + "°"; });

        sats.select('g.satellites').remove();
    
        let points = sats
            .insert("g")
            .attr("class", "satellites")
            .selectAll('g.satellite')
            .data(gnss_position)
            .enter()
            .append('g')
            .attr("transform", function(d) {
                var p = projection(d);
                return 'translate(' + p[0] + ', ' + p[1] + ')';
            });
            
        points
            .attr('class', 'satellite')
            .append("circle")
            .attr("stroke", function(d) {
                return d[3] > 0 ? "transparent" : d[5];
            })
            .attr("r", function(d) {
                return d[3] > 0 ? d[3]*0.5 : 3;
            })
        .attr("fill", function(d) {
                return d[3] > 0 ? d[5] : "transparent";
            });

     points
            .attr("r", 50)
            .append("text")
            .attr("class", "sats-label")
            .attr('dy', function(d) {
                return d[3] > 0 ? `${10+(d[3]/2)}px` : "1.3em";
            })
            .attr('dx', function(d) {
                return d[3] > 0 ? `${3+(d[3]/2)}px` : "0.7em";
            })
            .text(function(d){return d[2]})
    
}

function makeTable(str, obj)
{
    var table=d3.select(str);
    table.html("");
    var thead=table.append("thead");
    var tbody=table.append("tbody");
    var arr=[];
    gnss_position=[];
    Object.keys(obj).forEach(function(e) {
        if(e=="svs") {
            Object.keys(obj[e]).forEach(function(k) {
		if(obj[e][k].elev && obj[e][k].azi) {
                var obj2 ={id: k, elev: obj[e][k].elev.toFixed(1),
                          sigid: obj[e][k].sigid, 
                          db: obj[e][k].db, azi: obj[e][k].azi.toFixed(1),
                           "age-s": obj[e][k]["age-s"],
                          prres: obj[e][k].prres.toFixed(1)};
                
                if(obj[e][k].delta_hz_corr != null)
                    obj2["delta_hz_corr"]= obj[e][k].delta_hz_corr.toFixed(1);
                if(obj[e][k].qi != null)
                    obj2["qi"]= obj[e][k].qi;
                if(obj[e][k].used != null)
                    obj2["used"]= obj[e][k].used;
                
                arr.push(obj2);
                let color="blue";
                let gnssid = obj[e][k].gnss;
                if(gnssid == 0)
                    color="green";
                else if(gnssid == 2)
                    color="blue";
                else if(gnssid==3)
                    color="red"
                else if(gnssid==6)
                    color="yellow";
                gnss_position.push([obj[e][k].azi, obj[e][k].elev, k.split("@")[0] , obj[e][k].db/4,4, color]);
		}
            });
        }
        else
            arr.push({id: e, value: obj[e]});
        
    });

    var rows=tbody.selectAll("tr").
        data(arr).
        enter().
        append("tr");

    var columns= ["id", "value", "sigid", "azi", "elev", "db", "qi", "used", "prres", "delta_hz_corr", "age-s"];
    
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
                
                if(column == "id" && row.sigid != null)
                {
                        var gnidstr = row.id.split("@")[0].substring(0,1).trim();
                        var svstr = row.id.split("@")[0].substring(1).trim();
                        var sigid = row.sigid;

                        var gnssid = 0;
                        if (gnidstr === "C") gnssid = 3;
                        if (gnidstr === "G") gnssid = 0;
                        if (gnidstr === "E") gnssid = 2;
                        if (gnidstr === "R") gnssid = 6;

                        ret.value = "<a href='sv.html?gnssid=" + gnssid + "&sv=" + svstr + "&sigid=" + sigid + "'>"+row.id+"</a>";
                }
                
                return ret;
            })}).
        enter().append("td").html(function(d) {
            return d.value;
            
        }).attr("align", d=> d.align).style("background-color", d=> d.color);

}

var sats={};
var lastseen=null;
var observer=0;

function update()
{
    var seconds = 10;
    clearTimeout(repeat);
    repeat=setTimeout(update, 1000.0*seconds);

    if(lastseen != null)
        d3.select("#freshness").html(lastseen.fromNow());
    d3.json("./global.json", function(d) {
        lastseen = moment(1000*d["last-seen"]);
        d3.select("#freshness").html(lastseen.fromNow());
    });


    d3.queue(1).defer(d3.json, "./observers.json").awaitAll(ready);
    
    function ready(error, results) {
        var obj = {};
        for(var n = 0 ; n < results[0].length; ++n) {
            if(results[0][n].id == observer) {
                obj=results[0][n];
                break;
            }
        }

        makeTable("#galileo", obj);
        componentDidMount();
    };
      
}

var url = new URL(window.location.href);
observer = url.searchParams.get("observer");

repeat=update();


