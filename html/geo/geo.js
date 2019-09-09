
var repeat;

var world={};
d3.json("world.geojson", function(result) {
    world=result;
    update();
});

var oldsats=[];

function update()
{
    var seconds = 60;
    clearTimeout(repeat);
    repeat=setTimeout(update, 1000.0*seconds);

    d3.queue(1).defer(d3.json, "../almanac").defer(d3.json, "../observers").awaitAll(ready);
    
    function ready(error, results)
    {
        //    var aProjection = d3.geoMercator().scale(100).translate([250,250]);

        // all this complexity is so we can scale to full screen.
        // see: https://stackoverflow.com/questions/14492284/center-a-map-in-d3-given-a-geojson-object
        
        var mapDiv = document.getElementById("map");

        var center = [0,0];
        var scale  = 150;
        var offset = [mapDiv.clientWidth/2, mapDiv.clientHeight/2];
        
        var aProjection = d3.geoEquirectangular().scale(scale).translate([mapDiv.clientWidth/2,mapDiv.clientHeight/2]);
        var geoPath = d3.geoPath().projection(aProjection);

        // using the path determine the bounds of the current map and use 
        // these to determine better values for the scale and translation
        var bounds  = geoPath.bounds(world);
        var hscale  = scale*mapDiv.clientWidth  / (bounds[1][0] - bounds[0][0]);
        var vscale  = scale*mapDiv.clientHeight / (bounds[1][1] - bounds[0][1]);
        scale   = (hscale < vscale) ? hscale : vscale;
        var offset  = [mapDiv.clientWidth - (bounds[0][0] + bounds[1][0])/2,
                       mapDiv.clientHeight - (bounds[0][1] + bounds[1][1])/2];

        // new projection
        aProjection = d3.geoEquirectangular().center(center)
            .scale(scale).translate(offset);

        geoPath = d3.geoPath().projection(aProjection);
        
        var svg =d3.select("svg");
        svg.html("");

        svg.attr("width", mapDiv.clientWidth);
        svg.attr("height", mapDiv.clientHeight);

        
        svg.selectAll("path").data(world.features)
            .enter()
            .append("path")
            .attr("d", geoPath)                                           
            .attr("class", "countries");
        
        var arr=[];
        Object.keys(results[0]).forEach(function(e) {
            var o = results[0][e];
            o.sv=e;

            if(o["eph-latitude"] != null) {
                arr.push(o);
            }
        });


        svg.selectAll("circle").data(arr)
            .enter()
            .append("circle")
            .attr("class", "sats")
            .attr("r", 3)
            .attr("cx", d => aProjection([d["eph-longitude"],d["eph-latitude"]])[0])                
            .attr("cy", d => aProjection([d["eph-longitude"],d["eph-latitude"]])[1])
            .attr("fill", function(d) { if(d.gnssid==2) return "blue";
                                        if(d.gnssid==3) return "red";
                                        else return "green"; });

        svg.selectAll("text").data(arr)             
            .enter()
            .append("text")
            .attr("dx", d => 5+aProjection([d["eph-longitude"],d["eph-latitude"]])[0])                
            .attr("dy", d => 5+aProjection([d["eph-longitude"],d["eph-latitude"]])[1])
            .text(d => d.sv);

        svg.selectAll("rect").data(results[1])             
            .enter()
            .append("rect")
            .attr("class", "sats")
            .attr("width", 8)
            .attr("height", 8)
            .attr("x", d => aProjection([d["longitude"],d["latitude"]])[0]-4)
            .attr("y", d => aProjection([d["longitude"],d["latitude"]])[1]-4)
            .attr("fill", function(d) { console.log("Hallo!"); return "black"; });

    }
}


// d3.select("body").onresize = update; 
